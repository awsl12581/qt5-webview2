# simdutf 替代范围调研

> 最后修改时间：2026-10-26 11:10 CST
> 文档版本：v2
> 修改说明：完成 simdutf 转码接入与 Windows 调用替换，记录 macOS 构建和测试结果。

## 结论与基线

simdutf 可以接管当前 Windows 后端经 QString 完成的 UTF-8/UTF-16 转换。现已封装在内部转码函数中，统一服务 WebView2 URL、Bridge 消息、脚本、响应头和路径。它不替代 utf8proc 的字符分类、Ada 的 URL 处理，以及 Qt 的 JSON、文件系统或控件接口。

源码基线为 `08d0a713508ee902b3b44e02fe1af47855eccb26`。现有 [simdutf 子模块](../../3rdparty/simdutf/)锁定 **v9.3.2**、提交 `8d82299606e675b50de8dc16fd118aae4b65bfde`，现已接入项目构建。下表保留迁移前调用作为范围依据，实施和验证结果见后文；未进行性能对比，Windows 集成尚待 Windows 平台构建与运行验证。

## 具体替代点

下表行号对应上述基线；同类调用由内部统一函数接管，不逐处实现转换算法。

| 代码位置 | 现有用途 | 替代边界 |
| --- | --- | --- |
| [WebView2View.cpp](../../src/platform/windows/WebView2View.cpp) 第 46 行 `urlFromWide` | COM UTF-16 URL → QString → UTF-8 → Ada Url | UTF-16 → UTF-8 由 simdutf 完成，URL 继续交 Ada；所有调用该函数的原生 URL 回调随之覆盖 |
| 同文件第 92 行 `PostWebMessageAsJson` | UTF-8 Bridge 消息 → UTF-16 | 直接转码后传 WebView2，保留发送失败返回值 |
| 同文件第 463 行 Range 请求头 | COM 宽字符串 → UTF-8 | 替换转码，HTTP Range 解析保持独立 |
| 同文件第 531 行资源文件路径 | UTF-8 → 宽字符串 → filesystem path → FileRangeStream | 转码后构造 Windows 原生路径，不经过 ANSI 代码页 |
| 同文件第 541、548 行响应头 | MIME/CSP UTF-8 文本 → 宽字符串 | 替换转码；状态码、字节数及头格式化保持原职责 |
| 同文件第 739 行下载建议文件名 | COM 路径 → QString → QFileInfo 提取名称 → UTF-8 | 转码可替换；名称提取另用 Windows filesystem path，或暂保留 QFileInfo，不能只换转码就认定文件逻辑已去 Qt |
| 同文件第 801 行下载目标 | UTF-8 目标路径 → 宽字符串 → `put_ResultFilePath` | 替换转码；现有 QFileInfo 绝对路径及父目录检查另外处理 |
| 同文件第 886–888 行文档 token 脚本 | QJsonDocument UTF-8 输出 → QString → UTF-16 | simdutf 接管序列化结果转码，JSON 转义仍由序列化器负责 |
| 同文件第 937 行收到的 WebMessage JSON | COM UTF-16 JSON → UTF-8 | 严格转码后再解析 JSON；编码失败拒绝消息 |
| 同文件第 993 行注入 Bridge 脚本 | UTF-8 脚本 → UTF-16 | 替换转码，保持异步初始化失败处理 |
| 同文件第 1239 行导航 | Ada URL UTF-8 → UTF-16 | 替换转码，失败报告导航失败 |
| [WebView2Session.cpp](../../src/platform/windows/WebView2Session.cpp) 第 81 行 profilePath | UTF-8 配置路径 → UTF-16 filesystem path | 替换转码，保留目录创建及初始化失败处理 |
| [WebView2RuntimeProbe.cpp](../../tests/WebView2RuntimeProbe.cpp) 第 41 行 | WebView2 版本宽字符串 → UTF-8 | 测试工具复用内部转换函数 |
| [WebView2ContractTests.cpp](../../tests/WebView2ContractTests.cpp) 第 62 行 | 快照路径 QString → std::wstring | 可先取得 UTF-8 再用内部函数；若上游已提供原生 filesystem path，则直接使用原生路径，避免无谓往返 |

## 不应直接替换的代码

- [Application.cpp](../../src/webview/Application.cpp) 已使用 utf8proc 按 Unicode 类别判断应用 ID。simdutf 能验证编码，但不能替代字母/数字分类；无需增加重复校验。
- [Url.cpp](../../src/webview/Url.cpp) 的 Ada 解析、IDNA、来源、百分号编码与本地文件 URL 规则继续保留。simdutf 不承担 URL 语义。
- [WebViewBridge.cpp](../../src/webview/WebViewBridge.cpp)、[JsonMessage.cpp](../../src/webview/JsonMessage.cpp)等的 QString/QJson 转换依赖当前 JSON API；simdutf 不生成或解析 JSON。
- 文件、MIME、日志、demo 控件和测试中的 QString 调用还服务 Qt 接口。去掉这些转换需要先替换相应接口。
- macOS 的 NSString UTF-8 边界已有 Foundation 支持，暂不重写。Windows `wchar_t` 为 16 位，macOS 通常为 32 位，不能跨平台把 `wstring` 一律视为 UTF-16。

## 已实施接口与错误规则

内部 [TextEncoding.h](../../src/internal/TextEncoding.h) 和 [TextEncoding.cpp](../../src/webview/TextEncoding.cpp) 提供 UTF-8 ↔ `std::u16string` 转换，返回 `std::optional` 区分成功、空串和失败，函数为 `noexcept`，分配异常同样返回失败。Windows 另提供 `wstring_view` / `wstring` 边界适配。实际采用本机字节序的 `convert_utf8_to_utf16_with_errors()`、`convert_utf16_to_utf8_with_errors()`，匹配 `char16_t` 的内存表示；Windows 上即 UTF-16LE。转换结果在成功时给出输出码元数量，失败时给出输入错误位置，单位分别是输入字节或 UTF-16 码元。

simdutf 的底层 UTF-16 接口使用 `char16_t*`，而 Win32 使用 `wchar_t*`。通过 Windows 尺寸静态断言和码元复制衔接两种字符串，避免依赖二者指针可直接别名的假设。缓冲区必须足够大，按成功返回的输出长度截断；非法输入产生的部分输出不能继续传给平台。

统一采用严格转换：非法 UTF-8、过长编码、超范围码点和孤立 UTF-16 代理项返回失败，不使用替换字符接口。通用长度接口保留嵌入 NUL；在只能接收 NUL 结尾文本的 WebView2/文件路径边界则拒绝嵌入 NUL，避免截断。COM 返回的 NUL 结尾字符串先计算实际长度，处理完毕仍按原规则释放内存。

失败按调用场景处理：导航报告 Failed，初始化报告 Failed，Bridge 发送返回 false，收到的消息拒绝，下载取消，资源请求返回适当错误响应。回调路径需覆盖释放、deferral 完成和状态收尾；不能把转码失败混同为空串后继续操作，也不能让新增异常跨越 COM 回调。

## 构建与验收

根 CMake 已接入现有子模块，私有静态、PIC 链接 `simdutf::simdutf`。关闭 `SIMDUTF_TESTS`、`SIMDUTF_TOOLS`、`SIMDUTF_BENCHMARKS`、`SIMDUTF_FUZZERS`，保留项目自己的测试；通过 `EXCLUDE_FROM_ALL` 控制子项目默认构建和安装范围，正式发布时再核验安装清单。无需生成或维护另一份 single-header 拷贝，也不必修改第三方源文件。

保持 `ADA_USE_SIMDUTF=OFF`：本次依赖服务项目转码，不顺带切换 Ada 的 IDNA 实现，避免触发另一套依赖获取和职责变更。utf8proc 保持现有分类实现，两库可以同时私有链接，不暴露第三方类型到公共 API。

核心测试已补充可移植 UTF-8/UTF-16 用例：空串、ASCII、中文、emoji、补充平面字符往返，非法 UTF-8、孤立代理项，以及长度和 NUL 行为；[JsonMessageTests.cpp](../../tests/JsonMessageTests.cpp)原有 utf8proc 用例继续运行。macOS 用 `macos-appleclang-verify` preset 验证转码核心及既有功能。Windows 再用对应 MSVC preset 验证 wchar_t 边界、Unicode profile/资源/下载路径、脚本注入、JSON 往返和失败收尾。macOS 测试通过不能代替 Windows 编译与运行验收；不承诺未经测量的加速或体积收益。

## 实施结果

- 已替换表内 Windows 后端及测试工具的转码；自有 `src/`、`tests/` 和 `samples/` 中已无 `fromWCharArray`、`toStdWString`、`fromStdWString` 调用。Qt JSON、路径校验、MIME、日志和控件接口仍保留。
- 下载建议名称先严格转为 UTF-8，再按 Windows 的 `/`、`\` 分隔符提取末段；目标路径继续保留 QFileInfo 检查。转码失败取消下载，已取得的 deferral 仍走统一完成路径。
- Range 头、资源路径、MIME 和 CSP 转码失败返回 500；消息拒绝、初始化失败和导航失败按各自现有返回/回调处理，COM 文本在失败返回前释放。Windows 测试中一处遗留的 QString URL 构造也改为现有 UTF-8 接口。
- 在 macOS arm64 / Apple Clang 上执行 `cmake --preset macos-appleclang-verify`、`cmake --build --preset macos-appleclang-verify` 成功，库、demo 和测试程序均完成编译。首次构建发现新增测试字面量的转义错误，修正后重建成功；已有 WKProcessPool 弃用警告仍存在。
- `ctest --preset macos-appleclang-verify --output-on-failure`：**3/3 通过**，包括核心编码边界测试、macOS session 和 view 测试。构建缓存确认 simdutf 测试/工具/基准/fuzzer 和 `ADA_USE_SIMDUTF` 均关闭，产物为私有静态 `libsimdutf.a`，没有执行安装。
- 当前主机无法验证 Windows 后端编译、COM 回调和宽字符串适配测试；相关源码替换完成，但这些平台验证仍待 Windows MSVC preset 执行。没有测量性能、体积或宣称 Windows 运行已通过。

## 依据

- [锁定版本 README：能力、平台要求和许可](../../3rdparty/simdutf/README.md)
- [转换及校验接口](../../3rdparty/simdutf/include/simdutf/implementation.h)、[错误结果类型](../../3rdparty/simdutf/include/simdutf/error.h)
- [上游 CMake](../../3rdparty/simdutf/CMakeLists.txt)、[库目标定义](../../3rdparty/simdutf/src/CMakeLists.txt)

## 版本修改记录

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 2026-10-26 11:04 CST | 审计 Windows 编码转换调用，明确 simdutf 接入范围、边界处理和验证要求。 |
| v2 | 2026-10-26 11:10 CST | 完成 simdutf 转码接入与 Windows 调用替换，记录 macOS 构建和测试结果。 |
