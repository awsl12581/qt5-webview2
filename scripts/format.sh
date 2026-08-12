#!/usr/bin/env zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${0:a}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CLANG_FORMAT="${CLANG_FORMAT:-/opt/homebrew/opt/llvm/bin/clang-format}"
SOURCES=(${(f)"$(find "$PROJECT_ROOT/src" "$PROJECT_ROOT/tests" "$PROJECT_ROOT/samples" \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) | sort)"})
CHECK_ONLY=false

for arg in "$@"; do
    case "$arg" in
        --check | --dry-run) CHECK_ONLY=true ;;
        *) echo "Usage: $0 [--check|--dry-run]"; exit 1 ;;
    esac
done

command -v "$CLANG_FORMAT" >/dev/null || { echo "clang-format not found: $CLANG_FORMAT"; exit 1; }

for source in "${SOURCES[@]}"; do
    if $CHECK_ONLY; then
        "$CLANG_FORMAT" "$source" | diff -u "$source" - || exit 1
    else
        "$CLANG_FORMAT" -i "$source"
    fi
done
