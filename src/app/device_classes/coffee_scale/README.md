# Coffee Scale Device Class

Scale + brew action types, brew engine, and portal UI for NAU7802/HX711 coffee-scale boards.

## Command name length limits

Scale and brew action commands are stored in fixed-size char buffers inside their
respective payload structs (`coffee_scale_payload.h`). The on-disk command strings
must fit within these buffers (including the trailing null):

| Action type | Max command length (excl. null) | Sizing constant     | Longest current command         |
|-------------|---------------------------------|---------------------|---------------------------------|
| `scale`     | 15 chars                        | `SCALE_CMD_MAX_LEN` | `cal_weight_set` (14 chars)      |
| `brew`      | 12 chars                        | `BREW_CMD_MAX_LEN`  | `set_template` (12 chars)        |

Exceeding the limit causes silent `strlcpy` truncation and an `unknown cmd`
dispatch failure at runtime — there is no compile-time warning for over-length
command literals.

When adding a longer command string:

1. Update `SCALE_CMD_MAX_LEN` or `BREW_CMD_MAX_LEN` in `coffee_scale_payload.h`.
2. Re-run the build so the `static_assert`s confirm the payload still fits within
   `ACTION_PAYLOAD_DEVICE_CLASS_BYTES` (96 bytes).
