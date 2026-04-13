---
description: "Binding system expert — identifies binding template, scheme registration, and data flow issues in diffs"
applyTo: "**"
---

# Binding System Expert

Review code changes for binding template engine correctness: scheme registration, token resolution, data flow, and integration with LVGL rendering.

## Review Criteria

### Binding Template Syntax

* Malformed binding tokens in configuration, labels, or test data. Valid format: `[scheme:params]` with optional pipe fallback `[scheme:params|fallback]`.
* Semicolons used as parameter separators within binding tokens. Verify bracket-depth-aware splitting is used when the binding contains nested brackets.
* Missing or incorrect fallback values. The pipe fallback `|` must be at the outermost bracket depth.
* Bindings with format specifiers that do not match the data type (e.g., `%.2f` on a string value).

### Scheme Registration

* New binding schemes that are not registered with `binding_template_register_scheme()`.
* Registration order dependencies: schemes that depend on other schemes being registered first.
* Scheme name conflicts: two schemes registering the same prefix.
* Missing `collect_topics()` implementation for schemes that subscribe to MQTT topics.
* Resolver functions that allocate memory without freeing it (resolvers must write to the provided output buffer).

### Data Flow

* MQTT subscription store reads that happen outside the LVGL task without proper synchronization.
* Binding resolution called from the wrong context. `binding_template_resolve()` must be called from the LVGL task only.
* Data stream registry buffers accessed from tasks other than the LVGL rendering task.
* Stale data: bindings that should invalidate or re-resolve when their upstream data changes but do not.

### Widget Bindings

* Widget data bindings (`data_binding`, `data_binding_2`, `data_binding_3`) that reference non-existent MQTT topics or pad bindings.
* Widget min/max bindings that could produce inverted ranges (min > max).
* Sparkline widgets with time windows that do not match the data stream sample rate.
* Gauge/bar chart widgets with binding formats that produce non-numeric values.

### Pad Binding Integration

* `[pad:name]` bindings that reference names not defined in the pad's `bindings` array.
* Pad binding definitions with circular references (binding A references binding B which references A).
* Pad bindings used in `collect_topics()` that fail to recurse into the underlying scheme's topic collection.
* Missing pad context pointer when resolving pad bindings outside of a pad screen.

### Expression Bindings

* `[expr:expression]` tokens with syntax errors that the expression evaluator would reject.
* Expression bindings that reference inner bindings using incorrect nesting (bracket depth must be correct for semicolon splitting).
* Threshold function calls with unsorted threshold values.
* Division by zero possibilities in expression bindings.
* String comparisons in expressions that are case-sensitive when case-insensitivity might be expected.

## Severity Guidelines

| Severity | Criteria |
|---|---|
| Critical | Thread safety: binding resolution from wrong task; scheme resolver with memory leak |
| High | Malformed binding token that would produce `ERR:xxx` at runtime; missing scheme registration |
| Medium | Missing fallback value; widget binding with potential min>max; pad binding naming issue |
| Low | Format specifier mismatch; expression that could be simplified |

## DO

* Trace the full resolution path for new or modified bindings: token parse → scheme lookup → resolver call → output buffer.
* Check that new binding schemes follow the established pattern: `resolve()` function, optional `collect_topics()`, registration call in the appropriate init function.
* Verify that MQTT topic strings in bindings match the project's topic naming conventions.

## DON'T

* Flag binding syntax in user-facing documentation or examples — those may intentionally show simplified or placeholder bindings.
* Suggest changing the binding engine's architecture (bracket-depth splitting, scheme registry) unless you find a concrete bug.
* Flag binding fallback values for being too generic (e.g., `---`) — this is the project's intentional default placeholder.
* Flag health binding keys that look unfamiliar without checking `health_binding.cpp` for the full list of supported keys.
