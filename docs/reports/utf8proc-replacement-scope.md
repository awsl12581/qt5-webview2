# utf8proc 替代范围调研

> 最后修改时间：2026-10-26 11:10 CST
> 文档版本：v4
> 修改说明：同步 simdutf 已完成转码替换，utf8proc 继续负责字符分类。

## 结论

当前代码中需要 utf8proc 直接替代的业务逻辑只有一处：应用 ID 校验中的 Unicode 字符分类。它可以完整接管这项校验，但不能因此删除项目里所有 QString 使用，也不负责 UTF-16 平台接口转换、URL、JSON 或文件系统访问。

初次源码审计基线为 `5f82c5326ba4a779f0cea32a7497e19239311b45`；范围为 `src/`、`include/`、`samples/` 和 `tests/`，主要搜索字符分类、大小写转换、规范化及 QString 编码转换调用。后续已按本报告完成校验替代和构建接入，第三方子模块未修改，未进行性能或 Qt 对比实验。

## 已有依赖

仓库已锁定 utf8proc **2.12.0**，子模块提交为 `79cdd5ab40c79afa559f689ffc13b76812dee1ac`，其 README 声明 Unicode 数据版本为 **18.0.0**。根 CMake 已接入该库，应用 ID 校验已使用其 API。

依据为锁定子模块的 [README](../../3rdparty/utf8proc/README.md)、[头文件](../../3rdparty/utf8proc/utf8proc.h)、[实现](../../3rdparty/utf8proc/utf8proc.c)和 [CMake 配置](../../3rdparty/utf8proc/CMakeLists.txt)，不依赖上游主分支的后续变化。它是可供 C++20 调用的 C 库，提供 UTF-8 解码、字符分类、大小写折叠和规范化；代码与 Unicode 数据的分发声明见 [许可证](../../3rdparty/utf8proc/LICENSE.md)。

## 审计时的代码与替代分工

| 位置与用途 | 当前实现 | 替代方式 |
| --- | --- | --- |
| [Application.cpp](../../src/webview/Application.cpp) 的 `validId` | `QString::fromStdString` 后按 QChar 遍历，调用 `isLetterOrNumber()`，另允许 `-` | 用 `utf8proc_iterate()` 按长度解码 UTF-8，再用 `utf8proc_category()` 接受字母类、数字类和 ASCII 连字符 |
| 同文件的入口文档路径拆分 | QString 按 `/` 拆分，检查 `..` | 属于路径分段，可用标准字符串处理，不需要 utf8proc |
| [WebResourceManager.cpp](../../src/webview/WebResourceManager.cpp) 的 Range 头处理 | QString `trimmed()` 和 QRegularExpression | 属于 HTTP 协议解析；后续按协议定义空白和数字语法，不在本次 Unicode 分类迁移中改动 |
| [WebView2View.cpp](../../src/platform/windows/WebView2View.cpp)、[WebView2Session.cpp](../../src/platform/windows/WebView2Session.cpp) 的文本和路径边界 | UTF-8 与 Windows 宽字符串之间经 QString 转换 | utf8proc 没有现成的 UTF-8/UTF-16 整串互转接口；已选用 simdutf，具体替代范围见 [专项调研](simdutf-replacement-scope.md)，已完成转码替换及 macOS 构建测试，Windows 编译运行待验证 |
| [WkWebView.mm](../../src/platform/macos/WkWebView.mm) 的原生文本边界 | NSString UTF-8 转换，部分错误文本经过 QString | 保留 Foundation 转换，移除多余 QString 中转时不需要 utf8proc |
| 核心文件、JSON、日志、demo 和测试中的 QString 转换 | 适配 QFileInfo、QJsonValue、Qt 日志和 QWidget 等接口 | 随各自 Qt API 的替代处理，不能用 utf8proc 单独接管 |
| [Url.cpp](../../src/webview/Url.cpp) | Ada 解析 URL，项目处理文件路径与百分号编码 | 继续由 Ada 和现有路径逻辑负责，不引入第二套 Unicode 域名处理 |

审计未发现业务代码调用 Unicode 规范化、大小写折叠或字素簇分割，也未发现其他直接的 Unicode 字母/数字分类调用。因此目前不需要接入 utf8proc 的 NFC/NFKC、casefold 或字素 API。

## 已采用的校验规则

保持业务规则简单：应用 ID 非空，由 Unicode 字母、Unicode 数字和 ASCII `-` 组成。对 UTF-8 字节按完整码点分类，使用 utf8proc 当前锁定版本的数据；不复刻 Qt 的 UTF-16 代码单元遍历方式。

- 字母接受 `Lu/Ll/Lt/Lm/Lo`，数字接受 `Nd/Nl/No`。数字不限于 ASCII `0–9`，例如罗马数字和上标数字也属于允许范围。
- 空串、非法 UTF-8、嵌入 NUL、空白、下划线、斜杠、emoji 和组合标记拒绝。解码传递剩余字节长度，不能按 NUL 提前结束而忽略尾部输入。
- 不新增首字符、连字符位置或长度限制；当前规则允许纯连字符 ID。
- 不自动规范化、不折叠大小写，保留输入身份。预组合字母 `é` 可以通过，分解形式 `e` 加组合重音仍被拒绝；这符合“字母、数字、连字符”的类别规则。
- 新实现允许被 utf8proc 判为字母或数字的补充平面字符，例如扩展汉字。旧实现逐个 QChar 检查代理项，无法接受这些字符；Unicode 表升级也可能让新增字母/数字获得支持。这些变化应记录到 API 文档，不添加旧 Qt 兼容分支。

应用 ID 还会用于注册、白名单和 `app://` 来源构造。验收需通过应用创建和现有 Ada 来源逻辑验证 Unicode ID 一致性，但本次不改变 Ada 的 URL 规则。

## 实施与验收

改动覆盖 [Application.cpp](../../src/webview/Application.cpp) 的 ID 校验、根 [CMakeLists.txt](../../CMakeLists.txt)、[核心测试](../../tests/JsonMessageTests.cpp)、[macOS session 测试](../../tests/WkSessionTests.mm)及相关 API、架构和调研文档。函数继续保持内部实现，公共字段仍使用 UTF-8 `std::string`，不公开 utf8proc 类型。

CMake 通过 `add_subdirectory` 接入现有子模块，业务库和单独编译 Application.cpp 的核心测试均私有链接 `utf8proc::utf8proc`。使用静态、PIC 构建，关闭 `UTF8PROC_INSTALL` 和 `UTF8PROC_ENABLE_TESTING`，避免额外安装与上游测试数据下载；未修改子模块自身配置文件。

测试覆盖 ASCII/中文、非 ASCII 数字、补充平面字母、连字符，以及空串、组合标记、emoji、非法/截断 UTF-8 和嵌入 NUL。再验证 Unicode ID 的应用注册与来源匹配，使用 `macos-appleclang-verify` preset 构建并运行现有测试；Windows 编译验证在对应平台完成。无需扩展到一般文本规范化或性能对比。

2026-10-26 在 macOS arm64、Apple Clang 21 环境验证：上述 preset 配置、构建成功，3 项 CTest 测试全部通过。核心测试覆盖字符边界、Ada 来源、资源映射和策略白名单；[macOS session 测试](../../tests/WkSessionTests.mm)覆盖中文与补充平面 ID 注册及重复注册拒绝。构建产物为静态 `libutf8proc.a`，`UTF8PROC_INSTALL` 和 `UTF8PROC_ENABLE_TESTING` 均为 OFF。Windows 本次未编译。首次构建发现测试访问了未公开定义的 WebApplication 成员，移除该访问后构建通过；身份与来源检查由内部核心测试承担。

## 版本修改记录

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 2026-10-26 10:55 CST | 核对 Unicode 处理调用，明确 utf8proc 的直接替代点、迁移规则及验收范围。 |
| v2 | 2026-10-26 10:58 CST | 记录 utf8proc 校验替代、构建接入和测试结果。 |
| v3 | 2026-10-26 11:04 CST | 同步 Windows 转码选用 simdutf 的决策，保持 utf8proc 字符分类职责。 |
| v4 | 2026-10-26 11:10 CST | 同步 simdutf 已完成转码替换，utf8proc 继续负责字符分类。 |
