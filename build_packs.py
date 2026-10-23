#!/usr/bin/env python3
"""Build packs.json for the StepMania 5 PS Vita pack browser.

Queries the StepMania Online pack catalogue, resolves the direct .zip mirror
URL for every curated pack and (by default) verifies that the archive is
reachable before writing the index.

Usage:
    python build_packs.py [--output packs.json] [--no-verify]
"""

import argparse
import html
import json
import re
import sys
import time
import urllib.parse
import urllib.request

API_URL = "https://stepmaniaonline.net/api/packs/datatables"
MIRROR_URL = "https://stepmaniaonline.net/download/mirror/{pack_id}/"
USER_AGENT = "StepMania5-Vita/1.0 (+packs.json builder)"

CURATED_PACKS = [
    (1098, "Dance Dance Revolution (AC) (JP)"),
    (1102, "Dance Dance Revolution 2ndMIX (AC) (JP)"),
    (1105, "Dance Dance Revolution 3rdMIX (AC) (JP)"),
    (9750, "Dance Dance Revolution 4thMIX (AC) (JP)"),
    (1106, "Dance Dance Revolution 3rdMIX PLUS (AC) (JP)"),
    (1107, "Dance Dance Revolution 4thMIX PLUS (AC) (JP)"),
    (796, "DDR 1st Mix to EXTREME"),
    (815, "DDR Extreme 20th Anniversary"),
    (2088, "In The Groove"),
    (2079, "In The Groove 2"),
    (2080, "In The Groove 3"),
    (2086, "In The Groove Rebirth"),
    (2085, "In The Groove Rebirth 2"),
    (9701, "In The Groove 3 Unlocks"),
    (169, "Anime Toshokan - DDR Anime 1st Mix"),
    (170, "Anime Toshokan - DDR Anime 2nd Mix"),
    (183, "Anime Toshokan - DDR Anime 3rd Mix"),
    (185, "Anime Toshokan - DDR Anime 4th Mix"),
    (211, "Anime Toshokan - DDR Anime 5th Mix"),
    (2870, "Otaku's Dream 1st Touhou Mix"),
    (3972, "Touhou 3"),
    (4482, "MIKUFORCE"),
    (4494, "Mister Green Bean's Anime, Vocaloid, and J-Pop Simfiles Set 001"),
    (698, "Alrios's Vocaloid 1st"),
    (1855, "Fraxtil's Arrow Arrangements"),
    (1856, "Fraxtil's Beast Beats"),
    (1718, "Fraxtil's Singles"),
    (4108, "Valentine's Day"),
    (5848, "StepMania 5 Officials"),
    (10792, "Hatena Zubon Tournament Mix"),
]

TAG_RE = re.compile(r"<[^>]+>")
PACK_HREF_RE = re.compile(r'href="/pack/(\d+)"')
SIZE_RE = re.compile(r'data-sort="(\d+)"')


def fetch_json(url, attempts=3):
    last_error = None
    for attempt in range(attempts):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(request, timeout=45) as response:
                return json.load(response)
        except Exception as error:
            last_error = error
            if attempt + 1 < attempts:
                time.sleep(2 * (attempt + 1))
    raise RuntimeError("cannot fetch %s: %s" % (url, last_error))


def query_catalogue(term, length=100):
    query = urllib.parse.urlencode(
        {
            "draw": 1,
            "start": 0,
            "length": length,
            "search[value]": term,
            "search[regex]": "false",
        },
        safe="[]",
    )
    return fetch_json(API_URL + "?" + query)


def clean_text(value):
    return " ".join(html.unescape(TAG_RE.sub("", value)).split())


def parse_row(row):
    href = PACK_HREF_RE.search(row[1])
    size = SIZE_RE.search(row[2])
    if href is None or size is None:
        return None
    return {
        "id": int(href.group(1)),
        "name": clean_text(row[1]),
        "size": int(size.group(1)),
        "date": clean_text(row[5]),
    }


def lookup_pack(pack_id, term):
    payload = query_catalogue(term)
    for row in payload.get("data", []):
        pack = parse_row(row)
        if pack is not None and pack["id"] == pack_id:
            return pack
    raise RuntimeError("pack %d (%s) not found in the catalogue" % (pack_id, term))


def human_size(size):
    if size >= 1024 ** 3:
        return "%.1f GB" % (size / 1024 ** 3)
    if size >= 1024 ** 2:
        return "%.1f MB" % (size / 1024 ** 2)
    if size >= 1024:
        return "%.0f KB" % (size / 1024)
    return "%d B" % size


def resolve_mirror(pack_id):
    url = MIRROR_URL.format(pack_id=pack_id)
    request = urllib.request.Request(
        url, method="HEAD", headers={"User-Agent": USER_AGENT}
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.geturl(), response.status, response.headers


def verify_url(url):
    request = urllib.request.Request(
        url, method="HEAD", headers={"User-Agent": USER_AGENT}
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.status, response.headers


def build(verify):
    packs = []
    seen_urls = set()
    skipped = []

    for pack_id, term in CURATED_PACKS:
        try:
            pack = lookup_pack(pack_id, term)
            url, status, headers = resolve_mirror(pack_id)
            length = int(headers.get("Content-Length") or 0)
        except Exception as error:
            print("skip %-5d %s: %s" % (pack_id, term, error), file=sys.stderr)
            skipped.append(pack_id)
            continue

        if status != 200 or not url.lower().endswith(".zip"):
            print("skip %-5d %s: mirror gave %s %s" % (pack_id, term, status, url), file=sys.stderr)
            skipped.append(pack_id)
            continue

        if verify:
            try:
                status, headers = verify_url(url)
            except Exception as error:
                print("skip %-5d %s: %s" % (pack_id, term, error), file=sys.stderr)
                skipped.append(pack_id)
                continue

            content_type = headers.get("Content-Type", "")
            if status != 200 or "zip" not in content_type.lower():
                print(
                    "skip %-5d %s: %s %s" % (pack_id, term, status, content_type),
                    file=sys.stderr,
                )
                skipped.append(pack_id)
                continue
            length = int(headers.get("Content-Length") or length)

        if length and length != pack["size"]:
            print(
                "note %-5d %s: mirror size %d != catalogue size %d"
                % (pack_id, term, length, pack["size"]),
                file=sys.stderr,
            )

        if url in seen_urls:
            print("skip %-5d %s: duplicate url" % (pack_id, term), file=sys.stderr)
            skipped.append(pack_id)
            continue
        seen_urls.add(url)

        size = length or pack["size"]
        packs.append(
            {
                "name": pack["name"],
                "size": size,
                "sizeLabel": human_size(size),
                "url": url,
                "group": pack["name"],
            }
        )
        print("ok   %-5d %-6s %s" % (pack_id, human_size(size), pack["name"]))

    if len(packs) < 20:
        raise SystemExit(
            "only %d usable packs (need at least 20); %d skipped"
            % (len(packs), len(skipped))
        )
    return packs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="packs.json")
    parser.add_argument(
        "--no-verify",
        action="store_true",
        help="skip the HEAD request that checks every mirror link",
    )
    args = parser.parse_args()

    packs = build(not args.no_verify)

    with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(packs, handle, indent=2, ensure_ascii=False)
        handle.write("\n")

    print("wrote %d packs to %s" % (len(packs), args.output))


if __name__ == "__main__":
    main()
