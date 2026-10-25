# Ada 与 QUrl 语义核验分析

> 最后修改时间：2026-10-25 22:35 CST
> 文档版本：v1
> 修改说明：分析本次探针输出，记录差异、构建事实与未验证范围。

## 数据与运行结果

原始数据为 [results.jsonl](./results.jsonl)，共 47 行：1 行运行环境、34 行解析、5 行 route 设置、3 行相对解析、4 行文件转换，即 46 项操作观察。不是 46 项断言测试，不应解读为全部行为通过等价验收。

[configure.log](./configure.log) 与 [build.log](./build.log) 显示配置和编译成功，产物是本目录 `build/ada_qurl_probe`，ada 为 `build/ada/src/libada.a`；运行退出码为 0，[run.stderr.log](./run.stderr.log) 为空。运行时 Qt 版本 5.15.19、ada 编译版本 4.0.0，`__cplusplus` 为 202002。

已核验本次 `build/CMakeCache.txt`：Qt5Core 来自 `/opt/homebrew/opt/qt@5/lib/cmake/Qt5Core`，C++ 编译器 `/usr/bin/c++` 对应配置日志中的 AppleClang 21.0.0，构建类型 Debug，BUILD_SHARED_LIBS/BUILD_TESTING 为 OFF；ADA_TESTING、ADA_BENCHMARKS、ADA_TOOLS、ADA_USE_SIMDUTF、ADA_INCLUDE_URL_PATTERN、ADA_DEVELOPMENT_CHECKS 均为 OFF。`compile_commands.json` 保留实际命令。Debug 名称不表示 ada 额外 development checks 已打开；此结论以缓存为准。

本实验不是性能对比；两个解析器分别从相同 UTF-8 输入开始，各用自己的组件与序列化，不将一方规范化结果作为另一方输入。

## 主要发现

1. `app://demo` 的 ada origin 为 `null`；Qt 项目来源键为 `app://demo`。直接替换会丢失应用身份。
2. `app://DEMO`：Qt host 小写，ada opaque host 保持大小写。Unicode app host 在 Qt 下为 IDNA ASCII，在 ada 下为 UTF-8 百分号编码。网络 IDN 用例中两者则得到相同的 ASCII host。
3. 显式默认端口被 ada 移除，Qt 组件重建保留。凭据在 Qt `adjusted` 中保留、项目来源重建中丢弃，说明现有来源规则自身不统一。
4. `%2e%2e` 和字面 `..` 被 ada 折叠。当前 Qt 资源检查可观察的点段，在 ada pathname 中已经消失。
5. route `a%2Fb`：QUrl setPath 编成 `%252F`，ada set_pathname 保留 `%2F`；不能把两个 setter 当同一输入契约。
6. `resource/token` 无 base 时 ada 失败，带 app base 时正确解析到同一资源 URL。原来的合法相对 QUrl 不能直接装入只表示绝对 URL 的值类型。
7. 换行、空格、反斜杠的网络输入展示 Qt 拒绝而 ada 容错接受的情况；解析成功必须与授权条件分开。
8. file helper 保留字面 `%`，并移除文件名中的换行；不满足任意文件名的无损编码要求。Windows 路径字符串的观察仅代表该函数在当前 macOS 程序中的输出。

## 范围与局限

- 没有运行 WKWebView、WebView2、CSP、原生权限或浏览器来源验收。
- 没有读取或修改实际资源文件，不能据此宣称目录授权或链接防护通过。
- 未在 Windows 构建；盘符/UNC 输出不是 Windows 文件可访问性证据。
- 未运行完整 WPT、性能基准、模糊测试或 Unicode 全量兼容测试。
- `scope-audit.json` 记录词法引用，不是语义调用计数。
- `probe.cpp` 复现项目当前来源重建代码以观察输出，不直接链接生产 `normalizedOrigin()`；文件系统与平台授权逻辑不在实验内。

## 异常记录与复现

首次误在仓库根目录执行 `cmake --preset semantic-probe`，由于根目录没有此 preset 而退出 1；完整输出保存在 [initial-configure-error.log](./initial-configure-error.log)。没有产生构建或改变根预设。随后切换到本实验目录，按 [config.md](./config.md) 的命令配置、构建、运行成功；没有修改输入后重复运行或隐去失败结果。

没有失败候选实现需要保留在正式代码中；本目录只保留一次语义探针、配置、日志与原始结果，供报告核验。完整结论和实施提案见 [调研报告](../../docs/reports/ada-qurl-decoupling-research.md)。

## 版本修改记录

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 2026-10-25 22:35 CST | 分析本次探针输出，记录差异、构建事实与未验证范围。 |
