# 在 Android 模拟器上运行示例

> 最后修改时间：2026-11-06 13:18 CST
> 文档版本：v1
> 修改说明：记录 Android demo 的构建、运行方式、后端行为和当前限制。

本文说明如何构建并运行 `samples/android_demo`，以及该示例当前覆盖的公共契约范围。示例已经在本机 Android 模拟器上完成页面加载、资源拦截、双向 bridge 和窗口事件验证；尚未覆盖的能力在“当前限制”中列出。

## 前置条件

| 组件 | 本次验证使用的版本 | 说明 |
| --- | --- | --- |
| Android SDK Platform | 36 | 示例使用 `compileSdk = 36`、`targetSdk = 36` |
| Android NDK | 28.2.13676358 | 由 `app/build.gradle.kts` 的 `ndkVersion` 指定 |
| CMake | 3.31.6 | 仓库要求 3.24 及以上；SDK 自带的 3.22.1 不满足 |
| JDK | 17 及以上 | 本次使用 Android Studio 自带 JBR |
| Gradle | 9.6.0 | 通过仓库内的 Gradle Wrapper 获取 |
| AndroidX WebKit | 1.18.0 | 提供 `WebMessageListener`、document-start 注入等能力 |
| 系统 WebView | 133.0.6943.137 | 模拟器 API 36 镜像自带的版本 |

SDK 组件可以用 Android Studio 的 SDK Manager 安装，也可以用 `sdkmanager` 安装：

```powershell
sdkmanager "cmake;3.31.6" "ndk;28.2.13676358" "platforms;android-36"
```

另外需要 `ANDROID_HOME` 指向 SDK，或在 `samples/android_demo/local.properties` 中写入 `sdk.dir`。该文件只用于本机，已被示例目录的 `.gitignore` 排除。

## 构建与运行

```powershell
cd samples/android_demo
.\gradlew.bat assembleDebug
.\gradlew.bat installDebug
```

也可以直接用 Android Studio 打开 `samples/android_demo` 目录，选择模拟器后运行 `app`。应用 ID 为 `io.github.systemwebview.demo`，启动后即可看到与桌面示例相同的页面。

模拟器可以这样启动：

```powershell
$env:ANDROID_HOME\emulator\emulator.exe -avd <AVD 名称>
```

## 示例结构

| 位置 | 职责 |
| --- | --- |
| `app/src/main/cpp/CMakeLists.txt` | 以子目录方式引入仓库根 CMake，关闭示例、测试和桌面宿主 |
| `app/src/main/cpp/demo_bridge.cpp` | 示例宿主：创建 session、注册应用、安装 policy 与 bridge 处理函数 |
| `app/src/main/java/io/github/systemwebview/SystemWebViewHostView.java` | WebView 容器与宿主适配：创建 WebView、转发回调、安装 bridge 脚本 |
| `app/src/main/java/io/github/systemwebview/SystemWebViewJni.java` | JNI 入口、主线程判断、WebView Feature 查询、网站数据清理 |
| `app/src/main/java/io/github/systemwebview/demo/MainActivity.java` | 原生工具栏、状态栏、返回与刷新，以及窗口事件处理 |
| `app/src/main/java/io/github/systemwebview/demo/AssetExtractor.java` | 首次启动把 APK assets 解包到 `filesDir` |
| `build.gradle.kts` 中的 `copyDemoAssets` | 构建时从 `samples/demo/resources` 复制页面，避免维护第二份页面 |

页面的唯一来源是 `samples/demo/resources/demo.html`，构建时复制到生成目录并作为 assets 打包。

## 运行行为

1. 首次启动时 `AssetExtractor` 把 `assets/demo` 解包到 `filesDir/webview/demo`，并写入版本标记；应用更新后标记不匹配会重新解包。
2. 示例把该目录作为 `LocalBundle` 注册为应用 `demo`，入口文档为 `demo.html`。
3. 公共接口中的 URL 仍是 `app://demo/...`；Android 实际加载 `https://appassets.androidplatform.net/demo/...`。后端在边界做双向映射，policy、bridge 和资源校验都只看到 `app://demo`。
4. 页面资源由 `shouldInterceptRequest` 转发到 C++，调用现有的 `resolveMappedResource` 和 `WebResourceManager` 完成路径校验、MIME 判断和 CSP 注入。拦截发生在 WebView 的 IO 线程，Java 侧会把请求投递到主线程并等待，保证 C++ 状态只被主线程访问。
5. 页面到原生：document-start 脚本注入当前文档 token，页面通过 `window.systemWebViewNative.postMessage(JSON.stringify(...))` 发送；原生校验主框架、来源 origin、document token 和 JSON envelope 后交给 `WebViewBridge`。
6. 原生到页面：`evaluateJavascript` 调用 `window.__systemWebViewReceive(...)`。页面收到 `ack` 后会把状态文字改成 `Native received your message.`。
7. 页面标题栏的关闭按钮会通过 `window` 事件关闭 Activity；最小化会把任务移到后台；最大化会收到 `window-state` 回执。

## 当前限制

- 弹窗：示例关闭了多窗口，`window.open` 不会创建新页面，页面上的“打开示例页”没有可见效果。popup 接管需要额外的 `WebViewHost` 子容器。
- 文件选择、下载、相机、麦克风、定位、通知和剪贴板：`IWebViewSession::supports` 均返回 `false`，对应回调不会触发。
- 会话模式：示例使用 `SessionMode::Persistent`。临时会话需要系统 WebView 支持 `MULTI_PROFILE`，否则视图初始化会明确失败；`profilePath` 在 Android 上不参与存储位置决定。
- 已发布资源：`publishFile` 的响应会在内存中完整读取，适合小文件。大文件或媒体随机访问尚未实现 `Range` 流式响应。
- 线程模型：Android 后端要求调用发生在 Java 主线程；`WebViewHost::postToUi` 未被使用。非主线程创建 session 或视图会得到明确的初始化错误。
- ABI：示例当前只构建 `x86_64`，用于模拟器。真机需要把 `arm64-v8a` 加入 `abiFilters`。
- 宿主：Qt 5 Android 没有 QWidget，`qt_host.h` 不适用；示例使用原生 Android 视图提供工具栏和状态栏。

## 验证记录

2026-11-06 在 Windows 主机上完成：

| 项目 | 结果 |
| --- | --- |
| `gradlew.bat clean assembleDebug` | 成功 |
| 安装与启动（`Medium_Phone_API_36.0`，API 36，x86_64） | 成功 |
| 页面加载 | `Started → Committed → Finished`，最终状态显示 `已加载 app://demo/demo.html` |
| 资源拦截 | 主文档、内联脚本和样式均由 C++ 拦截返回，无控制台错误 |
| 页面到原生 | 日志出现 `Page message: Hello from System WebView` |
| 原生到页面 | 页面状态文字变为 `Native received your message.` |
| 窗口事件 | 点击页面关闭按钮后 Activity 结束并回到桌面 |
| Windows 回归 | 使用 MSVC v143 重新编译 `system_webview` 成功 |

## 相关文档

- [Android 支持调研](../reports/android-support-research.md)
- [架构与生命周期](../arch/architecture.md)
- [公开接口速查](../references/api-reference.md)

## 版本修改记录

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 2026-11-06 13:18 CST | 首次记录 Android 示例的构建、运行、后端行为、限制与验证结果。 |
