# Pack index format

The Vita launcher downloads the pack list from
`https://raw.githubusercontent.com/LuckyI13I/stepmania-psvita/main/packs.json`.
Two indexes are kept in this repository:

| File | Contents | Size |
| --- | --- | --- |
| `packs.json` | 30 curated, Vita-friendly packs | ~14 KB |
| `packs-all.json` | every pack in the StepMania Online catalogue | ~3.0 MB |

The launcher downloads `packs.json` when the browser opens: it is small, so the
list appears right away. `packs-all.json` is fetched only when the player starts
a search (TRIANGLE), kept in RAM for the rest of the session, and used for
global matching. SQUARE returns to the recommended list.

Bodies are capped at 8 MB, and the full catalogue is written minified
(`enrich_packs.py --compact`) so it stays far below that limit.

## Fields

```json
{
  "id": 2088,
  "name": "In The Groove",
  "size": 349129736,
  "sizeLabel": "333.0 MB",
  "date": "2026-04-25",
  "songs": 72,
  "meterRange": "1-13",
  "meterMin": 1,
  "meterMax": 13,
  "url": "https://mirror.reenigne.net/simfiles/In%20The%20Groove.zip",
  "group": "In The Groove",
  "banner": "https://stepmaniaonline.net/media/images/packs/fd64f8ff368fd98451fd6c27fcca44e02a8fb0d2.png"
}
```

| Key | Type | Meaning |
| --- | --- | --- |
| `id` | number | StepMania Online pack id |
| `name` | string | display name |
| `size` | number | archive size in bytes (used by the launcher) |
| `sizeLabel` | string | human-readable size |
| `date` | string | pack date on StepMania Online (`YYYY-MM-DD`) |
| `songs` | number \| null | number of songs in the pack |
| `meterRange` | string \| null | `min-max` chart meter, all styles combined |
| `meterMin` / `meterMax` | number \| null | same values as numbers |
| `url` | string | direct ZIP download |
| `group` | string | folder name under `Songs/`, unique per pack |
| `banner` | string \| null | direct banner image URL; `null` when the site has none |

## Regenerating

```powershell
python build_packs.py       # curated list -> packs.json (verifies every mirror)
python build_all_packs.py   # full catalogue -> packs-all.json
python enrich_packs.py --input packs.json
python enrich_packs.py --input packs-all.json --no-cache --workers 10 --compact
```

`enrich_packs.py` fetches each pack page, parses the song count from
`og:description`, the meter range from the difficulty chart labels, and the
banner from `og:image`. HTML pages are cached in `.cache/smo/` (not committed)
so re-runs are instant.

`--download-banners` switches to self-hosted, downscaled 320x100 JPEGs in
`banners/<id>.jpg` instead of linking stepmaniaonline.net directly.

## Launcher behaviour

| Action | Effect |
| --- | --- |
| Open the browser | loads `packs.json` (30 recommended packs) |
| TRIANGLE | text input; a non-empty query downloads `packs-all.json` once if needed, then shows every match in the catalogue |
| SQUARE | clears the query and returns to the recommended list |
| SELECT | re-downloads the index (and the full catalogue while searching) |
| X / tap | downloads and unpacks the selected pack into `Songs/<group>` |

Installed packs get a green dot. In the full catalogue the check is limited to
the first 500 matches to avoid hundreds of memory-card stats per search.
