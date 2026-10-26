# 文件与流替代范围调研

> 最后修改时间：2026-10-26 12:55 CST
> 文档版本：v2
> 修改说明：完成生产代码中的 Qt 文件/流替代，并记录 macOS 构建与测试结果。

## 结论

本项目的文件与流能力可以在不引入新的通用第三方库的情况下迁移。生产代码已使用 C++20 `std::filesystem` 处理路径、目录和文件状态，使用标准二进制流完成快照复制，并以项目 `ResourceBody` 接口承载资源读取。Windows 继续适配 COM `IStream`，macOS 由文件范围读取器向 `WKURLSchemeHandler` 分块发送数据。

不能把所有 Qt 类型机械替换成标准库类型：`std::filesystem` 不提供安全临时目录、原子保存、Windows reparse point 语义或 macOS 文件协调；`std::istream` 也不能直接替代 Range 读取和当前资源租约。公共 API 已移除 `QIODevice`，改为项目自有 `ResourceBody`，内部实现继续保留平台安全边界。

当前没有证据表明需要 libuv、Boost.Asio、libevent 或其他 I/O 库。它们不能替代路径安全、资源快照、Range 流或 WebView 平台响应契约。

## 当前使用范围

| 范围 | 代码位置 | 当前 Qt 用途 | 替代方向 |
| --- | --- | --- | --- |
| 公共资源响应 | [system_webview.h](../../include/system_webview/system_webview.h) | `ResourceResponse::body` 暴露文件流 | 已改为项目自有 `ResourceBody`，避免导出 Qt ABI |
| 资源快照 | [WebResourceManager.cpp](../../src/webview/WebResourceManager.cpp) | 临时目录、复制、提交和删除重试 | 已使用 `std::filesystem`、`ifstream/ofstream` 和 rename；保留租约、容量和重试 |
| 资源范围读取 | [WebResourceManager.cpp](../../src/webview/WebResourceManager.cpp) | `QFile::seek/read` 与 `QIODevice` 读取 | 项目 `RangeReader` 接口，明确 offset、remaining、短读和 EOF |
| 路径映射 | [ResourceMapping.cpp](../../src/webview/ResourceMapping.cpp) | canonical、目录和相对路径操作 | 已使用 `std::filesystem`；平台检查阻止 symlink/reparse point 越界 |
| WebView 策略 | [WebViewPolicy.cpp](../../src/webview/WebViewPolicy.cpp) | 根目录和文件路径检查 | 已使用 `std::filesystem::relative/canonical`，不用字符串前缀判断目录边界 |
| 应用与宿主文件选择 | [Application.cpp](../../src/webview/Application.cpp)、[HostCompletion.cpp](../../src/webview/HostCompletion.cpp) | bundle、文件和目录判断 | 已使用 `std::filesystem` 状态查询和显式错误码 |
| Windows 后端 | [WebView2View.cpp](../../src/platform/windows/WebView2View.cpp) | Qt 文件对象转路径；后端已有 `CreateFileW`、`ReadFile` 和 `IStream` | 统一使用 UTF-16 原生句柄和项目 Range reader，再适配 `IStream` |
| macOS 后端 | [WkWebViewSession.mm](../../src/platform/macos/WkWebViewSession.mm) | 文件状态和分块读文件 | 已使用 `std::filesystem` 和 `ResourceBody`，NSData 分块交付 |
| 测试与示例 | [tests/](../../tests/)、[samples/](../../samples/) | `QTemporaryDir`、`QFile`、`QFileInfo`，部分测试 HTTP 流 | 测试专用临时目录 helper、`std::ofstream`/原生 socket；不把测试工具接口带入库 API |

## 标准库可以覆盖的部分

### 路径与文件状态

`std::filesystem::path`、`exists`、`is_regular_file`、`is_directory`、`file_size`、`absolute` 和 `weakly_canonical` 可以覆盖大部分 `QFileInfo`、`QDir` 的基本查询。路径应保持为 `std::filesystem::path`，不要在内部先转换成窄字符串再依赖本地编码；Windows 到系统 API 时使用 `path::native()` 或直接构造宽字符路径。

路径映射必须继续执行 URL 解码、规范化、根目录边界检查、目录拒绝和文件类型检查。`canonical`/`weakly_canonical` 只能提供路径解析结果，不能单独解决并发替换、硬链接、symlink 或 Windows reparse point；这些场景仍需 [PathSecurity.cpp](../../src/webview/PathSecurity.cpp) 的平台实现和回归测试。

### 普通二进制文件读写

`std::ifstream`、`std::ofstream` 和 `std::fstream` 可以替代简单的 `QFile::open/read/write/seek`，使用 `std::ios::binary`，并通过 `std::error_code` 或明确异常策略报告失败。适用场景包括测试文件写入、一次性小文件读取和不需要导出所有权的内部辅助。

不应把 `std::ifstream` 直接放进公共 ABI：标准库对象布局、异常和编译器/CRT 绑定会成为接口契约。公共层应暴露项目自己的纯虚接口或不可变响应描述，具体文件句柄留在实现和平台适配层。

## 需要自有接口或平台 API 的部分

### 公共流接口

现有 `ResourceResponse::body` 已改为 `ResourceBody`，平台代码只依赖 `read`、`seek` 和 `path`：

```cpp
class ResourceBody {
public:
    virtual ~ResourceBody() = default;
    virtual std::int64_t read(char* data, std::int64_t maximum) = 0;
    virtual bool seek(std::int64_t offset) = 0;
    virtual std::string path() const = 0;
};
```

实际接口已明确：`read` 返回 0 表示 EOF，`seek` 使用绝对 offset，`path` 只供平台适配取得原生文件路径；reader 与资源 lease 共同保持快照生命周期。接口进入安装包后仍需匹配 C++20 编译器、标准库和 CRT。

### 资源快照与原子提交

`QTemporaryDir` 和 `QSaveFile` 同时解决了临时目录、唯一命名、临时文件和提交失败清理。标准库没有跨平台的安全临时目录 API，也没有封装“写临时文件后原子替换”的完整高层接口。

建议保留一个项目内部 `SnapshotStore`，由平台 helper 提供：

1. 在系统临时根目录下创建随机、权限受控且不存在的目录。
2. 使用二进制句柄写入临时文件，并检查每次写入和最终大小。
3. flush 后在同一文件系统内原子 rename/replace，成功后才登记资源。
4. 资源租约释放后删除快照；文件被读者占用时保留现有延迟重试和容量统计。

macOS/POSIX 可使用 `mkdtemp`、`open`、`rename`/`renameat` 等原生 API；Windows 使用 `GetTempPathW`、随机目录名、`CreateDirectoryW`、`CreateFileW` 和 `MoveFileExW`/对应原子替换语义。随机令牌应继续使用安全随机源，不用时间或普通伪随机数生成文件名。

### 范围流与平台响应

WebView2 需要 `IStream`，当前 [WebView2View.cpp](../../src/platform/windows/WebView2View.cpp) 已经通过 `CreateFileW`、`ReadFile` 和范围状态实现 `FileRangeStream`。迁移后应让它直接接收项目 reader 或原生句柄，保留 COM 引用计数、`S_OK`/`S_FALSE`、seek 和租约保持逻辑。

WKWebView 的 scheme handler 需要按请求范围读取文件并通过 `NSData` 分块发送。可以使用 `std::ifstream` 加 `seekg`，但 Unicode 路径、64 位 offset、读失败和短读必须显式验证；更稳妥的是使用 macOS `open`/`pread` 或 `NSFileHandle`，并把文件描述符关闭封装在 reader 生命周期中。

Range 头解析属于协议逻辑，不应由文件库隐式处理。继续限制当前支持的单一 byte range，使用整数溢出检查和 416 响应；reader 只负责已验证范围内的字节读取。

## 测试和示例迁移

测试中的 `QTemporaryDir`、`QFile` 和 `QFileInfo` 可以由测试 helper 封装，避免每个测试重复平台差异。helper 至少要覆盖：Unicode 文件名、空文件、不可读文件、目录误用、快照原子发布、读者持有时删除失败、释放后的重试和超大文件边界。

`WkViewTests.mm` 中的 `QTcpServer`/`QTcpSocket` 属于测试 HTTP 服务，不是库的文件流实现。可改用 POSIX socket、`NWConnection`，或测试专用的小型原生 HTTP server；不要为了生产资源流引入 libuv/libevent。

## 推荐迁移顺序

1. 先从公共头文件移除 `QIODevice`，定义项目 reader/响应所有权契约；同时保留当前 Range、lease 和错误状态。
2. 把 `ResourceMapping`、`WebViewPolicy`、`Application` 和 `HostCompletion` 的基本路径查询迁移到 `std::filesystem`，将 reparse point、symlink、硬链接和并发替换检查留在平台 PathSecurity helper。
3. 重写 `SnapshotStore` 的临时目录、原子提交、删除重试和容量统计，不要先把整个文件读入内存。
4. 迁移 Windows `IStream` 和 macOS scheme handler，验证 Unicode 路径、64 位范围、短读、EOF、取消和租约生命周期。
5. 最后迁移测试/示例的临时文件和 HTTP server，再从 [CMakeLists.txt](../../CMakeLists.txt) 移除 Qt 文件/网络组件中仅由这些目标使用的依赖。

## 验收标准

- 公共头文件不再包含 `QIODevice`，资源响应不导出 Qt 流对象。
- 正常文件、空文件、Unicode 文件名、目录、缺失文件和不可读文件的结果保持明确。
- 快照只有原子提交成功后才可访问；写入失败不增加已发布容量。
- Range 请求保持 200/206/416 状态、offset、length、Content-Range 和短读语义。
- 读者仍持有时释放资源不会破坏读取；租约结束后删除重试和容量回收与当前一致。
- Windows x64/ARM64 和 macOS 的原生路径、句柄、IStream/NSData 适配分别通过构建和测试。
- 生产库不新增 libuv、libevent、Boost 或其他 I/O 依赖；标准库和必要的平台 API 足以覆盖当前需求。

## 版本修改记录

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 2026-10-26 12:48 CST | 审计 Qt 文件、目录、临时文件和流的实际使用，提出 C++20 与平台原生替代方案。 |
| v2 | 2026-10-26 12:55 CST | 完成生产代码中的 Qt 文件/流替代，并记录 macOS 构建与测试结果。 |
