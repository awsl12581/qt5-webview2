# 窗口和控件宿主替代范围调研

> 最后修改时间：2026-10-26 14:20 CST
> 文档版本：v13
> 修改说明：移除内部 QtHostView，并为 popup 增加宿主适配器交接契约。

## 结论

当前项目的窗口宿主能力主要由 Qt Widgets 提供，不能由 C++20 标准库直接替代。Qt 的职责集中在四类：

1. 公共 API 中用 `QWidget*` 表示父控件，并返回 `QWidget*` 供布局使用。
2. Windows 用 QWidget 创建原生窗口、取得 `HWND`、接收尺寸变化，并把 `HWND` 交给 WebView2 controller。
3. macOS 用 QWidget 创建原生窗口、取得承载用的 `NSView`，接收尺寸/显示/布局事件，并把 `WKWebView` 放入该视图。
4. Qt 事件循环、线程判断和队列投递承载异步初始化、回调和延迟清理。

窗口和控件宿主的完整替代应采用“平台原生后端 + 编译期宿主适配实现”的结构：库只要求宿主提供一个可承载的原生窗口句柄、几何/可见性变化通知和主线程投递能力；Windows 使用 `HWND`、`GetClientRect`、`PostMessage` 或宿主调度器，macOS 使用 `NSView*`、`-[NSView addSubview:]`、`-[NSView setFrame:]` 和 Grand Central Dispatch。Qt、GLFW、SDL 和平台原生窗口分别在对应构建配置中编译，不做运行时插件注册或动态选择。

## 当前涉及范围

| 范围 | 当前位置 | Qt 作用 | 替代方向 |
| --- | --- | --- | --- |
| 公共创建接口 | [system_webview.h](../../include/system_webview/system_webview.h) | 旧接口直接接收 QWidget | 统一使用 `createWebView(WebViewHost)`，由 Qt、GLFW、SDL 或原生实现提供宿主 |
| Windows 承载控件 | [WebView2View.cpp](../../src/platform/windows/WebView2View.cpp) | `NativeViewHost : QWidget`、`WA_NativeWindow`、`winId()`、`resizeEvent()` | 使用宿主提供的 `HWND`；由适配层转发 resize、visibility、destroy；后端只操作 WebView2 controller |
| Windows 异步调度 | [WebView2View.cpp](../../src/platform/windows/WebView2View.cpp) | `QThread::currentThread()`、`QMetaObject::invokeMethod(..., QueuedConnection)` | 抽象 `postToUi(std::function<void()>)`；Win32 可用窗口消息/线程消息，或由上层事件循环注入 |
| macOS 承载控件 | [WkWebView.mm](../../src/platform/macos/WkWebView.mm) | `NativeViewHost : QWidget`、`winId()`、尺寸/显示/布局事件 | 使用宿主 `NSView*`；后端直接管理 `WKWebView` 的父子关系、frame 和可见性 |
| macOS 异步调度 | [WkWebView.mm](../../src/platform/macos/WkWebView.mm) | `QMetaObject`、`QPointer`、Qt 事件对象 | 使用主线程断言、`dispatch_async(dispatch_get_main_queue(), ...)` 和显式弱引用/生命周期状态 |
| Session 创建 | [WebView2Session.cpp](../../src/platform/windows/WebView2Session.cpp)、[WkWebViewSession.mm](../../src/platform/macos/WkWebViewSession.mm) | 接收 `QWidget*` 并创建平台 view | 接收宿主句柄或 `HostView`；不在 session 层创建 Qt 控件 |
| 示例 | [DemoWindow.cpp](../../samples/demo/DemoWindow.cpp)、[windows_widget_demo/main.cpp](../../samples/windows_widget_demo/main.cpp) | QMainWindow、QTabWidget、布局、按钮和窗口控制 | 示例可保留 Qt 版本，并增加 GLFW/SDL 或原生宿主示例；核心库不依赖具体 GUI 工具包 |
| 测试 | [WebView2RuntimeProbe.cpp](../../tests/WebView2RuntimeProbe.cpp)、[WkViewTests.mm](../../tests/WkViewTests.mm) 等 | QApplication、QWidget、QTimer、QEventLoop | 测试 helper 创建原生宿主窗口并提供最小调度器；纯核心测试移除 GUI 初始化 |

## 可替代与不可替代部分

标准库可以替代字符串、容器、所有权和回调类型，但不能提供：

- 窗口或控件对象；
- 原生句柄到子控件的嵌入关系；
- resize、show/hide、destroy 等窗口生命周期通知；
- 主线程事件循环、消息泵和异步回调调度；
- 跨平台布局、焦点、输入法、无障碍和窗口装饰。

因此，`QWidget*` 不能简单改成 `void*` 后继续保留当前语义。需要明确句柄类型、所有权和生命周期，并将宿主事件通知作为接口契约的一部分。

## 推荐 API 方向

建议在公共 API 中引入不暴露具体 GUI 框架 ABI 的宿主描述，并使用平台宏直接确定句柄字段：

```cpp
#if defined(_WIN32)
struct WebViewHost {
    void* hwnd = nullptr;
    // bounds, visibility, resize and UI dispatch callbacks
};
#elif defined(__APPLE__)
struct WebViewHost {
    void* nsView = nullptr;
    // bounds, visibility, resize and UI dispatch callbacks
};
#else
#error "Unsupported system webview host platform"
#endif

```

实际代码按 `_WIN32`、`__APPLE__` 等平台宏编译对应字段和实现。Qt、GLFW、SDL 只作为构建时选定的宿主来源：Qt 从 QWidget 获取句柄和事件，GLFW 从 GLFW 窗口获取句柄并接入 GLFW 主循环，SDL 从 SDL 窗口获取句柄并接入 SDL 事件循环。它们不通过统一的运行时工厂或插件接口注册。GLFW 和 SDL 主要提供窗口与事件循环，不提供 Qt 等价的通用控件布局，因此控件树和布局仍由应用或其他 UI 框架负责。

实际实现时，`onResize` 不应只传无参数回调；宿主适配层应提供当前 client bounds、可见性和销毁状态。上面的代码只表示职责，不是已批准的最终 ABI。

推荐的生命周期顺序是：创建宿主控件或原生 view，构造 `WebViewHost`，创建 view，宿主完成布局后调用 attach；尺寸变化时更新 bounds；宿主销毁前调用 detach/close。库不拥有外部宿主句柄，也不负责销毁宿主窗口。

第一阶段已在 [HostView.h](../../src/internal/HostView.h) 实现纯框架无关的 `HostView` 接口和 `ExternalHostView` 实现：接口只处理原生句柄、可见几何、UI 投递和状态变化通知，不包含 Qt 类型。Qt 的 `WA_NativeWindow`、事件和线程投递全部位于独立的 [qt_host.h](../../include/system_webview/qt_host.h) 适配器。Windows 的 [WebView2View.cpp](../../src/platform/windows/WebView2View.cpp) 与 macOS 的 [WkWebView.mm](../../src/platform/macos/WkWebView.mm) 只持有 `HostView*`，不创建 Qt 宿主。

## 平台实现

### Windows

- `WebViewHost::hwnd` 表示有效的 `HWND`。
- WebView2 使用 `CreateCoreWebView2Controller(hwnd, ...)`，随后通过 `put_ParentWindow`、`put_Bounds` 和 `put_IsVisible` 同步宿主状态。
- resize 和销毁通知由 Win32 子类化、宿主窗口过程，或上层 GUI 框架适配器转发。
- UI 调度抽象为 `postToUi`；Windows 原生实现可使用 `PostMessage` 配合隐藏消息窗口，宿主框架也可直接注入自己的调度器。

### macOS

- `WebViewHost::nsView` 表示有效的 `NSView*`，调用必须发生在主线程。
- `WKWebView` 作为子 view 加入宿主并同步 `frame`、`autoresizingMask` 和可见性。
- 尺寸、显示和销毁通知由宿主 `NSView` 子类或适配器转发。
- UI 调度使用主队列；对象生命周期由显式状态和弱引用控制，避免依赖 `QPointer`。

## 方案比较

| 方案 | 结果 | 判断 |
| --- | --- | --- |
| 直接用 C++20 标准库替换 QWidget | 没有窗口、句柄和事件循环能力 | 不可行 |
| 引入 SDL/GLFW | 提供窗口、原生句柄和事件循环，但不提供通用控件布局；需要适配器补齐嵌入、尺寸和销毁通知 | 可作为宿主适配器实现，不作为 QWidget 等价替代 |
| 引入 wxWidgets/GTK | 可以提供控件和事件循环，但增加新的 GUI ABI、构建和平台适配负担 | 不建议作为核心库依赖 |
| 使用平台原生 API + `HostView` | 覆盖 WebView2/WKWebView 所需能力，核心 ABI 与 GUI 框架解耦 | 推荐的抽象基础 |
| Qt/GLFW/SDL 宿主实现 | 通过编译期宏和构建配置连接各自的窗口句柄、事件循环和生命周期通知 | 按应用需求编译选择，不做运行时切换 |

## 实施边界

整体迁移分为五个阶段：

| 阶段 | 目标 | 状态 |
| --- | --- | --- |
| 1. 宿主职责审计 | 找出 QWidget、原生句柄、几何通知和 UI 调度的实际使用点 | 已完成 |
| 2. 内部宿主边界 | 建立不包含 Qt 类型的 `HostView` 接口和 `ExternalHostView` 实现，平台后端依赖接口 | 已完成 |
| 3. 公共创建接口迁移 | 用平台宏选择的宿主描述替代 QWidget 创建接口 | 已完成 |
| 4. 非 Qt 宿主实现 | 增加 Win32/AppKit 原生实现，并按需增加 GLFW/SDL 构建实现 | 已完成（GLFW/SDL 入口已加入，原生事件回调仍需增强） |
| 5. Qt 收口与验证 | 将 Qt 适配器移到独立目标，迁移示例/测试并评估移除 `Qt5::Widgets` | 已完成 |

具体实施顺序如下：

1. 先定义平台宏下的宿主句柄字段、宿主几何信息和 `postToUi` 的生命周期契约。
2. 将 `IWebView`/`IWebViewSession` 的 `QWidget*` 接口改为 `WebViewHost` 接口，核心和平台后端不再包含 Qt Widgets 宿主实现。
3. Windows、macOS 分别实现原生宿主适配，并保持当前 attach/detach、resize、close 和异步初始化语义。
4. 将 Qt 支持移到独立宿主实现；Qt 只作为第一种编译实现，应用通过 `QtHostWidget` 构造宿主。
5. 按相同契约增加 GLFW、SDL 或平台原生实现，通过平台宏和 CMake 选项在编译期选择，不要求它们提供通用控件系统。
6. 将测试拆为核心生命周期测试、Windows HWND 宿主测试和 macOS NSView 宿主测试；测试事件循环由各适配器 helper 提供。
7. 最后再评估是否移除 `Qt5::Widgets`，保留 Qt 网络或测试组件的范围应单独审计。

第三阶段已在 [system_webview.h](../../include/system_webview/system_webview.h) 增加 `WebViewHost` 和 `createWebView(WebViewHost)`，并删除 QWidget 创建接口。宿主描述包含原生句柄、初始几何/可见性以及 UI 投递和变化观察回调；[HostView.h](../../src/internal/HostView.h) 提供 `ExternalHostView` 将其接入平台后端。

第四阶段新增 [glfw_host.h](../../include/system_webview/glfw_host.h) 和 [sdl3_host.h](../../include/system_webview/sdl3_host.h)，分别从 GLFW 3 和 SDL 3 窗口生成 `WebViewHost`。CMake 选项 `SYSTEM_WEBVIEW_BUILD_GLFW_HOST`、`SYSTEM_WEBVIEW_BUILD_SDL3_HOST` 默认关闭；启用后分别构建 `system_webview_glfw_host`、`system_webview_sdl3_host` 接口目标。Qt、GLFW、SDL 三种宿主实现共用同一公共创建接口，不通过运行时插件切换。

第五阶段新增 [qt_host.h](../../include/system_webview/qt_host.h) 和 `system_webview_qt_host` 接口目标。Qt 应用创建 `QtHostWidget` 后，将 `webViewHost()` 传给 `createWebView(WebViewHost)`。popup 通过 `WebViewHostCallbacks::createPopupHost` 由应用提供子宿主，平台后端不再创建 Qt 宿主。`createWebView(QWidget*)` 入口已删除，三种宿主适配器统一使用 `WebViewHost`，并分别拥有独立公共入口和构建目标。

## 风险与验收重点

- 宿主句柄在 attach 前无效、在 close 后继续使用，必须返回明确失败状态或忽略操作。
- resize 与 WebView2 controller/WKWebView frame 更新必须在平台 UI 线程执行。
- 异步回调不能捕获已销毁的宿主；关闭时要取消待处理回调。
- Windows DPI、macOS backing scale 和坐标系转换不能由 `void*` 句柄隐式解决，应在宿主适配层明确处理。
- 公共头文件不得导出 Qt 类型；Qt 适配器的 ABI 和核心 ABI 应分开验证。

## 版本修改记录

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 2026-10-26 13:01 CST | 首次调研窗口、控件宿主和 UI 调度的 Qt 使用范围，提出平台原生宿主适配方案。 |
| v2 | 2026-10-26 13:10 CST | 完成 Qt 宿主适配器第一阶段实现，并记录 Windows/macOS 后端接入状态。 |
| v3 | 2026-10-26 13:18 CST | 明确框架无关适配器目标，补充 Qt、GLFW、SDL 和平台原生实现的职责边界。 |
| v4 | 2026-10-26 13:25 CST | 改为平台宏编译期选择宿主实现，移除运行时枚举和插件式设计。 |
| v5 | 2026-10-26 13:32 CST | 落地 `HostView` 编译期宿主边界，接入 Windows/macOS 后端并保留现有 Qt API 兼容层。 |
| v6 | 2026-10-26 13:42 CST | 拆分框架无关 `HostView` 接口与 `QtHostView` 实现，平台后端改为依赖接口。 |
| v7 | 2026-10-26 13:50 CST | 明确五阶段迁移计划，标记公共创建接口迁移为当前阶段。 |
| v8 | 2026-10-26 14:05 CST | 完成 `WebViewHost` 公共创建接口和非 Qt 宿主接入路径，进入原生/GLFW/SDL 实现阶段。 |
| v9 | 2026-10-26 14:20 CST | 加入 GLFW 3、SDL 3 子模块及 Qt/GLFW/SDL 三种宿主实现入口和可选 CMake 目标。 |
| v10 | 2026-10-26 14:40 CST | 增加 Qt 独立宿主适配头和 CMake 目标，完成三种宿主入口的收口。 |
| v11 | 2026-10-26 15:00 CST | 删除 `QWidget*` 创建入口，统一改用 `WebViewHost`，移除兼容迁移表述。 |
| v12 | 2026-10-26 14:10 CST | 修正阶段 5 收尾文案，明确 v2 不提供 QWidget 兼容入口、deprecated alias 或迁移 wrapper。 |
| v13 | 2026-10-26 14:20 CST | 删除 `QtHostView` 和 HostView 中的 Qt 类型，popup 改由宿主适配器提供 `WebViewHost`。 |
