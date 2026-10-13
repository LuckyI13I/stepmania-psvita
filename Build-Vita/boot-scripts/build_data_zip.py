"""Rebuild data.zip for the Vita VPK from the source tree.
data.zip = game data mounted read-only + extracted to ux0: on first boot.
Covers exactly what the loader needs (mirrors the historical zip):
  Announcers BGAnimations BackgroundEffects BackgroundTransitions
  Characters Courses Data NoteSkins Themes
Skips dev-only themes (Themes/_Installer, Themes/home) and Songs
(songs ship via FTP, not in the VPK).
Usage: python build_data_zip.py [workspace-root]  ->  data.zip
"""
import pathlib
import sys
import zipfile

ROOT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2]
# Workspace layout nests the tree under stepmania-5.0.12/; repo layout is flat.
TREE = ROOT / "stepmania-5.0.12" if (ROOT / "stepmania-5.0.12").is_dir() else ROOT
OUT = ROOT / "data.zip"

DIRS = ["Announcers", "BGAnimations", "BackgroundEffects",
        "BackgroundTransitions", "Characters", "Courses", "Data",
        "NoteSkins", "Themes"]
SKIP_PREFIXES = ["Themes/_Installer", "Themes/home"]


def skip(rel):
    return any(rel == p or rel.startswith(p + "/") for p in SKIP_PREFIXES)


files = []
for top in DIRS:
    base = TREE / top
    for f in sorted(base.rglob("*")):
        if f.is_file():
            rel = str(f.relative_to(TREE)).replace("\\", "/")
            if not skip(rel):
                files.append((f, rel))

tmp = OUT.with_suffix(".tmp.zip")
with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for f, rel in files:
        z.write(f, rel)
tmp.replace(OUT)
print("wrote", OUT, f"{OUT.stat().st_size/1e6:.1f} MB", len(files), "files")
