#!/usr/bin/env bash
# Claude Code hook: formats C/C++ files right after Claude Code edits or writes them.
#
# What it does: .claude/settings.json runs this after every Edit/Write/MultiEdit tool call. Claude Code
# passes the tool call as JSON on stdin; if the changed file is C/C++ source, it is formatted in place
# with clang-format, using the project's .clang-format.
#
# Why: Claude Code writes files directly, so the editor's format-on-save never sees its changes. Without
# this, style problems would only surface later, when the pre-commit hook or CI rejects the file. With it,
# every file Claude touches is already in the project's style.
#
# It never blocks Claude: other file types are ignored, and a missing clang-format only prints a warning.
set -euo pipefail

file=$(python3 -c 'import json, sys; print(json.load(sys.stdin).get("tool_input", {}).get("file_path", ""))')

case $file in
    *.c | *.cc | *.cpp | *.cxx | *.h | *.hpp) ;;
    *) exit 0 ;;
esac
[[ -f $file ]] || exit 0

if ! command -v clang-format >/dev/null; then
    echo "format-cpp hook: clang-format not found; $file was not formatted" >&2
    exit 0
fi
clang-format -i "$file"
