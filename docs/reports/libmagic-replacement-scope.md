# libmagic 替代范围调研

> 最后修改时间：2026-10-26 12:48 CST
> 文档版本：v6
> 修改说明：完成固定 MIME 扩展名 helper 替代，并记录构建与测试结果。

## 结论

libmagic 能替代项目中“根据文件内容识别类型”的部分能力，但不能直接等价替代 `QMimeDatabase`。它读取文件元数据和内容特征，通过 magic database 判断文件格式；使用 `MAGIC_MIME_TYPE` 时可返回 MIME 类型，使用 `magic_buffer()` 时也能对内存内容识别。它不负责文件读取、快照复制、路径安全、HTTP 响应、Range、资源生命周期或 Qt UI。

本项目当前没有调用 libmagic，未新增子模块或构建依赖。已使用项目内部固定扩展名 helper 替代三处 Qt MIME 查询；显式 MIME 仍优先，未知扩展名回退到 `application/octet-stream`。依据为当前源码调用和 file/libmagic 5.x 的官方 README、`magic(3)` API 与 `COPYING`；未来接入内容检测前仍需锁定上游版本、magic database 和许可证文件。

## 为什么 WebView2 会用到 MIME

项目为本地 `app://` 资源注册了 WebView2 `WebResourceRequested` 回调。收到 HTML、CSS、JavaScript、图片或其他资源请求后，项目自己打开文件，创建 `IStream`，再调用 `CreateWebResourceResponse()` 返回状态码、响应头和文件流。此时代码在 [WebView2View.cpp](../../src/platform/windows/WebView2View.cpp) 中拼出：

```text
Content-Type: <mime>
Content-Length: <length>
Accept-Ranges: bytes
```

WebView2 不会替项目推断这个本地文件的 MIME 类型；它只按项目提供的 HTTP 响应处理内容。`Content-Type: text/html` 让文档作为页面解析，`text/css` 让样式表生效，JavaScript MIME 让脚本执行，图片和字体 MIME 让相应资源解码。类型错误时，文件可能被当成普通文本、脚本被拒绝执行，或资源加载失败。

因此这里的 MIME 是**项目构造 WebView2 响应时的 HTTP 元数据**，不是 WebView2 SDK 的额外依赖，也不是 WebView2 内部调用 libmagic。macOS 的 `WKURLSchemeHandler` 有同样的边界：项目构造 `NSURLResponse` 时也需要 `Content-Type`。

## 当前调用点

当前只有三处自动 MIME 推断，全部调用 `QMimeDatabase().mimeTypeForFile(path).name()`：

| 位置 | 输入 | 当前行为 | libmagic 可替代部分 |
| --- | --- | --- | --- |
| [WebResourceManager.cpp](../../src/webview/WebResourceManager.cpp) | 发布文件快照路径 | 只有调用方未提供 `mimeType` 时推断类型 | 可替代自动推断；显式 `mimeType` 必须优先保留 |
| [WkWebViewSession.mm](../../src/platform/macos/WkWebViewSession.mm) | `app` 映射资源路径 | 资源响应前按路径推断 MIME | 可替代自动推断；WKWebView 的 Foundation 响应适配不由 libmagic 提供 |
| [WebView2View.cpp](../../src/platform/windows/WebView2View.cpp) | WebView2 映射资源路径 | 资源响应前按路径推断 MIME | 可替代自动推断；WebView2 response、UTF-16 转换和状态码不由 libmagic 提供 |

已发布资源从 `ResourceResponse::mimeType` 继续传到两个平台后端。三处都应共用一个项目 MIME helper，避免每个平台独立选择规则。对于已发布资源，MIME 在发布阶段确定并缓存；WebView2 的 Range 请求只复用它，不会每个分片重新检测文件。

从当前实现看，项目没有围绕 MIME 建立复杂规则：没有自定义数据库、内容白名单、类型转换层或 MIME 协商逻辑。调用方可以直接提供 `mimeType`；未提供时，代码只调用 Qt 的通用 `QMimeDatabase::mimeTypeForFile(...).name()`，平台层再把得到的字符串写入 `Content-Type`。因此这里实际需要替代的是一个通用的“路径到 MIME 字符串”辅助函数，而不是完整的文件格式分析系统。

## libmagic 能做什么

- 从文件路径、文件描述符或内存前缀读取内容特征，识别数千类文件格式。
- 输出描述文本、MIME 类型或 MIME encoding；`MAGIC_MIME_TYPE` 适合取得 `Content-Type` 候选。
- 识别扩展名无法表达或扩展名错误的二进制格式，适合作为“内容检测”回退。
- 通过外部 magic database 扩展规则；数据库必须和库版本、部署方式一致。

## 其他候选方案

libmagic 之外还有几类方案，但对当前代码来说大多超出需求。不能只按“能返回 MIME 字符串”判断它们值得引入。

| 方案 | 主要能力 | 适合本项目的部分 | 主要限制 |
| --- | --- | --- | --- |
| 项目固定扩展名表 | 按路径后缀返回确定的 MIME | 直接替代当前三处通用 MIME 查询；结果跨平台稳定 | 需要项目维护表；不能识别伪装扩展名或未知二进制 |
| freedesktop `shared-mime-info` | XML 数据库、glob 后缀和部分内容规则 | Linux/Unix 桌面环境中的通用 MIME 数据 | 不是单一轻量 C++ 库；需要携带数据库和解析/查询实现，跨 Windows/macOS 部署成本高 |
| GLib `GContentType` / GIO | 调用平台或 `shared-mime-info` 的内容类型服务 | 已经使用 GLib 的 Linux 应用 | 引入 GLib、GIO 和平台数据库；结果随目标机环境变化，不适合作为本项目的新增基础依赖 |
| Windows `FindMimeFromData`、注册表关联 | Windows 平台按数据、URL 或系统关联推断类型 | Windows 特有的回退探测 | 结果受系统注册表和 URLMon 行为影响，不能提供跨平台一致结果 |
| macOS `UTType` | Uniform Type Identifier 与 MIME/扩展名互转 | macOS 原生资源识别和系统类型映射 | 仅覆盖 Apple 平台；不能成为 Windows 后端的共同实现 |
| Apache Tika 等服务/运行时 | 内容解析和 MIME 检测能力很强 | 独立文件分析服务、复杂文档类型识别 | 需要 Java 或服务进程，体积、启动和部署成本远超 WebView 资源响应需求 |
| 其他轻量 MIME 表库 | 封装扩展名到 MIME 的静态映射 | 减少自维护表的代码量 | 维护状态、覆盖范围、许可证和结果稳定性必须单独审计；通常仍不能做内容检测 |

`shared-mime-info` 是数据集合和规范化数据库，不等于一个可直接链接的跨平台 C++ 替代库；GLib 只是它的常见访问入口。Windows 的 `FindMimeFromData` 和 macOS 的 `UTType` 可以作为平台回退，但会把 MIME 结果交给操作系统版本和用户环境，难以保证相同资源在两端得到相同的 `Content-Type`。对当前这段简单调用而言，这些方案都没有明显必要。

Node.js 的 `mime-types`、`mime-db` 一类包主要提供静态扩展名表，不能直接作为当前 C++ 库依赖。Boost、libuv、Boost.Asio 和 HTTP 库也不负责 MIME 类型数据库或内容识别，不能因为它们参与通信或 I/O 就视为 MIME 替代方案。

## 它不能直接替代什么

- `QMimeDatabase` 的扩展名数据库、别名、glob 优先级和 Qt 资源类型语义。两者返回的 MIME 名称、优先级和未知文件行为可能不同。
- Web 资源的产品规则。空文件、无内容的 CSS/JS/JSON、SVG 和普通 UTF-8 文本可能只能识别为 `text/plain` 或 generic data，不能保证得到浏览器期望的 `text/css`、`text/javascript`、`application/json` 或 `image/svg+xml`。
- 路径和文件系统安全。`magic_file()` 读取文件时仍需由项目先完成 canonical path、根目录和大小限制检查；不能把 MIME 识别当作授权检查。
- 资源快照、文件流、Range、`QIODevice`、`NSHTTPURLResponse`、WebView2 response、日志和异常收尾。
- UTF-8/UTF-16 转码。现有 Windows 边界仍由 simdutf 负责。

## 推荐规则

采用与当前调用职责一致的简单优先级：

1. 调用方显式传入的 MIME 直接使用。
2. 未传入时，按路径扩展名查询项目固定 Web MIME 表，至少覆盖 `html`、`css`、`js`、`json`、`svg`、图片、字体、音视频和 wasm。
3. 无扩展名或未知扩展名统一回退到 `application/octet-stream`。

这比直接把 `magic_file()` 接到现有调用点更接近当前需求。若未来确认无扩展名文件也必须识别，再单独加入内容检测，并用资源矩阵验证它不会改变已知 Web 扩展名的结果。

## 构建与部署关口

- libmagic 是 C API，接入需要处理 `magic_t` 生命周期、错误字符串、线程使用方式和异常边界；不得让 C API 错误跨平台回调传播。
- `MAGIC_MIME_TYPE` 依赖 magic database。数据库应作为项目安装产物随库部署，不能依赖目标机 `/usr/share/misc/magic.mgc` 或其他系统路径。
- 必须验证 Windows MSVC v143 的 x64/ARM64 构建、静态/动态链接、数据库查找、运行时路径、许可分发和交叉编译；Linux/macOS 上能找到系统 `libmagic` 不构成 Windows 验证。
- 需要核查压缩、CDF、ELF、文本等可选规则是否进入最终 database，以及最终包的体积和启动开销。
- 先做小规模矩阵：HTML/CSS/JS/JSON/SVG、空文件、无扩展名文本、伪装扩展名、PNG/WOFF/WASM、超大文件和不可读文件；记录 QMimeDatabase、libmagic 和固定扩展名表的实际结果。

## 建议

是的，当前代码不需要复杂的 MIME 识别库。Qt5 的用法本身也是通用文件类型查询，项目只依赖最终的 MIME 字符串。现已使用 `std::string`/`std::string_view` 配合项目内固定的 Web 扩展名表完成替代；它覆盖当前资源类型，并且比引入 `shared-mime-info`、GLib、平台 MIME API 或 Apache Tika 更容易保持跨平台一致。

推荐的统一策略是：显式 MIME > 项目固定扩展名表 > `application/octet-stream`。只有未来出现“无扩展名文件也必须识别”这一实际需求时，才增加 libmagic 或平台 API 回退。无论选择哪种回退，都不能替代 WebView2/WKWebView 响应构造、文件安全检查、Range 处理或资源生命周期管理。

## 依据与版本记录

- [file/libmagic README](https://github.com/file/file/blob/master/README.md)
- [libmagic API](https://github.com/file/file/blob/master/src/magic.h.in)
- [file/libmagic 许可证](https://github.com/file/file/blob/master/COPYING)

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 2026-10-26 11:36 CST | 审计 MIME 调用，明确 libmagic 的可替代范围、规则优先级和部署验证要求。 |
| v2 | 2026-10-26 11:40 CST | 补充 WebView2 资源响应中的 MIME 作用，澄清它不是 WebView2 的库依赖。 |
| v3 | 2026-10-26 12:31 CST | 补充项目固定 MIME 表、shared-mime-info/GLib、Windows/macOS 原生 API 及服务型识别方案的比较，明确统一推荐策略。 |
| v4 | 2026-10-26 12:38 CST | 根据实际调用代码收敛方案，明确当前 MIME 使用是通用路径映射，不是复杂内容识别。 |
| v5 | 2026-10-26 12:40 CST | 将推荐规则进一步收敛为显式 MIME、固定扩展名表和稳定回退。 |
| v6 | 2026-10-26 12:48 CST | 完成固定 MIME 扩展名 helper 替代，并记录构建与测试结果。 |
