#!/usr/bin/env python3
"""Enrich a StepMania Online pack index with song counts, meter ranges and banners.

Reads the index produced by build_packs.py / build_all_packs.py, fetches every
pack page on stepmaniaonline.net and adds:

    songs        number of songs in the pack
    meterMin     lowest chart meter in the pack
    meterMax     highest chart meter in the pack
    meterRange   "min-max" label for display
    banner       direct URL of the pack banner image (or null)

By default ``banner`` points at the original image on stepmaniaonline.net.
Pass ``--download-banners`` to instead save downscaled 320x100 copies into a
local folder and reference those (useful for self-hosted indexes).

Usage:
    python enrich_packs.py [--input packs.json] [--output packs.json]
                           [--download-banners] [--workers 8]
"""

import argparse
import html
import io
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

PACK_URL = "https://stepmaniaonline.net/pack/{pack_id}"
USER_AGENT = "StepMania5-Vita/1.0 (+packs.json builder)"
DEFAULT_BANNER_BASE = (
    "https://raw.githubusercontent.com/LuckyI13I/stepmania-psvita/main/banners"
)

DESCRIPTION_RE = re.compile(
    r'<meta property="og:description" content="([^"]*)"', re.I
)
IMAGE_RE = re.compile(r'<meta property="og:image" content="([^"]*)"', re.I)
LABELS_RE = re.compile(r"labels:\s*\[([^\]]*)\]")
SONGS_RE = re.compile(r"Contains\s+(\d+)\s+songs?", re.I)
INT_RE = re.compile(r"-?\d+")

BANNER_MAX_W = 320
BANNER_MAX_H = 100
BANNER_QUALITY = 82


def fetch(url, attempts=3, timeout=60):
    last_error = None
    for attempt in range(attempts):
        try:
            request = urllib.request.Request(
                url, headers={"User-Agent": USER_AGENT}
            )
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return response.read()
        except Exception as error:
            last_error = error
            if attempt + 1 < attempts:
                time.sleep(1.5 * (attempt + 1))
    raise RuntimeError("cannot fetch %s: %s" % (url, last_error))


def read_page(pack_id, cache_dir):
    cached = None
    if cache_dir:
        cached = os.path.join(cache_dir, "%d.html" % pack_id)
        if os.path.exists(cached) and os.path.getsize(cached) > 0:
            with open(cached, "rb") as handle:
                return handle.read()

    body = fetch(PACK_URL.format(pack_id=pack_id))
    if cached:
        os.makedirs(cache_dir, exist_ok=True)
        with open(cached, "wb") as handle:
            handle.write(body)
    return body


def parse_page(body):
    text = body.decode("utf-8", "replace")

    songs = None
    description = DESCRIPTION_RE.search(text)
    if description:
        match = SONGS_RE.search(html.unescape(description.group(1)))
        if match:
            songs = int(match.group(1))

    meters = []
    for block in LABELS_RE.findall(text):
        for value in INT_RE.findall(block):
            number = int(value)
            if 0 < number < 100:
                meters.append(number)

    banner = None
    image = IMAGE_RE.search(text)
    if image:
        url = html.unescape(image.group(1)).strip()
        if url and "nobanner" not in url:
            banner = urllib.parse.urljoin("https://stepmaniaonline.net/", url)

    return {
        "songs": songs,
        "meterMin": min(meters) if meters else None,
        "meterMax": max(meters) if meters else None,
        "bannerSource": banner,
    }


def encode_banner(body, source):
    try:
        from PIL import Image
    except ImportError:
        print("note: Pillow missing, banners are skipped", file=sys.stderr)
        return None

    try:
        image = Image.open(io.BytesIO(body))
        image.load()
    except Exception as error:
        print("note: bad banner %s: %s" % (source, error), file=sys.stderr)
        return None

    if image.mode not in ("RGB", "L"):
        background = Image.new("RGB", image.size, (0, 0, 0))
        if image.mode == "P" and "transparency" in image.info:
            image = image.convert("RGBA")
        if image.mode == "RGBA":
            background.paste(image, mask=image.split()[3])
            image = background
        else:
            image = image.convert("RGB")

    image.thumbnail((BANNER_MAX_W, BANNER_MAX_H), Image.LANCZOS)

    output = io.BytesIO()
    image.convert("RGB").save(
        output, format="JPEG", quality=BANNER_QUALITY, optimize=True
    )
    return output.getvalue()


def store_banner(pack_id, source, banners_dir, banner_base):
    if not source or not banners_dir:
        return None

    target = os.path.join(banners_dir, "%d.jpg" % pack_id)
    if not os.path.exists(target):
        try:
            body = fetch(source, timeout=90)
        except Exception as error:
            print("note: banner %d: %s" % (pack_id, error), file=sys.stderr)
            return None
        data = encode_banner(body, source)
        if not data:
            return None
        os.makedirs(banners_dir, exist_ok=True)
        with open(target, "wb") as handle:
            handle.write(data)

    return "%s/%d.jpg" % (banner_base.rstrip("/"), pack_id)


def enrich(pack, cache_dir, banners_dir, banner_base, banner_mode):
    pack_id = pack.get("id")
    if not pack_id:
        match = re.search(r"/(?:download/)?pack/(\d+)", str(pack.get("url", "")))
        pack_id = int(match.group(1)) if match else None
    result = dict(pack)
    if pack_id:
        result["id"] = pack_id

    meta = {"songs": None, "meterMin": None, "meterMax": None, "bannerSource": None}
    if pack_id:
        try:
            meta = parse_page(read_page(pack_id, cache_dir))
        except Exception as error:
            print("note: pack %s: %s" % (pack_id, error), file=sys.stderr)

    result["songs"] = meta["songs"]
    result["meterMin"] = meta["meterMin"]
    result["meterMax"] = meta["meterMax"]
    if meta["meterMin"] is not None:
        result["meterRange"] = "%d-%d" % (meta["meterMin"], meta["meterMax"])
    else:
        result["meterRange"] = None

    if banner_mode == "local" and meta["bannerSource"]:
        result["banner"] = store_banner(
            pack_id, meta["bannerSource"], banners_dir, banner_base
        )
    elif banner_mode == "remote":
        result["banner"] = meta["bannerSource"]
    else:
        result["banner"] = None

    return result


def order_fields(pack):
    ordered = {}
    for key in (
        "id",
        "name",
        "size",
        "sizeLabel",
        "date",
        "songs",
        "meterRange",
        "meterMin",
        "meterMax",
        "url",
        "group",
        "banner",
    ):
        if key in pack:
            ordered[key] = pack[key]
    for key in pack:
        if key not in ordered:
            ordered[key] = pack[key]
    return ordered


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", default="packs.json")
    parser.add_argument("--output", default=None)
    parser.add_argument(
        "--download-banners",
        action="store_true",
        help="save downscaled banner copies locally instead of linking the site",
    )
    parser.add_argument("--banners-dir", default="banners")
    parser.add_argument("--banner-base", default=DEFAULT_BANNER_BASE)
    parser.add_argument("--no-cache", action="store_true")
    parser.add_argument("--cache-dir", default=os.path.join(".cache", "smo"))
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument(
        "--compact",
        action="store_true",
        help="write minified JSON (keeps big indexes under the 4 MB buffer)",
    )
    args = parser.parse_args()

    output = args.output or args.input
    cache_dir = None if args.no_cache else args.cache_dir
    banner_mode = "local" if args.download_banners else "remote"

    with open(args.input, encoding="utf-8") as handle:
        packs = json.load(handle)

    if isinstance(packs, dict):
        packs = packs.get("packs", [])

    total = len(packs)
    print("enriching %d packs (banners: %s)" % (total, banner_mode))

    workers = max(1, args.workers)
    done = 0
    verbose = total <= 200
    step = 1 if verbose else 50
    enriched = []

    def work(pack):
        return enrich(
            pack,
            cache_dir,
            args.banners_dir,
            args.banner_base,
            banner_mode,
        )

    with ThreadPoolExecutor(max_workers=workers) as pool:
        for pack in pool.map(work, packs):
            done += 1
            if verbose or done % step == 0 or done == total:
                meters = pack["meterRange"] or "?"
                songs = pack["songs"] if pack["songs"] is not None else "?"
                print(
                    "%5d/%-5d %-40.40s songs=%-4s meters=%-7s %s"
                    % (
                        done,
                        total,
                        pack.get("name", "?"),
                        songs,
                        meters,
                        "banner" if pack.get("banner") else "-",
                    )
                )
            enriched.append(order_fields(pack))

    packs = enriched

    missing_songs = sum(1 for p in packs if p.get("songs") is None)
    missing_meters = sum(1 for p in packs if p.get("meterMin") is None)
    with_banner = sum(1 for p in packs if p.get("banner"))

    with open(output, "w", encoding="utf-8", newline="\n") as handle:
        if args.compact:
            json.dump(packs, handle, ensure_ascii=False, separators=(",", ":"))
        else:
            json.dump(packs, handle, indent=2, ensure_ascii=False)
        handle.write("\n")

    print(
        "wrote %d packs to %s (songs missing: %d, meters missing: %d, banners: %d)"
        % (len(packs), output, missing_songs, missing_meters, with_banner)
    )


if __name__ == "__main__":
    main()
