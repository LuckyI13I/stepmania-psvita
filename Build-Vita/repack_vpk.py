"""Repack the PS Vita VPK for StepMania 5.

Stages vpk/repack_tmp (sce_sys), the freshly built eboot50.bin, the freshly
built data.zip and vpk/bootart into a zip, then writes
StepMania5-Vita50-<tag>.vpk in the workspace root.

Usage: python repack_vpk.py b51
"""
import pathlib
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent
if not (ROOT / "vpk").is_dir():
    ROOT = pathlib.Path.cwd()
TAG = sys.argv[1] if len(sys.argv) > 1 else "test"
OUT = ROOT / f"StepMania5-Vita50-{TAG}.vpk"

STAGE = ROOT / "vpk" / "repack_tmp"
EBOOT = ROOT / "eboot50.bin"
DATA = ROOT / "data.zip"
BOOTART = ROOT / "vpk" / "bootart"

missing = [str(p) for p in (STAGE, EBOOT, DATA, BOOTART) if not p.exists()]
if missing:
    sys.exit("missing: " + ", ".join(missing))

entries = [(EBOOT, "eboot.bin")]
for ext in ("*.pem", "*.png"):
    for f in sorted(STAGE.glob(ext)):
        entries.append((f, f.name))
for f in sorted((STAGE / "sce_sys").rglob("*")):
    if f.is_file():
        entries.append((f, str(f.relative_to(STAGE)).replace("\\", "/")))
for f in sorted((STAGE / "ui").rglob("*")):
    if f.is_file():
        entries.append((f, str(f.relative_to(STAGE)).replace("\\", "/")))
for f in sorted((STAGE / "fonts").rglob("*")):
    if f.is_file():
        entries.append((f, str(f.relative_to(STAGE)).replace("\\", "/")))
entries.append((DATA, "data.zip"))
for f in sorted(BOOTART.glob("*.raw")):
    entries.append((f, f"bootart/{f.name}"))

tmp = OUT.with_suffix(".tmp.vpk")
with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for src, arc in entries:
        z.write(src, arc, compresslevel=6)
tmp.replace(OUT)
print("wrote", OUT, f"{OUT.stat().st_size} bytes", len(entries), "entries")
