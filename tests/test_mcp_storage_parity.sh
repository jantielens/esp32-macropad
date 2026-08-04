#!/bin/bash
# =============================================================================
# MCP storage browser parity guard
# =============================================================================
# The portal and MCP storage surfaces must share their path validation, status,
# directory-listing, and MIME rules. MCP file reads are intentionally bounded
# because the JSON-RPC transport returns Base64 rather than a streamed response.

set -e
cd "$(dirname "$0")/.."

HELPER="src/app/storage_browser.cpp"
HELPER_HEADER="src/app/storage_browser.h"
PORTAL="src/app/components/storage_component.cpp"
MCP="src/app/mcp_tools_config.cpp"
DISPATCHER="src/app/web_mcp.cpp"

require() {
    if ! grep -Fq "$2" "$1"; then
        echo "FAIL: expected '$2' in $1" >&2
        exit 1
    fi
}

require "$HELPER_HEADER" "STORAGE_BROWSER_LIST_MAX_ENTRIES = 128"
require "$HELPER_HEADER" "STORAGE_BROWSER_PATH_MAX_LEN = 192"
require "$HELPER" "storage_browser_path_is_safe"
require "$HELPER" "storage_browser_status_to_json"
require "$HELPER" "storage_browser_list"
require "$PORTAL" "storage_browser_status_to_json"
require "$PORTAL" "storage_browser_list"
require "$PORTAL" "storage_browser_file_content_type"
require "$MCP" "\"get_storage_status\""
require "$MCP" "\"list_storage\""
require "$MCP" "\"read_storage_file\""
require "$MCP" "MCP_STORAGE_FILE_MAX_BYTES = 64 * 1024"
require "$MCP" "MCP_STORAGE_FILE_RESULT_CAPACITY = 96 * 1024"
require "$MCP" "storage_browser_path_is_safe"
require "$MCP" "storage_browser_list"
require "$MCP" "base64::encode"
require "$DISPATCHER" "tool->result_json_capacity"

echo "PASS: portal and MCP storage surfaces share browser rules and bounded file reads."