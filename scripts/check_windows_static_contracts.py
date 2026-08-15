#!/usr/bin/env python3
"""Static boundary checks for the Windows WebView2 backend."""

from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
public = [root / "src" / "webview", root / "samples", root / "tests"]
native_pattern = re.compile(r"WebView2\.h|ICoreWebView2|HWND|windows\.h|wrl\.h|LoadLibrary|GetProcAddress|dlopen|dlsym")
violations = []
for base in public:
    for path in base.rglob("*"):
        if path.suffix not in {".h", ".hpp", ".cpp", ".mm", ".cmake"}:
            continue
        if "platform/windows" in path.as_posix() or "platform/macos" in path.as_posix():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for line_no, line in enumerate(text.splitlines(), 1):
            if native_pattern.search(line):
                violations.append(f"{path.relative_to(root)}:{line_no}:{line.strip()}")

todo = root / "src" / "platform" / "windows" / "WebView2Session.cpp"
if "TODO(webview2-file-selection)" not in todo.read_text(encoding="utf-8"):
    violations.append("missing TODO(webview2-file-selection)")

if violations:
    print("\n".join(violations), file=sys.stderr)
    raise SystemExit(1)
print("windows_static_contracts_ok")
