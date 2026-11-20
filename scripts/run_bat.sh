#!/bin/bash
# ~/run_bat.sh
# Usage: run_bat.sh <windows_bat_path> [args...]

BAT_PATH="${1:?Usage: run_bat.sh <bat_path> [args...]}"
shift

# Convert to CRLF and unbuffer for streaming
# -t flag from ssh side + stdbuf for line-level flushing
exec cmd.exe /c "${BAT_PATH}" "$@" 2>&1 | while IFS= read -r line; do
    # Strip Windows \r that cmd.exe sometimes adds
    echo "${line%$'\r'}"
done
