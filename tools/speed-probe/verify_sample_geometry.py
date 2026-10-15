"""Supplement existing set timing reports with native JPEG dimensions and source hashes."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root=Path(sys.argv[1])
summary=json.loads((root/"summary.json").read_text(encoding="utf-8"))
manifest=json.loads((root/"manifest.json").read_text(encoding="utf-8"))
env=os.environ.copy()
qt_root=env.get("QT_ROOT",str(Path(".tools/Qt/6.8.3/msvc2022_64").resolve()))
env["PATH"]=str(Path(qt_root)/"bin")+os.pathsep+env["PATH"]
for sample in summary["samples"]:
    source=Path("pictures4test")/sample["jpegFile"]
    output=root/f"{source.stem}-jpeg-geometry.json"
    subprocess.run([str(Path("build/dev/speed_probe.exe").resolve()),str(source.resolve()),"jpeg",str(output.resolve()),"1"],env=env,check=True)
    report=json.loads(output.read_text(encoding="utf-8"))["runs"][0]
    assert report["normalizedSourceSize"]==sample["normalizedSize"],sample["rawFile"]
    sample["jpegNativeDimensionsVerified"]=True
    sample["jpegColorNote"]=report["colorNote"]
for source in manifest["samples"]:
    assert hashlib.sha256(Path(source["path"]).read_bytes()).hexdigest()==source["sha256"]
summary["originalSourceHashesUnchanged"]=True
(root/"summary-verified.json").write_text(json.dumps(summary,indent=2),encoding="utf-8")
print("All native JPEG/RAW dimensions and source hashes verified")
