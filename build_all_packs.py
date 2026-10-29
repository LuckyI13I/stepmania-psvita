#!/usr/bin/env python3
"""Build a packs.json index containing the entire StepMania Online catalogue.

The StepMania 5 Vita launcher downloads the index into a 4 MB buffer, so this
script prints the resulting size and warns when the index would not fit.

Usage:
    python build_all_packs.py [--output packs-all.json] [--verify]
                              [--max-size-mb N] [--types dance,pump]
"""

import argparse
import html
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed

API_URL = "https://stepmaniaonline.net/api/packs/datatables"
PACK_URL = "https://stepmaniaonline.net/download/pack/{pack_id}/"
USER_AGENT = "StepMania5-Vita/1.0 (+packs.json builder)"
INDEX_LIMIT = 4 * 1024 * 1024

TAG_RE = re.compile(r"<[^>]+>")
PACK_HREF_RE = re.compile(r'href="/pack/(\d+)"')
SIZE_RE = re.compile(r'data-sort="(\d+)"')
TYPE_RE = re.compile(r'alt="([^"]+)"')


def fetch_json(url, attempts=3):
    last_error = None
    for attempt in range(attempts):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(request, timeout=60) as response:
                return json.load(response)
        except Exception as error:
            last_error = error
            if attempt + 1 < attempts:
                time.sleep(2 * (attempt + 1))
    raise RuntimeError("cannot fetch %s: %s" % (url, last_error))


def query_page(start, length=500):
    query = urllib.parse.urlencode(
        {"draw": start // length + 1, "start": start, "length": length}, safe="[]"
    )
    return fetch_json(API_URL + "?" + query)


def fetch_catalogue():
    first = query_page(0)
    total = int(first.get("recordsTotal") or 0)
    rows = list(first.get("data") or [])
    while len(rows) < total:
        page = query_page(len(rows))
        data = page.get("data") or []
        if not data:
            break
        rows.extend(data)
        print("fetched %d / %d" % (len(rows), total), file=sys.stderr)
    return rows, total


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
        "types": TYPE_RE.findall(row[4]),
    }


def human_size(size):
    if size >= 1024 ** 3:
        return "%.1f GB" % (size / 1024 ** 3)
    if size >= 1024 ** 2:
        return "%.1f MB" % (size / 1024 ** 2)
    if size >= 1024:
        return "%.0f KB" % (size / 1024)
    return "%d B" % size


def sanitize(value):
    out = "".join(
        char
        if char.isascii() and (char.isalnum() or char in " -_.")
        else "_"
        for char in value
    )
    out = out.strip(" .")
    return out[:96] or "Pack"


def make_group(label, pack_id, used):
    group = sanitize(label)
    if group in used:
        group = sanitize("%s [%d]" % (label, pack_id))
    base = group
    counter = 2
    while group in used:
        group = sanitize("%s %d" % (base, counter))
        counter += 1
    used.add(group)
    return group


def check_link(entry):
    try:
        request = urllib.request.Request(
            entry["url"], method="HEAD", headers={"User-Agent": USER_AGENT}
        )
        with urllib.request.urlopen(request, timeout=60) as response:
            return (
                entry,
                response.status,
                response.headers.get("Content-Type", ""),
                int(response.headers.get("Content-Length") or 0),
            )
    except urllib.error.HTTPError as error:
        return entry, error.code, "", 0
    except Exception as error:
        return entry, "ERR", type(error).__name__, 0


def verify_entries(entries, workers):
    verified = []
    failed = []
    done = 0
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(check_link, entry) for entry in entries]
        for future in as_completed(futures):
            entry, status, content_type, length = future.result()
            done += 1
            if status == 200 and "zip" in content_type.lower():
                if length > 0:
                    entry["size"] = length
                    entry["sizeLabel"] = human_size(length)
                verified.append(entry)
            else:
                failed.append((entry["name"], status, content_type))
            if done % 500 == 0:
                print("checked %d / %d" % (done, len(entries)), file=sys.stderr)
    return verified, failed


def build(args):
    rows, total = fetch_catalogue()
    packs = [pack for pack in (parse_row(row) for row in rows) if pack]
    print("parsed %d / %d packs" % (len(packs), total), file=sys.stderr)

    if args.types:
        wanted = {value.strip().lower() for value in args.types.split(",") if value.strip()}
        packs = [pack for pack in packs if wanted.intersection(pack["types"])]
    if args.max_size_mb:
        limit = args.max_size_mb * 1024 * 1024
        packs = [pack for pack in packs if pack["size"] <= limit]

    name_counts = Counter(pack["name"] for pack in packs)
    for pack in packs:
        if name_counts[pack["name"]] > 1:
            pack["label"] = "%s [%d]" % (pack["name"], pack["id"])
        else:
            pack["label"] = pack["name"]

    packs.sort(key=lambda pack: (pack["label"].casefold(), pack["id"]))

    used_groups = set()
    entries = []
    for pack in packs:
        entries.append(
            {
                "id": pack["id"],
                "name": pack["label"],
                "size": pack["size"],
                "sizeLabel": human_size(pack["size"]),
                "date": pack["date"],
                "url": PACK_URL.format(pack_id=pack["id"]),
                "group": make_group(pack["label"], pack["id"], used_groups),
            }
        )

    if args.verify:
        entries, failed = verify_entries(entries, args.workers)
        for name, status, content_type in failed[:20]:
            print("dead link: %s (%s %s)" % (name, status, content_type), file=sys.stderr)
        print(
            "verified %d packs, %d dead links" % (len(entries), len(failed)),
            file=sys.stderr,
        )

    if len(entries) < 20:
        raise SystemExit("only %d usable packs" % len(entries))

    return entries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="packs-all.json")
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--workers", type=int, default=16)
    parser.add_argument("--max-size-mb", type=int, default=0)
    parser.add_argument("--types", default="")
    args = parser.parse_args()

    entries = build(args)

    with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(entries, handle, indent=2, ensure_ascii=False)
        handle.write("\n")

    import os

    output_size = os.path.getsize(args.output)
    total_size = sum(entry["size"] for entry in entries)
    print(
        "wrote %d packs, %.2f MB index, %.2f TB archives to %s"
        % (len(entries), output_size / 1024 ** 2, total_size / 1024 ** 4, args.output)
    )
    if output_size >= INDEX_LIMIT:
        print(
            "WARNING: index is larger than the launcher's 4 MB download buffer",
            file=sys.stderr,
        )


if __name__ == "__main__":
    main()
