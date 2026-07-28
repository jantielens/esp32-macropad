---
title: Photoframe Next Image Version 1
description: Index for the locked contract, HTTP binding, and conformance kit
ms.date: 2026-07-25
ms.topic: reference
keywords:
  - photoframe
  - OpenAPI
  - conformance
---

## Normativity order

The files in this bundle have the following authority:

1. [contract.md](contract.md) is the normative and authoritative Version 1 contract.
2. [photoframe-next-image.openapi.yaml](photoframe-next-image.openapi.yaml) describes only the HTTP binding surface. It does not restate or override contract semantics.
3. [conformance/photoframe-next-image-v1/](conformance/photoframe-next-image-v1/) makes selected behavioral and binary-format requirements testable.

Where the OpenAPI description, conformance kit, or an implementation is silent
or ambiguous, the locked contract controls.

## Bundle contents

| Path | Purpose |
|------|---------|
| [contract.md](contract.md) | Locked transport-neutral contract and HTTP semantics |
| [photoframe-next-image.openapi.yaml](photoframe-next-image.openapi.yaml) | OpenAPI 3.1 description of `GET /api/v1/next` |
| [conformance/photoframe-next-image-v1/README.md](conformance/photoframe-next-image-v1/README.md) | Conformance profile and adapter documentation |
| [conformance/photoframe-next-image-v1/assertions.json](conformance/photoframe-next-image-v1/assertions.json) | Profile-agnostic behavioral assertions |
| [conformance/photoframe-next-image-v1/verify_vectors.py](conformance/photoframe-next-image-v1/verify_vectors.py) | Profile-driven binary vector verifier and generator |

## E1003 profile geometry

The `e1003-landscape` conformance profile uses the driven landscape geometry
`1872 x 1404` (width x height). This is distinct from the panel's physical
`1404 x 1872` portrait specification.

Geometry is not a protocol constant. The service binds geometry to each frame's
token capability profile, and each G16P object carries its width and height. A
future device adds a new conformance profile without changing the contract,
OpenAPI description, behavioral assertions, or verifier.
