# Windows WebView2 静态执行记录

日期：2026-08-15

## 已落地

- `cmake/platform/windows.cmake` 已接入 `unofficial::webview2::webview2`、`ole32` 和 `shlwapi`。
- `WebViewFactory.cpp` 已增加 `_WIN32` 编译期分支。
- `src/platform/windows/` 已建立 `WebView2Session`、`WebView2View` 类型边界和 Qt native host scaffold。
- Windows CTest target `webview_windows_contract_tests` 已加入。
- `scripts/check_windows_static_contracts.py` 已加入，用于扫描公共边界、动态加载路径和 `TODO(webview2-file-selection)`。
- `FileSelection` 明确返回 `Unsupported`，没有 JS shim 或 `IFileDialog`。

## 静态证据

```text
python3 scripts/check_windows_static_contracts.py
windows_static_contracts_ok

validate_claim_ledger.py
claim_ledger_ok

validate_outcome_contract.py
outcome_contract_ok
```

## ARM64 构建证据

本机使用 Visual Studio 18 Build Tools 的 ARM64 MSVC 19.51、Ninja 和现有 vcpkg
`arm64-windows` 安装完成了 Debug/Release configure 与 build。链接阶段实际消费
`unofficial::webview2::webview2` 和 `WebView2LoaderStatic.lib`。

Debug 下直接运行 `webview_core_tests.exe` 与 `webview_windows_contract_tests.exe`，
两者返回码均为 0。测试运行环境显式加入 vcpkg Debug/Release DLL 路径，并使用
`QT_QPA_PLATFORM=offscreen`；这证明静态 build contract 和公共契约测试，不代表
WebView2 GUI E2E 已完成。

## 尚未声称完成

`WebView2Session` 和 `WebView2View` 当前是显式失败 scaffold。Environment、profile、controller、navigation、bridge、popup、download、permission 和 custom scheme 尚未接入真实 COM API。它们必须在 Windows ARM64 MSVC 和 WebView2 Runtime 环境中完成后才能关闭对应 CSV issue。

当前 session/controller 仍是显式失败 scaffold，因此没有把成功构建和静态测试当作
Runtime integration 或 GUI E2E 证据。

## 用户侧验证命令

```powershell
cmake --preset windows-msvc-arm64-debug
cmake --build --preset windows-msvc-arm64-debug
ctest --preset windows-msvc-arm64-debug --output-on-failure
python scripts/check_windows_static_contracts.py
```

先确认 vcpkg 根目录下的 `installed\arm64-windows\share\unofficial-webview2` 和 WebView2 Runtime 可用，再运行上述命令。
