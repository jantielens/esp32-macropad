---
title: Portal Theme Lab
description: Parked visual studies and screenshot gallery for a future portal redesign.
---

## Status

This experiment preserves 22 visual directions without changing the production
portal's structure, navigation, APIs, or firmware. No design has been selected
for production. It lives on the `experiment/portal-redesign` branch and is
separate from generated build outputs.

The lab overlays the production portal served by
`../../tools/portal-dev-server.py`. Device responses are deterministic local
fixtures, not requests to hardware.

## Open the Gallery

From the repository root, run:

```bash
python3 experiments/portal-theme-lab/lab.py --port 8777 --profile jc3248w535
```

Open <http://localhost:8777/lab>. Python 3.10 or newer is required. Node.js and
Playwright are not required to browse the saved screenshots or live variations.
If the port is occupied, choose another port; the gallery uses relative URLs.

The gallery provides:

* Ten original palette studies and twelve additional style studies
* Light and dark versions of every design
* Home, pad editor, screen saver, and HID desktop comparisons
* Home and screen saver mobile comparisons for nine selected designs
* A full-resolution screenshot dialog with previous/next controls and arrow keys
* Live portal links with the existing theme toggle and an experiment selector

The style studies include compact Console, spacious Studio, Swiss, serif-led
Ledger, borderless Outline, multicolor Spectrum, Hardware, right-aligned Poster,
Wayfinder, Soft, monochrome Ink, and emoji-based Playroom.

## Preserved Results

There are 212 comparison screenshots: 176 desktop and 36 mobile. Desktop
captures use a 1600 x 1100 viewport; mobile captures use 390 x 844. Full-page
image heights may exceed those viewport heights. The results manifest records
each screenshot and its checks.

* [Screenshot files](screenshots/)
* [Capture results](results.json)
* [Style study overview](gallery-styles.png)

Captures checked loading, local fonts, light/dark switching, and horizontal
overflow. Additional browser checks verified gallery filters, screenshot links,
native-size dialog previews, and measurable differences in spacing, fonts,
alignment, borders, icon strokes, and multicolor accents. These are visual
experiments, not a completed accessibility or device integration audit.

The original palette studies and later style studies were captured at different
points in portal development. Minor differences in fixture content may therefore
appear. Refresh captures together when comparing a final shortlist.

## Reproduce Captures

Install the development dependency and Chromium from the lab directory:

```bash
cd experiments/portal-theme-lab
npm ci
npx playwright install chromium
```

Start the server in another terminal, then run `npm run capture`. On systems
missing Chromium's operating-system libraries, install Playwright's documented
browser prerequisites before capturing. The temporary library workaround used
for the original local captures is not part of this experiment.

Optional filters and server overrides:

```bash
LAB_VARIANT=11-console npm run capture
LAB_COLLECTION=style npm run capture
LAB_URL=http://localhost:8778 npm run capture
```

Captures replace matching screenshot files and merge their manifest entries;
unselected studies remain intact. The runner configures USB HID through the
local mock API. Only point `LAB_URL` at the lab server, not a physical device.

## Assets and Licenses

Fonts and icons are bundled so browsing does not require third-party requests.
Font files come from [Google Fonts](https://fonts.google.com/) and use the SIL
Open Font License. This includes Noto Color Emoji. Individual notices are
preserved in [licenses/](licenses/).

The icon bundle is
[Lucide 0.468.0](https://www.npmjs.com/package/lucide/v/0.468.0), distributed
under ISC with Feather-derived portions under MIT. The
[Lucide license](licenses/lucide-LICENSE.txt) and
[Feather license](licenses/feather-LICENSE.txt) are preserved alongside the font
notices.

To refresh fonts and license notices, run `npm run fonts`. This requires `curl`
and network access and uses the current Google Fonts responses, so it is not
a byte-for-byte rebuild of the archived fonts.

## Future Work

* Compare a shortlist and select a coherent visual direction
* Preserve existing information architecture and configuration workflows
* Define production typography, spacing, color, and icon tokens for both modes
* Verify keyboard access, focus states, contrast, readability, and touch targets
* Test long labels, validation errors, loading states, and board-specific fragments
* Confirm mobile behavior and production asset size before adopting dependencies
* Move only the selected styling into production and update relevant portal docs

Keep the gallery, capture tooling, fixture assumptions, and external assets out
of the firmware bundle unless separately reviewed for production use.