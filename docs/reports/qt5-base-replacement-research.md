# Qt5 Base 完整替代选型调研

> 最后修改时间：2026-10-26 16:35 CST
> 文档版本：v16
> 修改说明：完成 QUuid 替换，接入平台安全随机源、统一令牌编码和核心测试。

## 结论与范围

当前已采用 **C++20 标准库 + nlohmann/json + Ada URL + utf8proc + simdutf**；窗口、调度、文件、MIME 和日志仍按平台边界分别评估。libuv、libevent、spdlog 都不是当前移除 Qt 的必要依赖。

前两个库是核心选型；其余边界按项目内部实现或平台能力处理。MIME 已采用固定扩展名 helper，不引入 libmagic；未来若需要无扩展名内容识别，再单独评估内容检测库。

本报告覆盖当前 Windows WebView2、macOS WKWebView、公共接口、资源管理、示例与测试，不讨论实现尚不存在的 Linux 后端，也不要求覆盖 Qt5 Base 中本项目没有使用的全部功能。用户已同意使用 C++20；MIME 固定扩展名 helper 的实施结果已在 v10 中同步。

本地依据为提交 `6f5fb4bf098d5649161132b8d648f66a24948681`。调研开始时工作区无未提交修改。外部资料查阅于 2026-10-25；带版本的链接说明已核验版本，不代表最新版本或最终锁定版本。没有执行候选库构建、性能实验或 Windows 运行验证，不提供体积、速度或工期的估算值冒充实测。

## 当前依赖覆盖

| Qt 用途 | 代码依据 | 替代方向与必须保留的行为 |
| --- | --- | --- |
| 公开字符串、URL、JSON、容器 | [公开头文件](../../include/system_webview/system_webview.h) | UTF-8 文本、项目 URL 类型、JSON 类型或序列化边界、标准容器；属于公开 API/ABI 变更 |
| Windows 视图容器 | [WebView2View.cpp](../../src/platform/windows/WebView2View.cpp) 的 NativeViewHost、applyHostState | 原生 HWND 容器；保留挂载、卸载、重设父窗口、尺寸、显隐和关闭语义 |
| UI 线程投递和失效检查 | 同文件的 runOnUiThread、PendingAction、QPointer | 原生消息队列与弱生命周期状态；不能只将 QPointer 换成裸指针 |
| macOS 容器与调度 | [WkWebView.mm](../../src/platform/macos/WkWebView.mm) | NSView、主队列和运行循环；保留挂载、布局、回调取消及 native delegate 清理 |
| JSON 编解码和消息协议 | [WebViewBridge.cpp](../../src/webview/WebViewBridge.cpp)、[JsonMessage.cpp](../../src/webview/JsonMessage.cpp) | nlohmann/json；保留对象校验、方向 schema、大小限制、请求匹配和取消 |
| URL、来源与路径映射 | [ResourceMapping.cpp](../../src/webview/ResourceMapping.cpp)、[WebViewPolicy.cpp](../../src/webview/WebViewPolicy.cpp) | Ada 加项目 URL 策略；区分网络 origin、内部 app 标识与本地文件路径 |
| 应用 ID 字符校验 | [Application.cpp](../../src/webview/Application.cpp) | 当前使用 QChar::isLetterOrNumber，接受 Unicode 字母和数字，不能直接缩成 ASCII 校验 |
| 文件快照、范围流、临时目录 | [WebResourceManager.cpp](../../src/webview/WebResourceManager.cpp) | 标准文件系统与平台文件句柄；保留不可变快照、原子发布、容量统计、读者租约和删除重试 |
| MIME 自动推断 | 资源管理器及两个平台的资源响应 | 扩展名规则与内容检测；不能只列 HTML/JS/CSS 后就宣布覆盖 |
| UUID、到期时间、重试定时器 | 资源管理器、平台后端 | 系统安全随机源、chrono、原生定时调度 |
| 日志分类与宿主接管 | [Diagnostics.cpp](../../src/webview/Diagnostics.cpp) | 分类日志回调；保留级别过滤、事件码、标识和敏感字段排除 |
| Qt 控件示例 | [DemoWindow.cpp](../../samples/demo/DemoWindow.cpp)、[Windows 控件示例](../../samples/windows_widget_demo/main.cpp) | 原生窗口、原生控件与布局，保留多视图/标签、弹窗接收和窗口操作；后者需改成原生控件示例 |
| Qt 事件循环与测试 HTTP 服务 | [WkViewTests.mm](../../tests/WkViewTests.mm)、[WebView2ContractTests.cpp](../../tests/WebView2ContractTests.cpp) | 原生消息循环、定时器及测试服务实现；不能仅关闭测试目标 |
| 构建和安装包依赖 | [CMakeLists.txt](../../CMakeLists.txt)、[包配置模板](../../cmake/system_webviewConfig.cmake.in) | 删除 Qt 查找、链接与导出依赖，同步预设、安装指南和使用方示例 |

## 三方库建议

### 1. nlohmann/json：已采用

- 已完整替代自有源码、测试和 demo 的 Qt JSON。公开使用 `webview::Json` 别名及项目 `JsonType`；Bridge、schema、资源控制和两平台 JSON 边界均已切换。macOS 验证 preset 构建及 3 项测试通过，独立安装消费程序编译运行通过；Windows 编译运行待验证。详见 [JSON 替代范围与结果](json-replacement-scope.md)。
- 已查阅 v3.12.0：MIT，C++11 起，单头文件集成，无必需的其他库依赖，适用于 C++20。
- 本项目已有对象构造、字段检查和序列化需求；DOM 风格接口比只提供高速解析能力的方案更贴合当前代码。
- 默认数字模型与 Qt 不同。实现已将整数和浮点数统一映射到 schema 的 `Number`，并覆盖大整数、非法 UTF-8、重复键、解析深度及异常边界；异常不会穿过 COM 或 Objective-C 回调。
- 消息上限继续按统一序列化后的字节数计算，同时保留进入 Bridge 的原始字节上限检查。
- 当前选择直接公开 nlohmann/json 别名，并接受源码/ABI 变更；使用方必须匹配 3.12.0、影响类型的宏、编译器、标准库和 Windows CRT，重新构建。安装包提供该公开依赖，不承诺稳定 DLL ABI。

### 2. Ada URL：C++20 下核心推荐

- 替代 QUrl 的解析、组件操作、编码及网络 URL 规范化能力。
- 已查阅 v3.2.7：C++20，MIT / Apache-2.0 双许可；上游说明基础库自包含，支持 WHATWG URL 和相关 IDNA 处理。
- 该版本可关闭测试、基准、工具和可选 simdutf，避免为了本项目 URL 处理引入额外开发依赖。正式采用前应锁定受维护版本和校验值，而非追踪主分支。
- **不等于逐项复刻 QUrl。** QUrl、WHATWG 和项目策略存在语义差异，需要项目包装层及回归用例。
- 已核验该版本 url::get_origin() 源码：一般非特殊 scheme 返回 `null`，包括本项目的 `app`。内部 `app://<application-id>` 身份必须从受验证的 scheme、host 等组件构造，不能拿这个 `null` 做应用隔离键。
- IDNA 支持主要解决网络域名；自定义 scheme 的 opaque host 不应被假定具有相同的 IDNA/大小写行为。Unicode 应用 ID、百分号编码、Qt 原有规范化必须单独核验。
- 网络 URL 应测试默认端口、IPv6、凭据、IDN、空 query/fragment 和畸形输入。策略不能因解析器容错或移除默认端口而意外扩大授权。
- 资源路径须规定解码次数和分隔符语义，覆盖 `%2e%2e`、`%2f`、`%5c`、双重编码及 NUL。URL 规范化不能替代文件系统根目录检查。

### 3. utf8proc：保留 Unicode 校验的优先候选

- 2026-10-26 已完成 `Application.cpp` 应用 ID 校验的 utf8proc 替代；子模块锁定 2.12.0，私有静态链接。macOS 验证 preset 构建成功，3 项测试通过。详见 [utf8proc 替代范围调研](utf8proc-replacement-scope.md)，本节以该补充结论为准。
- 提供 UTF-8 解码与 Unicode 分类，适合替代当前应用 ID 的 Unicode 字母/数字判断。
- 上游提供字母和数字的分类接口；代码使用 MIT 许可，另含 Unicode 数据许可，应随锁定版本核对并分发声明。
- Ada 自带域名处理不等于提供通用字符分类接口，不能据此省掉这项需求。
- 建议采用 utf8proc 按 Unicode 码点分类的新规则，接受字母、数字及连字符，不自动规范化或折叠大小写；补充平面字母/数字和 Unicode 表更新造成的接受范围变化记入文档，不复刻 Qt 的代理项遍历限制。
- 也可用两平台原生 Unicode 分类，但分类表版本与实现差异更难统一。若追求跨平台一致，优先采用同一版本 utf8proc。

### 4. MIME：固定扩展名 helper 已采用，libmagic 暂不引入

- 当前三处 Qt MIME 查询已统一替换为项目内部固定扩展名 helper；调用方显式传入的 MIME 仍优先。
- helper 覆盖 HTML、CSS、JavaScript、JSON、XML、SVG、常见图片、字体、音视频、WASM 和常见下载格式；未知扩展名回退到 `application/octet-stream`。
- 该行为与当前代码实际需求一致：项目只使用 MIME 字符串构造 WebView 响应，不要求识别任意未知二进制格式。
- libmagic 仍可作为未来“无扩展名内容检测”的候选，但当前不添加子模块、数据库或部署依赖。

### 可选项及未选方案

| 候选 | 何时需要 | 本项目建议 |
| --- | --- | --- |
| libuv | 后续有独立 IPC、TCP/UDP、子进程或大量后台 I/O | MIT；当前去 Qt 不必引入，不能替代 STA UI 消息循环 |
| libevent | 需要网络连接管理、bufferevent 或其 HTTP 能力 | 当前无对应核心需求，不引入 |
| spdlog | 宿主需要统一日志后端或格式化体系 | C++11 起，可编译或头文件集成；不是库内日志分类回调的必要条件；还须锁定 fmt 集成方式 |
| Boost.URL | 希望保留 C++11/17、使用 RFC 3986 模型 | Boost 许可；可行候选，但不能把 RFC 解析当作浏览器 WHATWG/IDNA 的完整替代，也应核查 Boost 传递依赖 |
| uriparser | 更偏好小型 C URI 解析接口 | BSD-3-Clause，RFC 3986；仍需补齐 IRI/IDNA、编码和项目来源策略 |
| fmt | 工具链不能提供项目实际需要的 std::format 功能 | 可选；C++20 标准获准不代表所有标准库特性都已在现有工具链可用 |
| 测试框架/HTTP 库 | 希望统一测试断言和临时 HTTP 服务 | 可选开发依赖；当前测试以 assert 和自建事件循环为主，没有必须替换的 QtTest 框架依赖 |

## Windows 后端的直接替代路径

重点文件 [WebView2View.cpp](../../src/platform/windows/WebView2View.cpp) 已经直接使用 COM、HWND、CreateFileW、ReadFile 和 IStream。应保留这些实现，替换周围的 Qt 宿主与基础类型，无需再引入一个 WebView 包装框架。

| 现有实现 | 原生替代方案 | 注意事项 |
| --- | --- | --- |
| NativeViewHost : QWidget、winId | 项目拥有的子 HWND 或受支持的宿主 HWND | 定义所有权与销毁顺序；处理 WM_SIZE、DPI、焦点、父窗口更换和关闭 |
| resizeEvent、applyHostState | 窗口过程/宿主通知 + GetClientRect + controller 的 Bounds/ParentWindow | 现有 GetClientRect 和 COM 设置代码可保留，不做机械重写 |
| QThread、QMetaObject::invokeMethod | UI 线程 ID + 受锁任务队列 + PostMessageW 唤醒 | 投递失败、窗口销毁、任务取消和队列释放都要有结果；避免把未管理裸指针塞入消息 |
| QPointer、PendingAction | weak_ptr 生命周期状态、代际标识、一次完成保护 | HWND 可被复用，IsWindow 检查不能替代所有权保护 |
| QString 的 COM 转换 | UTF-8 公共文本 + simdutf UTF-8/UTF-16 转码 | 已接入 v9.3.2 并私有静态链接，Windows 转码已替换；macOS 验证构建及 3 项测试通过，Windows 编译运行待验证；严格拒绝非法输入，文件路径使用原生 UTF-16，详见 [专项调研](simdutf-replacement-scope.md) |
| QUuid 文档/资源令牌 | 项目内部 128-bit 安全随机令牌；Windows 使用 `BCryptGenRandom`，macOS 使用 `SecRandomCopyBytes`，统一十六进制编码 | C++20 没有标准 UUID API；不引入 UUID 三方库，不使用时间、计数器或 `std::random_device` 作为安全令牌来源 |
| QTimer 清理重试 | 消息循环定时器或可取消的原生定时调度 | 回调不能访问已销毁状态；到期语义与现有时间口径需明确 |
| QIODevice/QFile 与 FileRangeStream 的衔接 | 项目 ByteStream/文件响应描述 + 原生文件范围流 | 保留租约、偏移、短读、范围限制；不能换成全文件读入内存 |
| QRegularExpression 解析 Range | 小型语法解析 + from_chars | 保留支持的 Range 形式、溢出检查及 416 响应，不需要通用正则库 |

WebView2 官方要求控件位于具有消息泵的 STA UI 线程，调用与回调也在该线程。去掉 QApplication 后必须由宿主或示例运行 Win32 消息循环。libuv 即使采用 IOCP，也不会自动满足这一契约。资源流还有后台读取场景，应按 WebView2 的流线程要求验证 IStream，而不是假定所有读取都在 UI 线程。

macOS 对应使用 NSApplication/NSRunLoop、NSView、主队列和安全随机源。公共层可约定宿主负责 UI 调度，但默认独立示例必须提供完整原生实现，不能把原来由 Qt 完成的工作留成空回调。

## 公共接口与保留的契约

建议采用 UTF-8 字符串、固定宽度整数、标准容器、项目 URL 值类型及独立的原生视图宿主描述。URL 解析、JSON、文件流和平台类型的实现细节尽量留在内部。C++20 不自动解决共享库 ABI：使用方仍需匹配编译器、标准库和 Windows CRT，或另行设计稳定 C 边界；本次不扩张到语言绑定设计。

关闭、导航切换和会话销毁仍须撤销文档令牌、资源及待处理请求；方向 schema、来源限制、消息大小、弹窗移交、下载/选文件完成一次等语义不能因移除 Qt 而丢失。

文件操作不能只用 std::filesystem::canonical 加字符串前缀判断代替现有安全逻辑。需保留 Windows reparse point、硬链接检查，覆盖目录边界和并发替换。快照需以临时文件成功写入后再发布，写入失败不注册资源，读者持有租约时保持容量统计，删除失败继续重试。

日志组件继续提供现有六类事件和宿主接管能力；默认不新增库自行管理日志文件的职责。原生 tabs、窗口按钮及示例测试也属于完整迁移范围，仅关闭 samples/testing 不构成仓库完全脱离 Qt。

## 建议实施与验收顺序

1. 冻结现有行为用例，锁定 C++20、候选库版本和依赖来源。Windows 分别确认 MSVC v143、AMD64/ARM64 和 CRT，macOS 确认 Apple Clang/libc++、架构和部署目标。不能只看库宣称支持 C++20。
2. 在独立验证目录做 Ada、JSON、Unicode 和 MIME 的小规模验证。优先验证 `app://` 隔离、Unicode ID、URL 编解码、网页资源 MIME，再决定保留候选库；不提前运行长时间完整实验。
3. 落实公共类型、文件/日志组件和原生宿主契约，再迁移 Windows 与 macOS 后端。迁移结束删除被完全替代的 Qt 实现及过渡分支，不保留双份长期逻辑。
4. 迁移示例、测试消息泵和临时 HTTP 服务。验证页面桥接、导航/关闭竞态、弹窗、会话销毁、资源 Range/撤销/容量/删除重试，以及原生日志接管。
5. 更新 CMake、预设、导出的包配置和开发/发布指南，核实安装包与实际运行时依赖均不含 Qt；同时用独立使用方构建验证安装后的 API。

验收应同时包括：源码与导出接口无 Qt 类型；所有既有构建目标有可用替代；两平台真实桌面测试通过；发布依赖检查通过；不存在以 ASCII 限制、缩小 MIME 类型集或关闭测试来掩盖未覆盖场景的情况。

JSON、URL、Unicode 转码和应用 ID 校验的 macOS 验证已经完成；Windows 后端仍需在 Windows MSVC preset 上验证。首个剩余验证重点是 **MIME 方案在 MSVC ARM64 上的可部署性**，以及 Qt 文件、事件循环和 UI 边界的逐项迁移。

## Qt5-base 剩余范围

Qt JSON、URL、Unicode 分类、Windows UTF-8/UTF-16 转换、公共字符串/容器和 MIME 固定扩展名 helper 已完成替代。Qt 仍未从项目中完全移除，当前实际范围如下：

| 范围 | 当前代码位置 | 当前状态与替代方向 |
| --- | --- | --- |
| 核心生产库的 Qt 链接 | [CMakeLists.txt](../../CMakeLists.txt) 第 22、78 行 | `find_package(Qt5 ...)` 和 `system_webview` 对 `Qt5::Widgets` 的 PUBLIC 链接仍存在。必须先让核心 target 不再导出 Qt，再由宿主适配器单独链接 Qt。 |
| Qt 宿主适配器 | [qt_host.h](../../include/system_webview/qt_host.h) | `QWidget`、`QThread`、`QMetaObject`、`QResizeEvent` 仍是 Qt 实现。它可以作为可选适配器保留，但不应进入核心 API 或核心链接。 |
| 诊断日志 | [Diagnostics.h](../../src/internal/Diagnostics.h)、[Diagnostics.cpp](../../src/webview/Diagnostics.cpp)、[WebViewState.cpp](../../src/webview/WebViewState.cpp) 及 Windows/macOS 后端 | `QLoggingCategory`、`QDebug` 和 Qt 字符串格式化仍在生产代码中。建议用标准类型定义结构化诊断事件并通过调用方注入的回调交付；库不做过滤、输出或持久化。 |
| UUID 和令牌 | [WebViewBridge.cpp](../../src/webview/WebViewBridge.cpp)、[WebResourceManager.cpp](../../src/webview/WebResourceManager.cpp)、[WkWebView.mm](../../src/platform/macos/WkWebView.mm)、[WebView2View.cpp](../../src/platform/windows/WebView2View.cpp) | `QUuid` 仍生成请求 ID、文档令牌和快照临时目录名。统一替换为项目内部 128-bit 安全随机令牌；请求匹配只要求唯一性，document/resource token 还要求不可预测性。 |
| 定时器和应用调度 | [WebResourceManager.cpp](../../src/webview/WebResourceManager.cpp) | `QCoreApplication`/`QTimer` 仍用于快照删除重试。需要平台 dispatcher 或宿主调度接口，并处理取消、销毁和重试截止时间。 |
| Range 正则和 Qt 字符串边界 | [WebResourceManager.cpp](../../src/webview/WebResourceManager.cpp)、[WkWebViewSession.mm](../../src/platform/macos/WkWebViewSession.mm)、平台后端 | `QRegularExpression` 解析 HTTP Range，平台文件中仍有 `QString`/Qt 日志。可用 `std::string_view`、`from_chars`、原生 NSString/UTF-16 边界和统一日志 facade 替代。 |
| 文件和流 | [WebResourceManager.cpp](../../src/webview/WebResourceManager.cpp)、测试及平台资源响应 | 生产路径已有标准文件/平台流替代方向，但测试仍大量使用 `QFile`、`QDir`、`QFileInfo`、`QTemporaryDir` 和 `QIODevice`。迁移必须保留快照、Range、租约、目录边界、链接/reparse point 和删除重试语义，详见 [文件与流替代范围调研](file-stream-replacement-scope.md)。 |
| Qt 示例和控件 | [samples/demo](../../samples/demo)、[windows_widget_demo](../../samples/windows_widget_demo) | Qt Widgets 示例仍使用 `QApplication`、`QMainWindow`、标签页、布局和按钮。GLFW3/SDL3 示例已提供其他宿主，但 Qt 示例可继续作为独立适配器示例。 |
| Qt 测试工具 | [tests](../../tests) | 测试仍依赖 `QApplication`、`QEventLoop`、`QTimer`、`QFile`、`QTemporaryDir`、`QTcpServer` 和 QWidget。需要平台事件循环、标准文件 API 和测试 HTTP 服务逐项替换，不能通过关闭测试目标宣称完成。 |
| 包配置和开发文档 | [system_webviewConfig.cmake.in](../../cmake/system_webviewConfig.cmake.in)、README 和诊断/架构文档 | 包配置仍导出 Qt5 查找依赖，README 仍以 Qt5 为主要构建前提，部分架构/诊断文档仍描述 Qt 日志和宿主。核心脱 Qt 后需同步更新。 |

`CMakeLists.txt` 第 106 行起的 `SYSTEM_WEBVIEW_BUILD_GLFW_HOST`、`add_subdirectory(3rdparty/glfw3)` 和 `system_webview_glfw_host` 只是 GLFW 宿主适配器的可选构建入口，不是剩余 Qt 内容。第 116 行起的 SDL3 逻辑同理；两者与 [system_webview_qt_host](../../CMakeLists.txt) 并列，均不应把宿主实现重新耦合进核心库。

建议的替代边界如下：

| Qt 能力 | 替代方案 | 结论 |
| --- | --- | --- |
| 文件、目录、路径和基础流 | C++20 `std::filesystem`、`std::ifstream`/自有 range reader、Windows 原生 UTF-16 API、macOS Foundation | 优先标准库和平台 API；安全检查与快照契约必须原样保留。 |
| 主线程调度和定时器 | Win32 dispatcher、Grand Central Dispatch/NSRunLoop、宿主回调 | WebView2 STA 与 WKWebView 主线程契约由平台实现保证；libuv/Asio 不能直接替代 Widgets 消息泵。 |
| 日志 | 自有日志 facade，宿主提供回调；必要时再评估 spdlog | 先迁移行为和分类，不为替换 Qt 强行引入新的全局日志后端。 |
| Widgets 和布局 | Win32/AppKit，或独立保留 Qt/GLFW3/SDL3 适配器 | 没有一个库能自动覆盖现有 UI 语义；按宿主实现分别维护。 |

当前结论：Qt 尚未完全替代。已替代的是数据格式、URL、Unicode、编码、公共基础类型和 MIME helper；未替代的是核心 Qt 链接、生产诊断/调度/UUID/正则边界、Qt 宿主适配器、Qt 示例、Qt 测试工具以及包配置和文档中的 Qt 声明。只有这些范围完成并通过 Windows/macOS 实机验证，才能移除 Qt5 的核心构建要求。

## Qt 日志替代方案

### 常见库的处理方式

C++ 库通常不会假定所有调用方使用同一个日志后端。常见边界有两种：库接受调用方注入的 logger/sink 接口，或通过回调把日志记录交给宿主；应用再把记录适配到自己的日志系统、终端、文件、遥测或测试捕获器。成熟库也常提供全局日志回调，但全局可变状态会增加多实例隔离和并发配置难度，不适合本库优先采用。

`std::string`、`std::string_view`、枚举、整数和 `std::format` 可以替代 Qt 的文本构造与类型依赖，但标准库没有日志分类、过滤器、sink 或持久化设施。因此建议本库定义窄小的结构化事件接口，而不是把 Qt 操作符流简单换成 `std::format` 后继续在库内打印。事件至少包含 severity、category、稳定 event code、session/view/navigation 关联 ID，以及经审查允许的字段；可读消息可以用 `std::format` 在生产端构造，也可以由宿主根据结构化字段格式化。

调用方通过创建 session/view 的配置或宿主回调注册接收函数。库发生事件时同步调用该接收函数；没有注册接收函数时直接丢弃事件。库不创建日志文件、不指定目录/轮转策略、不维护后台队列，也不依赖具体第三方日志库。接收器由调用方负责线程安全、级别过滤、格式化、写入和上传。

同步回调简单且不会引入隐式线程，但接收器运行在事件发生线程，不能阻塞 UI/WebView 回调线程。契约需说明回调线程、生命周期、并发可能性和不允许抛出异常；库应捕获并吞掉接收器异常，避免日志代码破坏 WebView 回调。若未来需要异步处理，应由调用方接收后自行排队。若希望提供轻量接口，可允许调用方设置最小级别，但过滤仍属于调用方策略；库本身只避免不必要的低级别消息构造。

### 当前生产代码涉及范围

当前生产实现有 44 个 Qt 分类日志调用点，分布在以下 7 个文件；分类声明和定义在 [Diagnostics.h](../../src/internal/Diagnostics.h) 与 [Diagnostics.cpp](../../src/webview/Diagnostics.cpp)：

| 文件 | 当前日志用途 |
| --- | --- |
| [WebViewState.cpp](../../src/webview/WebViewState.cpp) | session/view 创建、就绪、初始化失败、关闭、Bridge 校验拒绝、导航失败 |
| [WebViewBridge.cpp](../../src/webview/WebViewBridge.cpp) | Bridge JSON/传输失败、transport 不可用、消息校验拒绝 |
| [WebResourceManager.cpp](../../src/webview/WebResourceManager.cpp) | 资源发布/快照失败、资源请求拒绝、范围读取失败 |
| [WkWebView.mm](../../src/platform/macos/WkWebView.mm) | Bridge 拒绝、popup/权限/导航策略拒绝、Web 内容进程终止 |
| [WkWebViewSession.mm](../../src/platform/macos/WkWebViewSession.mm) | session 创建、就绪和关闭 |
| [WebView2View.cpp](../../src/platform/windows/WebView2View.cpp) | 权限/popup/下载/导航策略拒绝、Bridge 边界拒绝、进程故障 |
| [WebView2Session.cpp](../../src/platform/windows/WebView2Session.cpp) | WebView2 browser process 终止 |

目前有六个分类：`lifecycle`、`navigation`、`policy`、`bridge`、`resource`、`runtime`。Debug 调用默认关闭，Info、Warning、Critical 默认开启；依赖 Qt 的 `QT_LOGGING_RULES` 做宿主外部过滤。日志采用 `event=... key=value` 文本，诊断字段包含关联 ID、来源、状态、固定原因码和原生错误码。现有脱敏规则禁止记录 URL path/query、Bridge payload、request/document token 和本地绝对路径，应继续作为事件接口契约。

生产日志以外，[JsonMessageTests.cpp](../../tests/JsonMessageTests.cpp) 使用 `qInstallMessageHandler` 捕获文本，并用 `QLoggingCategory::setFilterRules` 改变过滤状态；替代时应改为注入测试接收器，检查 severity/category/event/字段、默认无接收器时不输出、敏感数据不进入事件，以及接收器异常不会逃出日志调用。`docs/guides/diagnostics.md` 和 [API 参考](../references/api-reference.md) 目前也将 Qt logging category、`QT_LOGGING_RULES` 和 `qInstallMessageHandler` 写为公开接入方式，实施替代时要同步更新。

推荐结论：由本库拥有稳定的分类名、severity、event code 和字段语义；由调用方拥有过滤规则、最终文本格式、日志后端和持久化。公开 API 使用标准 C++ 类型，不暴露 Qt 或 spdlog 类型。该方案更符合“库产生诊断、宿主决定怎么处理”，也允许调用方接入现有日志体系而不增加必需三方库。

本方案已实施：`DiagnosticEvent`、`DiagnosticHandler` 和 `WebViewSessionOptions::diagnosticHandler` 已加入公共接口，生产日志调用已改为标准 C++ `DiagnosticStream`。核心测试已改用注入接收器，并验证事件分类、事件码、脱敏字段和关闭后的行为。

## UUID 替代方案

C++20 标准库没有 UUID 类型或密码学随机字节接口。本项目的三个用途都不需要完整 UUID 语义、版本字段或 UUID 解析：Bridge request ID 只需要高概率唯一，document token 和资源 token 还会参与页面/资源边界校验，因此必须不可预测，快照目录名也应避免被猜测。

最简方案是项目内部提供一个 `randomToken128()`：在 Windows 调用 `BCryptGenRandom`，在 macOS 调用 `SecRandomCopyBytes`，读取 16 个随机字节，再编码为 32 个小写十六进制字符。该格式不带连字符，但现有代码只把值当作不透明字符串，不依赖 UUID version/variant 字段，行为足够稳定。失败时应返回明确错误并终止本次生成，不能回退到时间、进程 ID、地址、递增计数器或普通伪随机数。

不建议使用 `std::random_device` 作为唯一实现。标准只保证它是非确定性随机源的抽象，具体实现可能退化为伪随机设备，不能为 document token 或 resource token 提供跨平台的密码学保证。Boost.UUID、libuuid、stduuid 等库可以生成 UUID 文本，但会增加依赖、许可/安装配置和格式适配；其中 UUID 生成是否使用系统安全随机源仍需逐平台核验，不能仅凭库名判定安全性。

因此推荐优先级为：平台安全随机源 + 项目内部编码；只有未来需要对外暴露 UUID version/variant、解析或跨系统互操作时，才单独引入 UUID 库。该方案不新增 3rdparty 子模块，能直接移除当前 `QUuid` 头文件和 Qt Core 对这些调用点的依赖。

该方案已实施于 [RandomToken.cpp](../../src/internal/RandomToken.cpp)：Windows 链接 `bcrypt`，macOS 链接 `Security.framework`，生成失败时返回空值并由请求、快照或文档初始化路径明确失败。`WebViewBridge`、`WebResourceManager`、`WkWebView` 和 `WebView2View` 均已改用该接口；核心测试验证 32 位小写十六进制格式和连续生成不重复。macOS `macos-appleclang-debug` preset 下主库和 `webview_core_tests` 构建通过，核心测试通过。

## 外部资料

以下为本次查阅的上游依据；主分支资料可能继续变化，正式接入需使用锁定版本重新核验。

- [nlohmann/json v3.12.0：集成、语言要求与许可](https://github.com/nlohmann/json/blob/v3.12.0/README.md)
- [Ada v3.2.7：C++20、IDNA、自包含及许可](https://github.com/ada-url/ada/blob/v3.2.7/README.md)
- [Ada v3.2.7 构建选项](https://github.com/ada-url/ada/blob/v3.2.7/CMakeLists.txt)
- [Ada v3.2.7 URL 实现：get_origin 和 opaque host](https://github.com/ada-url/ada/blob/v3.2.7/src/url.cpp)
- [WebView2 官方线程模型](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/threading-model)
- [utf8proc：功能、代码及 Unicode 数据许可](https://github.com/JuliaStrings/utf8proc/blob/master/README.md)
- [utf8proc Unicode 分类接口](https://github.com/JuliaStrings/utf8proc/blob/master/utf8proc.h)
- [file/libmagic：文件识别与数据库](https://github.com/file/file/blob/master/README.md)
- [file/libmagic 许可](https://github.com/file/file/blob/master/COPYING)
- [file/libmagic 可选构建依赖](https://github.com/file/file/blob/master/configure.ac)
- [libuv 能力与许可](https://github.com/libuv/libuv/blob/v1.x/README.md)
- [spdlog 集成与格式化依赖](https://github.com/gabime/spdlog/blob/v1.x/README.md)
- [Boost.URL 的 RFC 模型与语言要求](https://github.com/boostorg/url/blob/develop/README.adoc)
- [uriparser 的 RFC 模型与许可](https://github.com/uriparser/uriparser/blob/master/README.md)

## 版本修改记录

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 2026-10-25 21:31 CST | 完成 C++20 去 Qt 选型、Windows 后端映射、兼容性边界及验证建议。 |
| v2 | 2026-10-26 10:55 CST | 补充 utf8proc 源码审计结论、实际锁定版本和直接替代范围。 |
| v3 | 2026-10-26 10:58 CST | 同步 utf8proc 校验替代完成及 macOS 验证结果。 |
| v4 | 2026-10-26 11:04 CST | 将 Windows 编码转换候选更新为已选定的 simdutf，并链接专项范围审计。 |
| v5 | 2026-10-26 11:10 CST | 同步 simdutf 已接入、Windows 转码调用已替换及 macOS 验证结果。 |
| v6 | 2026-10-26 11:29 CST | 记录 nlohmann/json 已替代 Qt JSON，公开依赖及验证结果。 |
| v7 | 2026-10-26 11:32 CST | 补充 Qt JSON 完成替代后的剩余 Qt 范围和三方库建议。 |
| v8 | 2026-10-26 11:36 CST | 补充 libmagic MIME 调研及其与 QMimeDatabase 的替代边界。 |
| v9 | 2026-10-26 11:40 CST | 补充 WebView2 资源响应中的 MIME 作用及 libmagic 的边界。 |
| v10 | 2026-10-26 12:48 CST | 同步固定 MIME 扩展名 helper 已实施，明确暂不引入 libmagic。 |
| v11 | 2026-10-26 12:55 CST | 补充文件与流 Qt 用途的专项替代方案及标准库/平台 API 边界。 |
| v12 | 2026-10-26 15:39 CST | 按当前源码和 CMake 实际依赖重新盘点 Qt 生产库、适配器、示例、测试和包配置的剩余范围。 |
| v13 | 2026-10-26 15:39 CST | 调研 Qt 日志的标准库替代边界、调用方接收模式，并盘点当前生产日志调用点。 |
| v14 | 2026-10-26 16:05 CST | 实施标准 C++ 诊断事件回调，移除生产日志路径的 Qt 日志依赖，并同步测试与接入文档。 |
| v15 | 2026-10-26 16:20 CST | 确定 QUuid 的替代方案，采用平台安全随机源和项目内部令牌编码，不引入 UUID 三方库。 |
| v16 | 2026-10-26 16:35 CST | 完成 QUuid 替换，接入平台安全随机源、统一令牌编码和核心测试。 |
