# nlohmann/json 替代 Qt JSON 的范围

> 最后修改时间：2026-10-26 11:32 CST
> 文档版本：v2
> 修改说明：完成 nlohmann/json 替代，记录构建、测试、安装包验证及剩余 Qt 范围。

## 结论与基线

现有 [nlohmann/json 子模块](../../3rdparty/json/)可以覆盖当前全部 Qt JSON 用途，无需再引入其他 JSON 库。迁移应一次覆盖公开类型、消息解析与序列化、schema、两平台后端、测试和 demo；不保留 QJson 兼容层。页面的 event/request/response 协议保持不变，C++ 调用方需要更新类型并重新编译。

审计基线为 `974fa2037c72b70f9642786bc9ac95ed90c2fd98`；子模块锁定 **v3.12.0**，提交 `55f93686c01528224f448c19128836e7df245f72`。该库采用 MIT 许可、仅头文件实现，支持本项目 C++20。实施已完成，结果和限制见下文。

## 直接覆盖范围

搜索自有 `src/`、`include/`、`tests/`、`samples/` 中的 `QJson`，共涉及 **12 个文件**。下表包含所有直接调用；构建和文档另计。

| 位置 | 当前职责 | 替代内容 |
| --- | --- | --- |
| [system_webview.h](../../include/system_webview/system_webview.h) | 公开消息 payload、4 类 Bridge 回调、发送方法、schema 字段类型 | `QJsonObject` 改为项目 `Json` 别名；`QJsonValue::Type` 改为项目 `JsonType` 枚举，移除 Qt JSON 头文件 |
| [JsonMessage.h](../../src/webview/JsonMessage.h)、[JsonMessage.cpp](../../src/webview/JsonMessage.cpp) | 按长度解析对象、生成紧凑 JSON | 集中使用 `parse` / `dump`，检查对象根节点，提供可处理的失败结果 |
| [WebViewBridge.cpp](../../src/webview/WebViewBridge.cpp) | 六字段 envelope 编解码、事件/请求/回复、pending 收尾 | 替换对象构造和字段读取，统一序列化入口；保留 requestId/type 匹配、一次回复和关闭取消 |
| [WebViewPolicy.cpp](../../src/webview/WebViewPolicy.cpp) | 消息字节上限、字段存在性/类型、禁止多余字段 | 使用对象查找和类型谓词；所有数字统一匹配 `Number`，与 Bridge 共用 envelope 编码规则 |
| [WebViewState.cpp](../../src/webview/WebViewState.cpp) | `release-resource` / `resource-revoked` 内置消息 | 替换 token 读取及 payload 构造，保持方向、字段数和非空 token 校验 |
| [WkWebView.mm](../../src/platform/macos/WkWebView.mm) | token 字面量、发送校验、Foundation 互转、接收 wrapper | 替换全部 Qt JSON；保留 WKWebView 与 Foundation 必需的原生对象边界 |
| [WebView2View.cpp](../../src/platform/windows/WebView2View.cpp) | token 脚本、WebMessage wrapper 解析与内层消息序列化 | 使用新 JSON 编解码；现有 simdutf 继续负责 UTF-8/UTF-16，不改变原生文本职责 |
| [JsonMessageTests.cpp](../../tests/JsonMessageTests.cpp) | JSON、Bridge、schema、资源控制测试 | 替换构造和断言，保留全部已有业务覆盖，补充新库的类型与错误边界 |
| [WkViewTests.mm](../../tests/WkViewTests.mm)、[WebView2RuntimeProbe.cpp](../../tests/WebView2RuntimeProbe.cpp) | schema、Bridge 回调、关闭收尾测试 | 更新 JSON 类型和字段访问，不删除平台验证 |
| [DemoWindow.cpp](../../samples/demo/DemoWindow.cpp) | schema、hello/window 事件、ack/window-state 回传 | 改为新类型，仅在 Qt 控件显示时转换为 QString |

用户当前定位的 [WebView2ContractTests.cpp](../../tests/WebView2ContractTests.cpp) **没有直接 Qt JSON 调用**，本轮通常不需要修改。其他没有 JSON 调用的 session、资源文件流、路径校验、窗口管理和 demo 页面也无需改写。

## 已采用的接口

直接公开 `using Json = nlohmann::json`，保持现有 DOM 使用方式，不额外实现一套 JSON 对象封装。schema 则定义简单的 `JsonType`：`Null`、`Boolean`、`Number`、`String`、`Array`、`Object`；缺失字段用查找结果判断，不再把 Qt 的 `Undefined` 当 JSON 类型。

`BridgeMessage::payload` 和省略 payload 的默认参数显式使用 `Json::object()`。nlohmann 的默认构造是 null，不能机械地把所有 `QJsonObject` 改名后继续使用 `{}`：取消请求、错误回复、测试和空参数调用也须检查。协议 payload 仍必须是对象，数组、字符串和 null 均不能作为 envelope 的 payload 发送。

公开别名意味着 nlohmann/json 是**公开头文件依赖**，不能照搬 simdutf 的私有链接方案。调用方必须匹配锁定 JSON 版本、影响类型布局的宏、编译器、标准库和 Windows CRT；这次接受源码与 ABI 变更，不承诺稳定 DLL ABI。统一通过项目别名使用，不添加旧 QJson 重载。

## 两平台边界

- **Windows**：`PostWebMessageAsJson` 继续接收 JSON 文本；先 `dump`，再经 simdutf 转为 UTF-16。接收端先严格转 UTF-8，再解析 wrapper，校验 documentToken、来源及内层对象。token 字面量直接序列化 JSON 字符串，不再构造单元素数组取 `[0]`。
- **macOS**：`WKScriptMessage.body` 和 `callAsyncJavaScript` 使用 Foundation 对象，保留 `NSJSONSerialization` 作为原生对象与 JSON 字节间的适配。C++ 对象解析和序列化统一交 nlohmann；删除 `foundationObject` 中的 QJson 中转，检查 Foundation 转换失败后返回 false 或拒绝消息。旧系统的脚本调用路径继续只拼接已完成 JSON 转义的文本。
- **共同保留**：可信来源、main frame 或 document token 边界、已提交文档、导航关闭时取消 pending 的规则。JSON 替换不改变授权和生命周期设计。

## 只需明确的语义变化

| 问题 | 迁移规则 |
| --- | --- |
| 数字细分 | nlohmann 区分有符号整数、无符号整数和浮点数；schema 的 `Number` 用 `is_number()`，避免原 `Double` 字段拒绝普通整数 |
| 协议版本 | 接受数值恰为 1 的 `1`、`1.0`；拒绝 `1.5`，不依赖取整转换。六字段、kind、requestId、type、error 和对象 payload 检查继续保留 |
| 缺失与错误类型 | 先 `find` / 类型检查再取值；不使用会插入 null 的读取方式，不依赖 Qt `toString()` 等的默认空值行为 |
| UTF-8 与非有限数字 | 解析和输出使用严格编码，不静默替换非法字节；C++ 构造的 NaN/Infinity 主动拒绝，避免默认 dump 成 null 后掩盖错误 |
| 对象键与数字输出 | 接受新库的键顺序、转义和数字格式；测试比较 JSON 值或业务结果，不要求复刻 Qt 的文本。整数可能保留更多位，但页面仍受 JavaScript Number 精度限制 |
| 重复键 | 采用新库默认的同名键后值覆盖，不另建兼容解析器；schema 检查解析后的对象 |
| 大小和深度 | 默认 64 KiB 消息限制保留；检查传入 Bridge 的字节数及统一编码后的 envelope 字节数。平台重新序列化后的文本不能声称等于页面原始字节；建议新增 128 层 JSON 嵌套上限，超限拒绝整条消息 |
| 异常 | `parse`、`dump`、字段取值及原生适配的库异常统一转为解析/发送失败。发送请求失败时移除 pending 并完成错误回调，不能让 JSON 异常跨 COM 或 Objective-C 回调 |

不追求复刻 Qt 的错误文案、空值转换和序列化字节；以协议有效、策略有效、失败可收尾为验收标准。解析器 callback 过滤节点不等于拒绝整条输入，实现深度限制时必须显式判定整条失败。

## 构建、安装与文档

在根 [CMakeLists.txt](../../CMakeLists.txt) 接入已有子模块，关闭 `JSON_BuildTests`，使用 `nlohmann_json::nlohmann_json` 的 PUBLIC 依赖传播。核心测试直接编译公共实现，也须取得同一 JSON 目标。保持现有 Ada、simdutf、utf8proc 配置，不改第三方源码。

建议沿用子模块的安装规则，开启 `JSON_Install`，以实际参与安装的子目录方式接入；不要同时用 `EXCLUDE_FROM_ALL` 排除它的安装。这样安装前缀能同时提供 JSON 头文件和 CMake package。[包配置模板](../../cmake/system_webviewConfig.cmake.in) 增加匹配版本的 `find_dependency(nlohmann_json 3.12.0 EXACT CONFIG)`，验证使用方只需查找并链接项目目标即可构建。安装规则只安装到验证前缀，不能覆盖正式安装；随分发保留许可证。

实施时同步 [API 参考](../references/api-reference.md)、[架构](../arch/architecture.md)、[英文 README](../../README.md)、[中文 README](../../README.zh-CN.md) 和 [Qt 替代总报告](qt5-base-replacement-research.md) 中的 JSON 类型、示例和依赖说明。当前报告仅记录规划，不把这些当前实现文档提前改成已完成。

## 实施结果

- `webview::Json` 公开别名替代 QJsonObject/QJsonValue，`JsonType` 替代 schema 类型；Bridge、Policy、资源控制、Windows/macOS 平台边界、demo 和测试均已完成迁移。自有 `src/`、`include/`、`tests/`、`samples/` 中已无 `QJson*` 使用。
- 统一 `JsonMessage` 入口负责解析、序列化、Bridge envelope 和类型判断；payload 必须是对象，空对象使用 `Json::object()`。解析/序列化拒绝非法 UTF-8、非有限数字、实际 NUL、非法根类型和超过 128 层嵌套；`Number` 统一接受整数和浮点数。
- Windows 仍由 simdutf 负责 JSON 文本的 UTF-8/UTF-16 边界；macOS 保留 `NSJSONSerialization` 作为 Foundation 对象适配。页面协议字段、来源、document token、大小限制、请求匹配和取消语义保持不变。
- 根 CMake 以 PUBLIC 方式接入 `nlohmann_json::nlohmann_json`，关闭其测试，安装头文件、CMake package 和 MIT 许可证；包配置要求精确版本 3.12.0。未修改第三方源码或子模块版本。
- 使用 `macos-appleclang-verify` preset 配置和构建成功，CTest **3/3 通过**；独立临时安装前缀的消费程序成功找到 nlohmann/json 3.12.0，编译、链接和运行通过。验证目录为 `build/json-package-verify-20261026-1124/`，安装日志为 `install-final.log`。
- 当前主机无法验证 Windows MSVC 后端编译、WebView2 COM 回调及 Windows JSON 原生边界；没有进行性能、体积或 Qt 序列化字节级对比。macOS 的 WKProcessPool 弃用警告与本次替换无关，仍存在。

## 剩余 Qt 替代范围

JSON 替代后，Qt 仍在以下边界使用，不能由 nlohmann/json 提供：

| Qt 范围 | 当前用途 | 可选帮助库或方向 | 当前建议 |
| --- | --- | --- | --- |
| `QApplication`、`QWidget`、布局、事件循环 | 公共宿主 API、demo 和原生视图容器 | 保留 Qt；Windows 可用 Win32，macOS 可用 AppKit，但需要重做公共宿主接口 | 暂不替换，属于 UI/消息循环迁移 |
| `QFile`、`QDir`、`QFileInfo`、`QTemporaryDir` | 文件、目录、快照、测试临时目录 | `std::filesystem`、平台原生文件 API；测试可用 Catch2/GoogleTest | 逐模块迁移，不能用 JSON 库替代 |
| `QIODevice`、`QBuffer`、`QMimeDatabase` | 流接口、资源响应和 MIME 推断 | `std::istream`/自有 I/O、平台 MIME API、libmagic | 先定义流所有权和 MIME 规则，再迁移 |
| Qt 信号/槽、`QTimer`、`QMetaObject` | UI 调度、延迟删除、资源过期和回调 | 原生 RunLoop/dispatch、Boost.Asio/libuv；两平台主线程规则仍需自建 | 不因 JSON 替代而引入，避免扩大范围 |
| `QLoggingCategory`、`qCDebug` | 诊断分类、过滤和宿主日志接管 | spdlog、独立日志 facade、平台统一日志 | 先保持 Qt 日志行为，后续单独评估 |
| `QString` 的 Qt UI 边界 | 窗口标题、状态栏、Foundation/Qt 控件 | simdutf 只处理 UTF-8/UTF-16，平台原生字符串 API 更合适 | 继续按边界转换，不把 JSON 库当通用文本库 |

nlohmann/json 只负责结构化 JSON；它不能替代 Qt UI、事件循环、文件系统、流、MIME、日志或 Objective-C/Win32 对象适配。后续 Qt5-base 迁移应按上述边界逐项调研，避免引入一个库承担不相关职责。

## 后续验收与范围

1. 在 Windows 对应 MSVC preset 验证 WebView2 的 JSON 原生边界、COM 回调失败收尾和 ARM64/x64 构建。
2. 发布前再次验证安装包的精确 nlohmann/json 版本、许可证和最小消费项目；不得把验证前缀当作正式安装目录。
3. Qt 的窗口、事件循环、文件、日志和 MIME 依赖按“剩余 Qt 替代范围”单独立项，不再扩大 JSON 迁移职责。

## 依据与版本记录

依据包括上述项目调用文件、锁定子模块的 [CMake 配置](../../3rdparty/json/CMakeLists.txt)、[JSON 主头文件](../../3rdparty/json/include/nlohmann/json.hpp)、[解析入口](../../3rdparty/json/include/nlohmann/detail/input/parser.hpp)、[序列化实现](../../3rdparty/json/include/nlohmann/detail/output/serializer.hpp) 和 [许可证](../../3rdparty/json/LICENSE.MIT)。本轮结论来自源码核对，不包含性能、二进制体积或跨平台运行实测。

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 2026-10-26 11:13 CST | 梳理 Qt JSON 调用，提出公开接口、Bridge、平台边界和构建安装的整体替代方案。 |
| v2 | 2026-10-26 11:32 CST | 完成 nlohmann/json 替代，记录构建、测试、安装包验证及剩余 Qt 范围。 |
