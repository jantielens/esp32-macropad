---
title: Photoframe Next Image Contract
description: Proposed transport-neutral contract for selecting and delivering the next photoframe image
ms.date: 2026-07-25
ms.topic: reference
keywords:
  - photoframe
  - image delivery
  - protocol
---

## Status

**This is the locked Version 1 of the photoframe next-image contract.** It
defines a greenfield, best-effort next-image contract.

The contract answers one question: what image should this frame show next?

The key words **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** are
normative requirements.

## Design decisions

Version 1 makes these deliberate choices:

* One `next` operation returns image content or a reference to it
* Selection is best effort, with no display acknowledgement
* A failed request or display may cause an image to be skipped or repeated
* A frame may report its current displayed-content fingerprint as advisory input
* One bearer token both identifies and authenticates a frame
* The token binds to the frame's media capabilities and exact panel geometry
* HTTP uses `/api/v1/next`; the path is the HTTP protocol-version signal
* Successful responses carry a stable opaque image key and content CRC32
* The media type identifies the transport-byte format
* Inline delivery and manually followed `302 Found` redirects are conforming
* `204 No Content` means keep the current display unchanged

There are no assignment revisions, journals, acknowledgements, synchronization
operations, compare-and-swap requirements, or exactly-once display claims.

## Scope

The contract coordinates an image service with a client acting for one frame.
The client can be the frame itself or a bridge. Both use the same operation and
receive the same media metadata and content.

The contract specifies:

* Frame authentication
* Next-image selection semantics
* Image identity and exact-byte integrity
* Inline and referenced image delivery
* Version and error behavior

Image selection policy, storage technology, deployment topology, caching
strategy, and downstream bridge-to-frame transport are outside the contract.

A **wake** is one scheduled client refresh attempt: the client becomes active
(by timer or deep-sleep wake), performs at most one bounded next-image retrieval
cycle, and returns to its idle or sleep state.

## Transport-neutral operation

The protocol has one operation:

```text
next(frame_credential, current_fingerprint?) -> show | keep | error
```

A `show` result contains:

| Value         | Required | Meaning                                      |
|---------------|----------|----------------------------------------------|
| Media content | Yes      | Exact transport bytes or a reference to them |
| Format        | Yes      | Registered format of the exact transport bytes |
| Image key     | Yes      | Stable opaque identity of the logical image  |
| Content CRC32 | Yes      | Integrity value for exact transport bytes    |

A non-HTTP binding MUST preserve these values and the result semantics defined
below. It can encode them differently.

A `keep` result contains no image. The client leaves its current display
unchanged, including when it has no current image.

## Best-effort selection semantics

Each authenticated `next` call asks the service to select an eligible image.
The service MAY select a different image on the next call, whether or not the
client downloaded or displayed the previous result.

The contract does not distinguish selected, served, downloaded, and displayed.
Any service-side history is selection history, not proof of display.

Consequences are intentional:

* A network or display failure can skip an image
* A retry can return the same image or a different image
* Service restart or concurrent requests can cause repeats
* Clients do not acknowledge successful display
* Bridges do not maintain a separate transaction with the service

When the client reports a current fingerprint and another eligible
`(image_key, content_crc32)` pair exists, the service MUST NOT return the
reported pair. This is a one-result exclusion window. A changed CRC for the same
logical image is new transport content and is not excluded. When no alternative
exists, the service MAY return the reported pair or `keep`.

Selection quality beyond this exclusion rule is service policy and
non-conformance guidance. Clients MUST tolerate repeats caused by absent or
stale fingerprints, concurrent requests, and service recovery.

### Advisory current-display fingerprint

A client MAY report the image key and CRC of its currently displayed content.
It records this fingerprint only after the image has passed integrity checks and
the display operation has succeeded. It continues reporting the fingerprint
until another display succeeds.

The fingerprint is advisory:

* The service MAY use it to avoid an immediate repeat
* The service MAY use it when choosing between `show` and `keep`
* The service MAY record it as best-effort display telemetry
* An absent or stale fingerprint MUST NOT cause an error

This report is not an acknowledgement, commit, or precondition. It has no
exactly-once guarantee, and the service cannot assume it observed every display.
It reports a successful panel operation, not proof that a person saw the image
or that it remained visible for any duration.

Both key and CRC are needed because the transport bytes can change without
changing the logical image key. A client MUST send both values or neither.

## Frame identity and authentication

Each frame has one opaque bearer token. The token is both the frame identifier
and its credential. Requests do not carry a separate device ID.

Tokens MUST:

* Be generated from at least 128 bits of cryptographically secure randomness
* Identify exactly one frame configuration
* Bind to that frame's supported formats and exact panel width and height
* Be independently revocable and replaceable
* Be processed without timing-sensitive secret comparison
* Never be returned in a response or written to application logs

Bearer tokens provide authentication only while secret. Deployments that do not
trust the network MUST use a confidential transport such as TLS. A deployment
using plain HTTP on a trusted LAN explicitly accepts that any observer on that
network can steal and replay the token.

The reference firmware does not verify TLS certificates. It configures
`WiFiClientSecure` with `setInsecure()`. HTTPS therefore encrypts the token and
image bytes against passive eavesdropping, but it does not authenticate the
service and does not protect against an active man-in-the-middle attacker. Keep
the reference site and frames on a trusted LAN. Using the reference client on
an untrusted network requires certificate verification before TLS can provide
server authentication.

Authentication failure returns only an authentication error. It MUST NOT reveal
whether a token once existed or why it is invalid.

The service resolves an authenticated token to a frame capability profile:

* One or more supported format codes
* Exact panel width in pixels
* Exact panel height in pixels

The service MUST return only content whose format and intrinsic dimensions
match that profile. The client MUST independently enforce the same constraints.
A missing, unknown, or revoked token always produces the same `401 Unauthorized`
result. Token creation, one-time display, rotation, and revocation UX are outside
the wire contract.

## Protocol versioning

The protocol uses a positive integer major version. Version `1` is defined here.

For HTTP, the major version appears only in the path:

```text
/api/v1/next
```

No protocol-version field or custom version header is required. A client selects
the version by selecting the endpoint. An unsupported major version returns
`404 Not Found`.

Within version 1:

* Services MUST ignore unknown request headers
* Clients MUST ignore unknown response headers
* The four protocol-semantic `Photoframe-*` header names defined here are frozen
* Adding another protocol-semantic `Photoframe-*` header requires a new major version
* A new diagnostic or standard HTTP header is additive only when ignoring it preserves all version 1 behavior
* Any response header that a client must act on requires a new major version
* Existing header meaning, media-type meaning, authentication, and selection semantics MUST NOT change
* A new required request field, required response value, or incompatible semantic change requires a new major path
* Removing inline or redirect delivery requires a new major path

Other transport bindings MUST carry the same integer major in their own framing.
The versioning of a bridge's downstream radio protocol is separate.

## Image identity

`image_key` is a stable opaque identifier for one logical image. Clients compare
it for equality and MUST NOT parse it or derive storage locations from it.

Version 1 does not prescribe how the service generates the key. The same logical
image MUST keep the same key across requests, storage moves, signed URL changes,
and service restarts. Different logical images MUST have different keys.

The HTTP representation MUST be URL-safe ASCII between 1 and 64 characters,
using only letters, digits, hyphen, and underscore.

An image can be re-encoded without changing its key. Clients therefore identify
reusable transport content by the pair `(image_key, content_crc32)`, not by the
image key alone.

## Content integrity

`content_crc32` is the standard CRC-32 value used by zlib and IEEE 802.3. It
covers the exact image transport bytes received after redirects and before
media decoding or decompression. HTTP transfer framing is not part of the CRC
input.

For HTTP, the CRC is exactly eight lowercase hexadecimal digits.

Every image result MUST carry the actual CRC, including `00000000` when that is
the calculated value. The client MUST verify the bytes before cache admission
or display. A mismatch invalidates the bytes. The client MUST discard them and
treat the request as failed.

Image responses MUST NOT use an HTTP content coding. `Content-Encoding` is
absent or `identity`, so clients compute CRC over the response body without an
additional HTTP decoding step.

Re-encoding or recompressing an image can change its CRC without changing its
image key.

## Format registry

The transport-neutral format code identifies the exact byte format. HTTP uses
the corresponding media type instead of carrying the numeric code separately.
A future BLE binding uses the numeric code.

| Code | HTTP media type                   | Content                    |
|------|-----------------------------------|----------------------------|
| `1`  | `image/jpeg`                      | Baseline JPEG image bytes  |
| `2`  | `application/vnd.photoframe.g16p` | Uncompressed G16P bytes    |
| `3`  | `application/vnd.photoframe.g16z` | G16Z-compressed G16P bytes |

An unknown code or media type is unsupported. Codes `0` and `4` through `255`
are reserved for future versions.

### JPEG format code 1

The content is a non-progressive JPEG image. Its intrinsic width and height MUST
exactly match the token-bound panel geometry. The client MUST reject progressive
JPEG, malformed JPEG, and dimension mismatch.

### G16P format code 2

G16P version 1 is an 18-byte header followed by packed 4-bit grayscale pixels.
All multibyte integers are unsigned and little-endian.

| Offset | Size | Field          | Required value or meaning               |
|--------|------|----------------|-----------------------------------------|
| `0`    | `4`  | Magic          | ASCII `G16P`                            |
| `4`    | `1`  | Version        | `1`                                     |
| `5`    | `1`  | Flags          | `0`; all bits reserved                  |
| `6`    | `2`  | Width          | Exact token-bound panel width           |
| `8`    | `2`  | Height         | Exact token-bound panel height          |
| `10`   | `4`  | Payload length | `width * height / 2`                    |
| `14`   | `4`  | Payload CRC32  | CRC-32 of the packed pixel payload only |
| `18`   | Variable | Payload    | Packed pixels in row-major order        |

Version 1 requires an even width. Each payload byte represents two adjacent
pixels. The left pixel is the high nibble and the right pixel is the low nibble.
Nibble `0x0` is black and `0xF` is white, with linear source levels between
them. Rows appear top to bottom and pixels within a row appear left to right.
There is no row padding.

The payload CRC is an internal framebuffer check and is distinct from
`content_crc32`, which covers the complete 18-byte header and payload. The
client MUST validate magic, version, flags, dimensions, payload length, total
length, and payload CRC before display.

### G16Z format code 3

G16Z consists of four ASCII bytes `G16Z` followed immediately by one raw DEFLATE
stream as defined by RFC 1951. The stream has no zlib or gzip wrapper and
decompresses to exactly one complete, valid G16P version 1 object. No bytes may
precede the magic or follow the DEFLATE stream.

The decompressed G16P object MUST satisfy every format code 2 requirement. The
transport `content_crc32` covers the compressed G16Z bytes, including its magic;
the G16P payload CRC covers only the decompressed packed-pixel payload.

A client MUST reject unsupported, malformed, undecodable, or dimension-mismatched
content without displaying or caching it. The service is responsible for
selecting a format and exact geometry supported by the frame identified by the
bearer token. Version 1 does not add per-request format negotiation.

Adding a media type is additive when the service sends it only to frame
configurations that declare support for it. Changing the meaning of an existing
media type is breaking.

## HTTP binding

### Request

```http
GET /api/v1/next HTTP/1.1
Host: photoframe.local
Authorization: Bearer <frame-token>
Photoframe-Current-Image-Key: M7x4qQ2V0A
Photoframe-Current-Content-CRC32: 89abcdef
```

The request has no query parameters and no body. The two `Photoframe-Current-*`
headers are optional. Their value syntax matches the corresponding image-result
headers. The service parses them only after authentication. A malformed or
incomplete pair is ignored as though both headers were absent and MUST NOT block
image delivery.

The service MUST prevent shared HTTP caches from serving one frame's result to
another frame. Version 1 responses use:

```http
Cache-Control: private, no-cache
Vary: Authorization
```

`private, no-cache` permits a frame's private application cache while requiring
the dynamic `next` operation to reach the service each time. A shared cache MUST
NOT reuse the response for another frame.

### Inline image result

An inline image uses `200 OK`. The body is the exact transport content.

```http
HTTP/1.1 200 OK
Content-Type: application/vnd.photoframe.g16z
Content-Length: 612345
Photoframe-Image-Key: M7x4qQ2V0A
Photoframe-Content-CRC32: 89abcdef
Cache-Control: private, no-cache
Vary: Authorization

<image bytes>
```

`Content-Length` SHOULD be present when known. Chunked or equivalent streaming
remains conforming.

`200 OK` means the returned content is the desired display state. If the exact
key and CRC are already displayed, that state is already satisfied; version 1
does not provide a force-redraw command.

### Redirect image result

The service MAY return `302 Found` with `Location` referencing the exact
transport content. The redirect response MUST carry `Photoframe-Image-Key` and
`Photoframe-Content-CRC32`. It MUST NOT use `Content-Type` to describe the target
image because that header describes the redirect response itself.

The client MUST disable automatic redirect following, capture the image key and
CRC from the `302`, and then issue a separate GET to `Location`. It MUST
associate that metadata with the final downloaded bytes and
verify the CRC over those bytes. The final `200 OK` response MUST carry the
precise image `Content-Type` and MUST NOT use a non-identity `Content-Encoding`.
The redirected GET MUST NOT contain the frame bearer token, regardless of
whether `Location` has the same origin. The redirect target MUST be fetchable
without that token. Transparent or automatic redirect following is
non-conforming.

Any redirect status other than `302 Found` is an invalid show result. The client
MUST NOT follow it.

Redirect targets are delivery references, not image identity. They MAY expire
or change without changing the image key.

### Keep result

When the service has nothing new for the frame to show, it returns:

```http
HTTP/1.1 204 No Content
Cache-Control: private, no-cache
Vary: Authorization
```

The response has no body, image metadata, or retry hint. It means keep the
current display exactly as it is. The client does not clear, redraw, or replace
the display, and retries on its configured refresh schedule.

An empty gallery and a policy decision to hold the current image both use this
result because the required client behavior is identical.

On cold start, when the client has no successfully displayed image, `keep`
leaves the panel in its existing boot, blank, or retained hardware state. The
client MUST NOT invent a fallback image as part of this protocol.

### Errors

| Status                   | Meaning                            |
|--------------------------|------------------------------------|
| `401 Unauthorized`       | Bearer token is missing or invalid |
| `404 Not Found`          | Endpoint major is unsupported      |
| `405 Method Not Allowed` | Method is not `GET`                |
| `5xx`                    | Temporary service failure          |

Error responses MUST NOT contain image content or image metadata. Clients retry
temporary failures on their normal failure schedule and MUST NOT treat an error
as `keep`.

A `401` response MUST include `WWW-Authenticate: Bearer`. A `405` response MUST
include `Allow: GET`.

Request handling follows this observable precedence:

1. Route the requested major version; an unsupported path returns `404`
2. Check the method on a supported path; a non-GET request returns `405`
3. Authenticate the bearer token; missing, unknown, and revoked tokens return identical `401` responses
4. Parse the advisory fingerprint; malformed input is ignored as absent

This order prevents advisory parsing from exposing token existence.

### Invalid show result

A client MUST treat a show result as failed when required image-key or CRC
metadata is absent or malformed, the final media type is absent or unsupported,
the transport CRC mismatches, or content validation or decoding fails. It MUST
not display or admit those bytes to cache.

The client MUST bound recovery to one additional logical retrieval cycle in the
same wake. A cycle is one `next` request plus, when that request returns `302`,
one manual content GET. For a failed cached object, the client first evicts that
object and uses the recovery cycle to fetch fresh content. For failed network
content, it may repeat the content GET directly or start one new `next` cycle.
If validation still fails, the client keeps its prior display unchanged,
records a failed wake, and retries on its configured failure schedule.

## Bridge parity

A bridge calls the same `next` operation with the frame's bearer token. It
receives the same image key, CRC, media type, and exact image content as a direct
frame.

The bridge can relay those values and bytes over its downstream transport. The
HTTP contract does not require a metadata-only operation because a full-image
bridge needs the content. If a future bridge proves that metadata-only polling
is necessary, that capability must be designed from measured need rather than
added speculatively to version 1.

A bridge MAY send the advisory current-display fingerprint when its downstream
transport confirms successful display. Otherwise it omits the fingerprint.

The bridge MUST isolate credentials and results per frame. It MUST NOT use one
frame's token to fetch content for another frame.

## Storage and delivery independence

Conformance does not depend on where image bytes or selection state live. Azure,
local files, databases, and object stores can all conform.

The following are deployment concerns below the contract:

* Storage paths, containers, buckets, databases, and sidecar formats
* Local or remote image origins
* Inline serving, reverse proxying, redirects, and signed URLs
* Gallery organization and image-selection policy
* Client cache file names and eviction policy
* Service-side selection history

These choices MUST preserve frame isolation, image identity, exact-byte CRC
semantics, media type, and the result behavior defined above.

## Client cache contract

The cache is keyed by `image_key`, never by a delivery URL or a value parsed
from one. Each entry stores the exact transport bytes, `content_crc32`, format,
and intrinsic dimensions. A cached representation is reusable only when its
`(image_key, content_crc32)` pair matches the show result.

The transport `content_crc32` is authoritative for cache admission. The client
MUST verify it before staging or writing an entry. For G16P and G16Z, the client
also performs the internal G16P payload CRC and structural checks, but those do
not replace transport CRC validation.

Before reuse, the client MUST revalidate the cached byte length, transport CRC,
format, and dimensions. Any failure evicts the entry and follows the bounded
failed-wake behavior. A URL, successful prior decode, or matching image key
alone never makes an entry valid.

## Control boundary

Version 1 has exactly two successful control outcomes:

* `show`: make the returned image the current display state
* `keep`: leave the current display state unchanged

Clearing the display, showing a local fallback, changing brightness, controlling
sleep, rebooting, and updating firmware are not image-selection outcomes. They
require concrete product use cases and belong in a separate device-management
contract if they are ever added.

A client MAY locally re-drive its panel from validated current or cached content
for a manual-refresh button, ghosting clear, or hardware recovery. This does not
call `next`, change the current fingerprint, or require a protocol command. A
service-controlled force-redraw signal is a real deferred capability and would
require a future major version because clients must act on it.

## Not included in version 1

Version 1 intentionally omits:

* `/api/assignment/current`
* `/api/assignment/image`
* `/api/assignment/ack`
* `/api/assignment/sync`
* Assignment and committed revisions
* RFC 1982 comparison
* Assignment journals and compare-and-swap state
* Display acknowledgements and exactly-once commit
* JSON assignment representations
* `device_id` request fields
* `schema`, `state`, `created_at`, and canonical image ID fields
* Protocol-version fields and headers in the HTTP binding
* Mandatory ETag and conditional requests

These mechanisms solve durable display confirmation and replay, which version 1
does not require. They can be reconsidered only if confirmed-display semantics
become a product requirement.

## Deferred capabilities

Version 1 intentionally defers:

* Service-controlled force redraw
* Additional format codes
* Per-request format or geometry negotiation
* Token provisioning and management UX
* Device-management commands

Adding a deferred capability that requires new client behavior requires a new
major protocol version.
