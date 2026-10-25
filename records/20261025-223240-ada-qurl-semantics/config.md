# Ada 与 QUrl 语义核验配置

> 最后修改时间：2026-10-25 22:32 CST
> 文档版本：v1
> 修改说明：记录独立语义探针的环境、输入、构建选项和命令。

## 目的与基线

启动时间：2026-10-25 22:32 CST。核验本项目使用的 URL 操作能否由 ada 覆盖，识别 app 来源、编码路径和文件转换的差异。不是性能对比，也不运行第三方完整测试。

- 项目版本：`1ca001162b12a5e321dd2c283411c693cb1115fc`，工作区正式源码无修改。
- ada：仓库子模块 `b12a893a45809da8103bb4f1e2f6f5ee13f9100b`，版本 4.0.0。
- 宿主：Darwin arm64；Apple Clang 21.0.0，CMake 4.3.3；Qt 来自项目预设指定的 `/opt/homebrew/opt/qt@5`。
- C++20、Debug、Ninja；ada 静态库，测试/基准/工具/URLPattern/simdutf 均关闭；不安装、不下载依赖、不修改三方源码。
- Qt 与 ada 在同一个小程序中读取同一 UTF-8 输入；分别输出各自组件，不把 Qt 的解析结果再传入 ada。Qt origin 输出复现当前 `normalizedOrigin` 的组件重建逻辑，并额外输出 `adjusted` 的结果。
- 完整输入见 [probe.cpp](./probe.cpp)：网络、自定义 scheme、相对路径、Unicode、端口、凭据、路径编码及文件路径；macOS 上的 Windows 路径字符串只用于观察，不能证明 Windows 原生行为。

## 执行命令与产物

在本目录执行：

```sh
cmake --preset semantic-probe
cmake --build --preset semantic-probe
./build/ada_qurl_probe
```

配置、构建和运行分别完整写入 `configure.log`、`build.log`、`results.jsonl`；运行标准错误写入 `run.stderr.log`。构建目录 `build/` 独立于项目正式和验证构建。探针同时输出实际 Qt 运行时版本和 ada 编译版本；CMakeCache 和 compile_commands 保留实际依赖位置及编译配置。

首次在仓库根目录执行本实验 preset 失败，错误日志为 `initial-configure-error.log`；随后切换到本目录执行成功。无输入替换、无全局依赖安装。结果与局限记录于 [analysis.md](./analysis.md)。

## 版本修改记录

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 2026-10-25 22:32 CST | 记录独立语义探针的环境、输入、构建选项和命令。 |
