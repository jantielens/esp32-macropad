---
description: "KISS expert — identifies over-engineering, unnecessary indirection, and premature generalization in diffs"
applyTo: "**"
---

# KISS Expert

Review code changes for violations of the Keep It Simple principle: over-engineering, unnecessary complexity, and premature generalization.

## Review Criteria

### Over-Engineering

* Abstractions (classes, interfaces, base classes) introduced for something used exactly once. If there is only one implementation, the abstraction likely adds complexity without benefit.
* Generic solutions for specific problems. Template parameters, strategy patterns, or plugin systems when only one variant exists.
* Configuration systems for values that could be constants. Runtime configurability has a cost; question whether it is needed.

### Unnecessary Indirection

* Wrapper functions that add no logic, validation, or abstraction — they just forward to another function.
* Intermediate data structures created only to be immediately consumed by the next step.
* Layers of indirection where a direct call would be clearer and equally maintainable.
* Getter/setter pairs for fields with no validation, transformation, or access control logic.

### Complex Conditionals

* Nested `if/else` chains deeper than 3 levels that could be flattened with early returns or guard clauses.
* Boolean expressions with more than 3 terms that could be extracted into a named helper function.
* Switch statements with fall-through or shared logic that could be simplified with a lookup table or map.

### Premature Generalization

* Code designed to handle hypothetical future requirements that are not part of the current change.
* Function parameters that are unused or always passed the same constant value.
* Configurable limits (e.g., `MAX_WIDGETS`) set to values far beyond any foreseeable use case without justification.

## Severity Guidelines

| Severity | Criteria |
|---|---|
| High | Abstraction layer that significantly increases code volume with one consumer; architectural over-engineering |
| Medium | Unnecessary wrapper; overly complex conditional that harms readability |
| Low | Minor indirection; slightly generous configuration limit |

## DO

* Consider whether the abstraction might have additional consumers in the near future. Check the codebase for patterns that suggest planned expansion.
* Evaluate the tradeoff: would the simpler version be meaningfully easier to understand and maintain?
* Look at the broader change context. Sometimes apparent over-engineering in isolation makes sense as part of a larger pattern already established in the codebase.

## DON'T

* Flag established project patterns. If the codebase consistently uses a HAL pattern with single implementations, that is a convention, not over-engineering.
* Flag complexity that serves testability. A thin interface that enables mocking is a valid use of abstraction.
* Flag Arduino/embedded patterns that enable compile-time selection (e.g., conditional includes, board override patterns). These are architectural necessities.
* Suggest simplifications that would break the project's modular compilation approach.
