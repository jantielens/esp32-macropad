---
title: Photoframe Next Image Version 1 Conformance
description: Profile-based vectors and behavioral assertions for interoperable implementations
ms.date: 2026-07-26
ms.topic: reference
keywords:
  - photoframe
  - conformance
  - test vectors
---

## Scope

This kit tests selected requirements from the locked
[contract](../../contract.md). It does not replace the contract. Binary vectors
test media validation directly, while `assertions.json` defines stateful HTTP
scenarios for implementation-specific test adapters.

## Profiles

A profile supplies the token-bound capabilities that are intentionally not
protocol constants:

* Exact panel width and height
* Supported format codes and media types
* Profile-specific valid and invalid binary vectors

The included `e1003-landscape` profile uses the driven geometry `1872 x 1404`.
This differs from the panel's physical `1404 x 1872` portrait specification.

Add another device by adding `profiles/<name>/profile.json` and its `vectors`
directory. The shared assertions and verifier require no device-specific edits.

## Verify a profile

Run the verifier from the repository root:

```bash
tools/photoframe-site/.venv/bin/python \
  docs/dev/photoframe-next-image/conformance/photoframe-next-image-v1/verify_vectors.py \
  e1003-landscape
```

Pass a profile directory instead of its name when validating a profile outside
this bundle. Add `--generate` to regenerate deterministic fixtures with the
production encoders from `tools/photoframe-site/gray16.py` before verification:

```bash
tools/photoframe-site/.venv/bin/python \
  docs/dev/photoframe-next-image/conformance/photoframe-next-image-v1/verify_vectors.py \
  e1003-landscape --generate
```

Generation requires Pillow. Verification uses only the Python standard library.

## Behavioral adapter

The adapter-driven behavioral scenarios have no executable oracle in this kit:
they define the required sequence, and conformance on that axis relies on the
implementer's adapter. The binary vectors and OpenAPI schema are the
machine-checkable parts.

A service or client runner loads `assertions.json`, substitutes capability data
from `profile.json`, and maps the following conceptual operations to its own
test harness:

| Operation | Required behavior |
|-----------|-------------------|
| `reset` | Clear service, frame, cache, and request-observation state |
| `configure_token` | Create a valid, unknown, or revoked token with profile capabilities |
| `configure_images` | Set the eligible logical images and exact transport variants |
| `request` | Send an arbitrary method, path, headers, and body without automatic redirects |
| `fetch_location` | Fetch a redirect target with caller-controlled headers |
| `configure_client` | Set current display, cache entries, and retry schedule |
| `wake_client` | Run one client wake including its bounded recovery cycle |
| `observe` | Return response, display, cache, and ordered request-trace state |

The runner must preserve raw response bodies and headers. It must also expose
the headers received by the redirect target so token stripping can be tested,
including redirects to the same origin.

Each scenario has an immutable `id`, a role under test, setup data, an action,
and observable assertions. A runner should report unsupported adapter
operations as skipped, not passed. Passing binary vectors alone does not claim
full protocol conformance.

## Vector layout

`vectors/manifest.json` records every artifact's SHA-256 hash, size, media type,
expected validity, and expected rejection code. Valid image artifacts must pass
all checks for their profile. Invalid artifacts must fail and include their
declared rejection code.

`transport-crc32-zero.bin` is an empty integrity-only fixture. Its standard
CRC-32 is `00000000`; it is not an image and must not be offered as a show
result.