#!/usr/bin/env python3
"""Report shipped application sizes across GitHub releases, without rebuilding."""

import argparse
import csv
from datetime import datetime, timezone
from html import escape
import json
from pathlib import Path
import re
import subprocess
import sys


def github_json(endpoint, paginate=False):
    command = ["gh", "api", endpoint]
    if paginate:
        command.extend(["--paginate", "--slurp"])
    else:
        command.extend(["-H", "Accept: application/octet-stream"])
    result = subprocess.run(command, capture_output=True, text=True, check=True, timeout=180)
    return json.loads(result.stdout)


def release_highlights(notes):
    entries = []
    category = "Changes"
    for line in (notes or "").splitlines():
        heading = re.match(r"^#{1,6}\s+(.+)", line)
        bullet = re.match(r"^[-*+]\s+(.+)", line)
        if heading:
            category = heading.group(1).strip()
        elif bullet:
            entries.append({"category": category, "text": bullet.group(1)})
        elif line[:1].isspace() and line.strip() and entries:
            entries[-1]["text"] += " " + line.strip()
    counts = {}
    highlights = []
    for entry in entries:
        category = entry["category"]
        if counts.get(category, 0) >= 2:
            continue
        text = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", entry["text"])
        text = text.replace("**", "").replace("`", "")
        text = re.split(r"(?<=[.!?])\s+(?=[A-Z])", text, maxsplit=1)[0]
        if len(text) > 280:
            text = text[:277].rsplit(" ", 1)[0] + "..."
        highlights.append({"category": category, "text": text})
        counts[category] = counts.get(category, 0) + 1
    return highlights[:8]


def release_points(releases, board, include_prereleases=False):
    points = []
    skipped = []
    for release in sorted(releases, key=lambda item: item.get("published_at") or ""):
        if release["draft"] or (release["prerelease"] and not include_prereleases):
            continue
        tag = release["tag_name"]
        expected = f"esp32-macropad-{board}-{tag}.bin"
        assets = [asset for asset in release["assets"] if asset["name"] == expected]
        if not assets:
            skipped.append(tag)
            continue
        if len(assets) != 1 or not isinstance(assets[0]["size"], int) or assets[0]["size"] <= 0:
            raise ValueError(f"Invalid or ambiguous application asset in {tag}")
        size = assets[0]["size"]
        previous = points[-1]["bytes"] if points else None
        points.append({
            "tag": tag, "published_at": release["published_at"],
            "url": release["html_url"], "asset_name": expected, "bytes": size,
            "delta_bytes": None if previous is None else size - previous,
            "delta_percent": None if previous is None else 100 * (size - previous) / previous,
            "release_notes": release.get("body") or "",
            "footprint": None,
            "footprint_asset": next((asset["id"] for asset in release["assets"]
                                     if asset["name"] == "firmware-footprint.json"), None),
        })
    if not points:
        raise ValueError(f"No app-only release assets found for board {board!r}")
    return points, skipped


def attach_footprint(point, dataset, board):
    if dataset.get("schema_version") != 1:
        raise ValueError("unsupported footprint schema")
    if dataset.get("provenance", {}).get("ref") != point["tag"]:
        raise ValueError("footprint release provenance does not match")
    reports = [report for report in dataset["reports"] if report["board"] == board]
    if len(reports) != 1:
        raise ValueError("board footprint missing or duplicated")
    snapshot = reports[0]
    if snapshot.get("source") != "local-build":
        raise ValueError("footprint is not from a real build")
    if snapshot["context"]["application_binary_bytes"] != point["bytes"]:
        raise ValueError("footprint binary size does not match release asset")
    point["footprint"] = snapshot


def collect(repo, board, include_prereleases):
    releases = [release for page in github_json(f"repos/{repo}/releases", paginate=True)
                for release in page]
    points, skipped = release_points(releases, board, include_prereleases)
    warnings = []
    for point in points:
        asset = point.pop("footprint_asset")
        if asset is None:
            continue
        try:
            attach_footprint(point, github_json(f"repos/{repo}/releases/assets/{asset}"), board)
        except (ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
            warnings.append(f"{point['tag']}: footprint unavailable: {error}")
    return {"schema_version": 1, "repo": repo, "board": board,
            "generated_utc": datetime.now(timezone.utc).isoformat(),
            "include_prereleases": include_prereleases,
            "points": points, "skipped_releases": skipped, "warnings": warnings}


def mib(size):
    return f"{size / 1048576:.3f} MiB"


def delta(size):
    return "n/a" if size is None else f"{size / 1024:+,.1f} KiB"


def chart(points, changes=False):
    width, height, left, top, bottom = 1000, 300, 75, 20, 45
    plot_width, plot_height = width - left - 20, height - top - bottom
    values = [(point["delta_bytes"] or 0) if changes else point["bytes"] for point in points]
    lower = min(0, min(values)) if changes else 0
    upper = max(1, max(values))
    span = upper - lower

    def ypos(value):
        return top + (upper - value) / span * plot_height

    def xpos(index):
        return left + (index + 0.5) * plot_width / len(points)

    parts = [f'<svg viewBox="0 0 {width} {height}" role="img" '
             f'aria-label="{"Release-to-release change in KiB" if changes else "Application size in MiB"}">']
    for tick in range(5):
        value = lower + span * tick / 4
        position = ypos(value)
        label = f"{value / (1024 if changes else 1048576):.2f}"
        parts.append(f'<path class="grid" d="M{left},{position}H{width - 20}"/>'
                     f'<text x="{left - 10}" y="{position + 4}" text-anchor="end">{label}</text>')
    if not changes:
        coordinates = " ".join(f"{xpos(index)},{ypos(value)}" for index, value in enumerate(values))
        parts.append(f'<polyline class="trend" points="{coordinates}"/>')
    for index, (point, value) in enumerate(zip(points, values)):
        xvalue, yvalue = xpos(index), ypos(value)
        title = escape(f"{point['tag']} | {point['published_at'][:10]} | "
                       f"{delta(point['delta_bytes']) if changes else mib(value)}")
        parts.append(f'<a href="#release-{index}" data-release="{index}" aria-label="{title}"><title>{title}</title>')
        if changes:
            bar_width = max(1, plot_width / len(points) * 0.65)
            parts.append(f'<rect class="{"increase" if value >= 0 else "decrease"}" '
                         f'x="{xvalue - bar_width / 2}" y="{min(yvalue, ypos(0))}" '
                         f'width="{bar_width}" height="{max(1, abs(yvalue - ypos(0)))}"/>')
        else:
            parts.append(f'<circle cx="{xvalue}" cy="{yvalue}" r="4"/>')
        parts.append('</a>')
        if index % max(1, len(points) // 8) == 0 or index == len(points) - 1:
            parts.append(f'<text x="{xvalue}" y="{height - 15}" text-anchor="middle">'
                         f'{escape(point["tag"])}</text>')
    return "".join(parts) + "</svg>"


def release_link(point):
    url = point["url"]
    if not url.startswith("https://github.com/"):
        raise ValueError("Release URLs must use https://github.com/")
    return f'<a href="{escape(url, quote=True)}">{escape(point["tag"])}</a>'


def render_html(dataset):
    points = dataset["points"]
    first, latest = points[0], points[-1]
    growth = latest["bytes"] - first["bytes"]
    ranked = sorted(points[1:], key=lambda point: abs(point["delta_bytes"]), reverse=True)[:8]
    jumps = "".join(f'<tr data-release="{points.index(point)}"><td>{release_link(point)}</td><td>{delta(point["delta_bytes"])}</td>'
                    f'<td>{point["delta_percent"]:+.2f}%</td></tr>' for point in ranked)
    rows = []
    for index, point in enumerate(points):
        snapshot = point["footprint"]
        slots = snapshot["context"].get("app_partitions", []) if snapshot else []
        slot = next((entry for entry in slots if entry.get("selected")), None)
        headroom = f'{slot["headroom_bytes"] / 1024:,.1f} KiB' if slot and slot.get("headroom_bytes") is not None else "n/a"
        percent = "n/a" if point["delta_percent"] is None else f'{point["delta_percent"]:+.2f}%'
        rows.append(f'<tr id="release-{index}" data-release="{index}"><td>{release_link(point)}</td>'
                    f'<td>{escape(point["published_at"][:10])}</td><td>{point["bytes"]:,}</td>'
                    f'<td>{mib(point["bytes"])}</td><td>{delta(point["delta_bytes"])}</td>'
                    f'<td>{percent}</td><td>{headroom}</td></tr>')
    detailed = [point for point in points if point["footprint"]]
    subsystem = '<p>No matching footprint snapshots are available.</p>'
    if detailed:
        baseline, last_detail = detailed[0], detailed[-1]
        old_rows = {row["id"]: row for row in baseline["footprint"]["subsystems"]}
        new_rows = sorted(last_detail["footprint"]["subsystems"], key=lambda row: row["flash"], reverse=True)
        maximum = max((row["flash"] for row in new_rows), default=1) or 1
        subsystem_rows = []
        for row in new_rows:
            previous = old_rows.get(row["id"])
            change = row["flash"] - previous["flash"] if previous is not None and len(detailed) > 1 else None
            subsystem_rows.append(f'<tr><td>{escape(row["name"])}</td><td class="bar-cell">'
                                  f'<span class="bar" style="width:{100 * row["flash"] / maximum:.2f}%"></span>'
                                  f'<span>{row["flash"] / 1024:,.1f} KiB</span></td><td>{delta(change)}</td></tr>')
        subsystem = (f'<p>{len(detailed)} snapshots: {release_link(baseline)} to {release_link(last_detail)}. '
                     'Bars show the latest available attributed flash payload; changes use the first available snapshot.</p>'
                     '<div class="scroll"><table><thead><tr><th>Subsystem</th><th>Latest Flash</th>'
                     '<th>Change</th></tr></thead><tbody>' + "".join(subsystem_rows) + '</tbody></table></div>')
    warnings = list(dataset["warnings"])
    for point in detailed:
        warnings.extend(f'{point["tag"]}: {warning}' for warning in point["footprint"]["context"].get("warnings", []))
    warning_html = "".join(f'<li>{escape(warning)}</li>' for warning in warnings)
    skipped = ", ".join(dataset["skipped_releases"]) or "None"
    title = escape(dataset["board"])
    view_data = dict(dataset, points=[dict(point, highlights=release_highlights(point.get("release_notes")))
                                     for point in points])
    initial_highlights = "".join(f'<li><strong>{escape(entry["category"])}:</strong> {escape(entry["text"])}</li>'
                                 for entry in view_data["points"][-1]["highlights"])
    initial_highlights = initial_highlights or '<li>No release highlights available.</li>'
    embedded = json.dumps(view_data, ensure_ascii=True).replace("<", "\\u003c").replace(">", "\\u003e").replace("&", "\\u0026")
    return f'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{title} Firmware History</title><style>
:root{{--ink:#202c2b;--muted:#566462;--green:#00695c;--red:#b33d4b;--line:#d5ddda;--cyan:#d6eeeb}}
*{{box-sizing:border-box}}body{{margin:0;color:var(--ink);background:#f7faf9;font:14px Verdana,sans-serif;line-height:1.6;letter-spacing:0}}
main{{max-width:1180px;margin:auto;padding:36px 28px}}header{{border-top:5px solid var(--green);padding-top:18px}}
h1{{font:36px Georgia,serif;overflow-wrap:anywhere;margin:8px 0}}h2{{font:25px Georgia,serif;margin:0 0 16px}}
p{{max-width:90ch}}a{{color:var(--green)}}.eyebrow,small{{color:var(--muted)}}section{{padding:30px 0;border-bottom:1px solid var(--line)}}
.metrics{{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:20px;padding:24px 0;border-bottom:1px solid var(--line)}}
.metrics strong{{display:block;font-size:23px}}.metrics span{{color:var(--muted)}}.scroll{{overflow-x:auto}}
svg{{width:100%;min-width:680px;display:block}}svg text{{font:12px Verdana,sans-serif;fill:var(--muted)}}
.grid{{stroke:var(--line);stroke-width:1}}.trend{{fill:none;stroke:var(--green);stroke-width:2.5}}
circle,.decrease{{fill:var(--green)}}.increase{{fill:var(--red)}}svg a:hover circle,svg a:focus circle{{r:7}}
.history-layout{{display:grid;grid-template-columns:minmax(0,2fr) minmax(280px,1fr);gap:28px;align-items:start}}
.charts{{min-width:0}}.release-detail{{position:sticky;top:16px;margin-top:30px;border-top:3px solid var(--green);padding:16px 0;background:#f7faf9}}
.release-detail h2{{font-size:23px;margin-bottom:4px}}.release-detail p{{margin:6px 0 12px}}.release-detail ul{{padding-left:20px;margin-top:0}}
.release-detail-body{{height:330px;overflow-y:auto;scrollbar-gutter:stable}}.release-detail li{{margin-bottom:10px}}
.release-detail pre{{white-space:pre-wrap;overflow-wrap:anywhere;font:13px Verdana,sans-serif}}.release-detail small{{display:block;margin-top:10px}}
svg a[data-selected="true"] circle{{r:7;stroke:var(--ink);stroke-width:2}}svg a[data-selected="true"] rect{{stroke:var(--ink);stroke-width:2}}
table{{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}}th,td{{padding:10px 12px;border-bottom:1px solid var(--line);text-align:right;white-space:nowrap}}
th:first-child,td:first-child{{text-align:left}}th{{font-size:12px;color:var(--muted)}}tr:target{{background:var(--cyan)}}
.bar-cell{{position:relative;min-width:220px}}.bar{{position:absolute;left:0;top:7px;bottom:7px;background:var(--cyan);max-width:100%}}.bar-cell span:last-child{{position:relative}}
summary{{cursor:pointer}}footer{{padding-top:20px;color:var(--muted);font-size:12px}}li{{overflow-wrap:anywhere}}
@media(max-width:900px){{.history-layout{{display:block}}.release-detail{{position:sticky;bottom:0;top:auto;margin:0;padding:12px 0;z-index:2}}.release-detail-body{{height:160px}}}}
@media(max-width:600px){{main{{padding:20px 16px}}h1{{font-size:28px}}.metrics{{grid-template-columns:repeat(2,minmax(0,1fr))}}.metrics strong{{font-size:20px}}}}
@media print{{main{{max-width:none;padding:0}}.scroll{{overflow:visible}}svg{{min-width:0}}section{{break-inside:avoid}}.history-layout{{display:block}}.release-detail{{position:static}}.release-detail-body{{height:auto;overflow:visible}}}}
</style></head><body><main>
<header><div class="eyebrow">FIRMWARE / RELEASE HISTORY</div><h1>{title}</h1>
<p>{escape(dataset["repo"])} &middot; {len(points)} releases &middot; {first["published_at"][:10]} to {latest["published_at"][:10]}</p></header>
<div class="metrics"><div><span>First application</span><strong>{mib(first["bytes"])}</strong>{release_link(first)}</div>
<div><span>Latest application</span><strong>{mib(latest["bytes"])}</strong>{release_link(latest)}</div>
<div><span>Net change</span><strong>{growth / 1048576:+.3f} MiB</strong>{growth / first["bytes"] * 100:+.2f}%</div>
<div><span>Peak application</span><strong>{mib(max(point["bytes"] for point in points))}</strong>App-only binary</div></div>
<div class="history-layout"><div class="charts"><section><h2>Application Size</h2><div class="scroll">{chart(points)}</div><small>MiB (1,048,576 bytes). Releases are equally spaced in publication order, not elapsed time.</small></section>
<section><h2>Change Per Release</h2><div class="scroll">{chart(points, changes=True)}</div><small>KiB (1,024 bytes). Red indicates growth; green indicates reduction. First release has no preceding baseline.</small></section>
</div><aside class="release-detail" aria-labelledby="release-detail-heading"><h2 id="release-detail-heading">Release Details</h2>
<div id="selected-release" aria-live="polite" aria-atomic="true"><p id="selected-summary">{escape(latest["tag"])} &middot; {latest["published_at"][:10]} &middot; {mib(latest["bytes"])} &middot; {delta(latest["delta_bytes"])}</p>
<div class="release-detail-body" tabindex="0"><ul id="selected-highlights">{initial_highlights}</ul>
<details id="selected-full-notes"><summary>Full Release Notes</summary><pre id="selected-notes">{escape(latest.get("release_notes") or "No release notes available.")}</pre></details></div></div>
<a id="selected-release-link" href="{escape(latest["url"], quote=True)}">View {escape(latest["tag"])} on GitHub</a>
<small>Highlights follow changelog order, with up to two entries per section. They are not verified causes of size changes.</small></aside></div>
<section><h2>Largest Changes</h2><div class="scroll"><table><thead><tr><th>Release</th><th>Change</th><th>Change %</th></tr></thead><tbody>{jumps}</tbody></table></div>
<p>Release links provide changelog context. Timing alone does not establish which feature caused a size change.</p></section>
<section><h2>Subsystem Footprint</h2>{subsystem}</section>
<section><h2>All Releases</h2><div class="scroll"><table><thead><tr><th>Release</th><th>Published (UTC)</th><th>Bytes</th><th>Size</th><th>Change</th><th>Change %</th><th>App Slot Headroom</th></tr></thead><tbody>{''.join(rows)}</tbody></table></div></section>
<section><h2>Scope And Limitations</h2><p>Exact GitHub release asset sizes measure shipped firmware, including feature flags, dependencies, compiler changes, image headers and alignment. No historical firmware was rebuilt. Merged images, bootloaders, partition tables and other board variants are excluded.</p>
<p>Drafts are excluded. Prereleases are {"included" if dataset["include_prereleases"] else "excluded"}. Missing board assets are skipped, not treated as zero. Each change compares adjacent available releases. Runtime heap and task stacks are not measured.</p>
<p>Subsystem ownership rules can change between releases. Attribution is not guaranteed savings from disabling a feature. Missing categories are not treated as zero. App slot headroom comes from the snapshot's selected reference partition, not a device's active OTA slot. n/a means unavailable or not comparable.</p>
<details><summary>Coverage and diagnostics ({len(warnings)} warnings)</summary><p>Releases without matching board assets: {escape(skipped)}</p><ul>{warning_html}</ul></details></section>
<footer>Generated {escape(dataset["generated_utc"])}. Data: GitHub release metadata and retained footprint attachments.</footer>
<script type="application/json" id="report-data">{embedded}</script>
<script>
const report = JSON.parse(document.getElementById('report-data').textContent);
let selectedIndex = -1;
function showRelease(index) {{
    if (index === selectedIndex) return;
    const release = report.points[index];
    if (!release) return;
    selectedIndex = index;
    const change = release.delta_bytes === null ? 'n/a' : `${{release.delta_bytes >= 0 ? '+' : ''}}${{(release.delta_bytes / 1024).toFixed(1)}} KiB`;
    document.getElementById('selected-summary').textContent = `${{release.tag}} | ${{release.published_at.slice(0,10)}} | ${{(release.bytes / 1048576).toFixed(3)}} MiB | ${{change}}`;
    const list = document.getElementById('selected-highlights');
    list.replaceChildren();
    for (const entry of release.highlights) {{
        const item = document.createElement('li');
        const label = document.createElement('strong');
        label.textContent = `${{entry.category}}: `;
        item.append(label, document.createTextNode(entry.text));
        list.append(item);
    }}
    if (!release.highlights.length) {{
        const item = document.createElement('li');
        item.textContent = 'No release highlights available.';
        list.append(item);
    }}
    document.getElementById('selected-notes').textContent = release.release_notes || 'No release notes available.';
    document.getElementById('selected-full-notes').open = false;
    document.querySelector('.release-detail-body').scrollTop = 0;
    const link = document.getElementById('selected-release-link');
    link.textContent = `View ${{release.tag}} on GitHub`;
    link.href = release.url;
    for (const marker of document.querySelectorAll('svg a[data-release]')) {{
        marker.dataset.selected = Number(marker.dataset.release) === index ? 'true' : 'false';
    }}
}}
for (const eventName of ['pointerover', 'focusin', 'click']) {{
    document.addEventListener(eventName, event => {{
        const target = event.target.closest('[data-release]');
        if (!target) return;
        showRelease(Number(target.dataset.release));
        if (eventName === 'click' && target.closest('svg')) event.preventDefault();
    }});
}}
showRelease(report.points.length - 1);
</script>
</main></body></html>'''


def write_report(dataset, output):
    page = render_html(dataset)
    output.mkdir(parents=True, exist_ok=True)
    (output / "index.html").write_text(page, encoding="utf-8")
    (output / "history.json").write_text(json.dumps(dataset, indent=2) + "\n", encoding="utf-8")
    fields = ["tag", "published_at", "bytes", "delta_bytes", "delta_percent", "url", "asset_name"]
    with (output / "history.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(dataset["points"])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default="jantielens/esp32-macropad")
    parser.add_argument("--board", default="esp32-p4-lcd4b")
    parser.add_argument("--output", type=Path, default=Path("build/firmware-history"))
    parser.add_argument("--include-prereleases", action="store_true")
    parser.add_argument("--input", type=Path, help="Regenerate offline from a previous history.json")
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[\w.-]+/[\w.-]+", args.repo):
        parser.error("--repo must be OWNER/REPOSITORY")
    try:
        dataset = json.loads(args.input.read_text(encoding="utf-8")) if args.input else collect(args.repo, args.board, args.include_prereleases)
        if dataset.get("schema_version") != 1 or not dataset.get("points"):
            raise ValueError("Expected a version 1 history dataset with release points")
        write_report(dataset, args.output)
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        detail = error.stderr.strip() if isinstance(error, subprocess.CalledProcessError) else str(error)
        print(f"Firmware history failed: {detail}", file=sys.stderr)
        return 1
    print(f"Report: {(args.output / 'index.html').resolve()}")
    print(f"{len(dataset['points'])} releases; {sum(bool(point['footprint']) for point in dataset['points'])} footprint snapshots")
    for warning in dataset["warnings"]:
        print(f"Warning: {warning}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())