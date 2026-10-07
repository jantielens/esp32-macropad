#!/usr/bin/env python3
import pathlib
import re

SOURCE = pathlib.Path("src/app/mcp_tools_config.cpp").read_text()
ADAPTER = pathlib.Path("src/app/timer_mcp_adapter.cpp").read_text()


def require(pattern: str, message: str) -> None:
    if not re.search(pattern, SOURCE, re.DOTALL):
        raise AssertionError(message)


require(
    r'\{\s*"timers"\s*,\s*"/config/timers\.json"\s*,\s*timer_config_save_raw\s*,\s*4096\s*\}',
    "MCP Timer writes must delegate to timer_config_save_raw",
)
require(
    r'if \(strcmp\(entry->name, "timers"\) == 0\)\s*\{'
    r'.*?timer_config_exists\(\).*?timer_config_to_json\('
    r'result\.createNestedObject\("config"\)\).*?return true;',
    "MCP Timer reads must report physical existence and normalized config",
)
require(
    r'static void exec_timer_control\(.*?timer_command_run\(\*payload, msg, msg_len\);',
    "MCP Timer control must execute through the shared command runner",
)
require(
    r'static bool tool_timer_control\(.*?timer_mcp_parse_args\(args, &payload'
    r'.*?mcp_run_control\(exec_timer_control',
    "MCP Timer control must validate before main-loop dispatch",
)
if "strlen(mode) >= sizeof(payload->timer_mode)" not in ADAPTER:
    raise AssertionError("MCP Timer adapter must reject oversized mode")
if "strlen(value) >= sizeof(payload->timer_value)" not in ADAPTER:
    raise AssertionError("MCP Timer adapter must reject oversized value")

control_body = re.search(
    r'static void exec_timer_control\(.*?\n\}', SOURCE, re.DOTALL
)
assert control_body is not None
for forbidden in ("timer_configure_and_start(", "timer_toggle_prepared(", "timer_set_countdown_ms("):
    if forbidden in control_body.group(0):
        raise AssertionError("MCP Timer control bypasses the shared command runner")

require(
    r'\{\s*"alarms"\s*,\s*nullptr\s*,\s*alarm_config_save_raw\s*,\s*8192\s*\}',
    "MCP Alarm writes must delegate to the shared durable manager",
)
require(
    r'if \(strcmp\(entry->name, "alarms"\) == 0\).*?alarm_config_to_json\(.*?alarm_status_to_json\(',
    "MCP Alarm reads must report normalized definitions and runtime status",
)
require(
    r'static bool tool_alarm_control\(.*?alarm_command_submit\(',
    "MCP Alarm controls must use the main-loop command queue",
)
require(
    r'if \(c->save == alarm_config_save_raw\).*?alarm_last_error\(',
    "MCP Alarm writes must expose manager validation failures",
)
if 'REGISTER_MCP_TOOL(s_tool_get_alarm_status)' not in SOURCE or 'REGISTER_MCP_TOOL(s_tool_alarm_control)' not in SOURCE:
    raise AssertionError("MCP Alarm status and control tools must be registered")
if '!time_service_timezone_valid(timezone)' not in SOURCE:
    raise AssertionError("MCP timezone writes must use the shared timezone validator")
require(r'alarm\["weekdays"\].*?zero rings once.*?snooze remains available',
    "MCP capabilities must explain one-shot weekday semantics")
require(r'alarm\["one_shot"\].*?once_epoch and once_local.*?missed occurrences disable',
    "MCP capabilities must advertise persisted one-shot status and expiration")

print("timer_mcp_integration: PASS (timers and alarms)")
