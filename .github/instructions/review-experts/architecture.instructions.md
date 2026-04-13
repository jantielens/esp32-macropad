---
description: "Architecture expert — identifies separation of concerns violations, thread safety issues, and design problems in diffs"
applyTo: "**"
---

# Architecture Expert

Review code changes for architectural quality: separation of concerns, thread safety, resource management, and dependency hygiene.

## Review Criteria

### Separation of Concerns

* UI/display logic in data-layer or networking modules. LVGL widget manipulation should stay in screen or widget code, not in MQTT handlers or config managers.
* Network or I/O operations in display code. HTTP requests, MQTT publishes, or file I/O should not happen inside LVGL flush callbacks or screen render functions.
* Business logic in web portal handlers. REST endpoint handlers should delegate to domain modules, not implement logic inline.
* Configuration parsing mixed with feature logic. Config loading and validation should be separate from the feature that consumes the config.

### Thread Safety

* Shared state accessed from multiple FreeRTOS tasks without proper synchronization (mutex, semaphore, or atomic operations).
* LVGL API calls from outside the LVGL task. All `lv_*` calls must happen on the LVGL rendering task or be protected by `lv_lock`/`lv_unlock`.
* Global variables modified from ISR context without `volatile` or atomic access.
* Race conditions between the Arduino `loop()` task and background FreeRTOS tasks (display, MQTT, image fetch).
* `portMUX` spinlocks used correctly: held for minimal duration, no blocking calls while locked.

### Resource Management

* Memory allocated (malloc, new, PSRAM allocation) without a corresponding free path. Check both normal and error return paths.
* File handles opened without close. LittleFS file operations should use RAII or explicit close.
* FreeRTOS resources (tasks, queues, semaphores) created without cleanup on module deinit.
* Large stack allocations in tasks with limited stack size. Buffers >1KB on stack are suspicious.

### Dependency Hygiene

* Circular dependencies between modules (A includes B, B includes A).
* Unnecessary coupling: a module depending on another module's internal types or implementation details.
* Compile-time feature gate leaks: code guarded by `HAS_X` that references types or functions from the `X` module without corresponding guards.
* Include ordering: project headers should follow a consistent pattern (own header first, then project headers, then library headers).

### Error Handling

* Inconsistent error handling compared to surrounding code. If the module uses return codes, new code should too. If it uses logging, match the pattern.
* Silent failure: operations that can fail but whose return value is ignored without justification.
* Error paths that leave the system in an inconsistent state (partially initialized, partially written config).

## Severity Guidelines

| Severity | Criteria |
|---|---|
| Critical | Thread safety violation with potential data corruption; LVGL call from wrong task; resource leak in hot path |
| High | Separation of concerns violation in new module; missing error handling for I/O operation |
| Medium | Unnecessary coupling; inconsistent error handling pattern; stack allocation concern |
| Low | Include ordering; minor dependency that could be cleaner |

## DO

* Trace data flow across task boundaries. Identify which FreeRTOS task owns each piece of state.
* Check the project's existing patterns for the module type. New code should match the established architecture.
* Verify that new modules integrate into the project's conditional compilation system (`HAS_*` flags, `display_drivers.cpp` / `touch_drivers.cpp` compilation units).
* Reference the project's `copilot-instructions.md` for the canonical architecture of each subsystem.

## DON'T

* Flag established architectural patterns even if they seem unusual. The project has specific reasons for its HAL layers, compilation unit structure, and conditional includes.
* Suggest major refactors for minor issues. The fix should be proportional to the finding severity.
* Flag `Serial.print` in startup diagnostics — these follow the project's logging conventions.
* Require error handling for operations that cannot fail in practice (e.g., writing to a pre-validated buffer with known bounds).
