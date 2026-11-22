#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

case "${1:-}" in
    check) options=(--dry-run --Werror) ;;
    apply) options=(-i) ;;
    *) printf '用法: bash scripts/format.sh {check|apply}\n' >&2; exit 2 ;;
esac

git ls-files -z -- main/ tests/ |
    while IFS= read -r -d '' path; do
        case "$path" in
            *.c|*.h) clang-format "${options[@]}" "$path" ;;
        esac
    done
