# Qt5 Base 完整替代选型调研

> 最后修改时间：2026-10-26 12:55 CST
> 文档版本：v11
> 修改说明：补充文件与流 Qt 用途的专项替代方案及标准库/平台 API 边界。

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
| QUuid 文档/资源令牌 | BCryptGenRandom，或 macOS 对应安全随机源 | 保留令牌不可预测性；std::random_device 不作为可移植的密码学保证 |
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

Qt JSON 已由 nlohmann/json 完整替代，simdutf、utf8proc 和 Ada 也已分别承担 Windows 转码、Unicode 分类和 URL 解析职责。当前剩余 Qt 主要是 UI 与平台适配：`QApplication`/`QWidget`/布局、事件循环和定时器、`QFile`/`QDir`/`QFileInfo`、`QIODevice`、Qt 日志以及少量 QString 的原生边界转换。文件和流的具体调用、Range/快照契约及迁移顺序见 [文件与流替代范围调研](file-stream-replacement-scope.md)。

建议的辅助库和边界如下：

| 剩余 Qt 范围 | 可评估的三方库或原生方案 | 结论 |
| --- | --- | --- |
| 文件、目录和路径 | C++20 `std::filesystem`，Windows UTF-16 API，macOS Foundation | 优先标准库和平台 API；保留 symlink/reparse point、硬链接和并发替换检查 |
| 流和资源文件 | `std::ifstream`/`std::fstream`、项目自有 range reader、Windows `IStream` | 先移除公共 `QIODevice` ABI，再迁移快照、Range、租约和平台响应；详见 [文件与流报告](file-stream-replacement-scope.md) |
| MIME 检测 | 项目内部固定扩展名 helper；未来可选 libmagic | 已完成显式 MIME、固定扩展名表和 `application/octet-stream` 回退；当前不需要第三方 MIME 库 |
| 主线程调度、定时器和异步 I/O | Windows dispatcher、Grand Central Dispatch、libuv 或 Boost.Asio | WebView2 STA 和 WKWebView 主线程仍依赖平台循环；libuv/Asio 不能直接替代 Qt Widgets 事件循环 |
| 日志 | spdlog 或自有日志 facade | 先保持 QLoggingCategory 行为，单独迁移日志分类和过滤 |
| Widgets、布局、跨平台窗口 | Win32/Windows App SDK、AppKit，或保留 Qt Widgets | 没有一个轻量三方库能无损替代当前 UI；这是最大剩余范围 |

这些库只解决各自边界，不能为了“完全替代 Qt5-base”把 libuv、Boost.Asio、libmagic 或 spdlog 混合成新的通用层。下一阶段应按文件系统、MIME、日志、调度、Widgets 的顺序分别调研和验证。libmagic 的具体调查和验证矩阵见 [专项报告](libmagic-replacement-scope.md)。

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
