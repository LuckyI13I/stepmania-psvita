"""Pack StepMania5-Vita50 VPK from canonical inputs.
Sources (all paths absolute, repo-friendly):
  eboot50.bin                          <- fresh vita-make-fself output
  stepmania-5.0.12/Build-Vita/sce_sys/  <- icon/livearea/param.sfo (STITLE locked)
  StepMania5-Vita50.vpk:data.zip       <- game data (static until data rebuild)
  vpk/bootart/*                        <- boot splash art (build-bootart-assets.py)
Usage: python repack_vpk.py b21
"""
import shutil, sys, zipfile, pathlib

root = pathlib.Path(r"C:\Users\troia\Desktop\Work\StepMania 5 PS VITA")
tag = sys.argv[1] if len(sys.argv) > 1 else "b21"

tmp = root / "vpk" / "repack_tmp"
if tmp.exists():
    shutil.rmtree(tmp)
tmp.mkdir(parents=True)

shutil.copyfile(root / "eboot50.bin", tmp / "eboot.bin")
shutil.copytree(root / "stepmania-5.0.12" / "Build-Vita" / "sce_sys", tmp / "sce_sys")
with zipfile.ZipFile(root / "StepMania5-Vita50.vpk") as z:
    z.extract("data.zip", tmp)

out = root / ("StepMania5-Vita50-%s.vpk" % tag)
if out.exists():
    out.unlink()
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=1) as z:
    z.write(tmp / "eboot.bin", "eboot.bin")
    for f in sorted((tmp / "sce_sys").rglob("*")):
        if f.is_file():
            z.write(f, "sce_sys/" + str(f.relative_to(tmp / "sce_sys")).replace("\\", "/"))
    z.write(tmp / "data.zip", "data.zip")
    for f in sorted((root / "vpk" / "bootart").glob("*")):
        if f.is_file():
            z.write(f, "bootart/" + f.name)
print("wrote", out, out.stat().st_size)
