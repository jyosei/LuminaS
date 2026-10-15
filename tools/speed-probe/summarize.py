"""Summarize repeated local timings and enforce identical RAW preview pixels."""
import hashlib
import json
from pathlib import Path
import statistics
import sys

root = Path(sys.argv[1])
reports = {name: json.loads((root / filename).read_text(encoding="utf-8"))
           for name, filename in [("jpeg", "jpeg.json"), ("audit", "raw-audit.json"), ("preview", "raw-preview.json")]}
raw_runs = reports["audit"]["runs"] + reports["preview"]["runs"]
assert len({run["previewRgb32Sha256"] for run in raw_runs}) == 1
assert all(run["sourceUnchanged"] and (run["width"], run["height"]) == (2560, 3840) for run in raw_runs)
assert all(run["linearDiagnosticsEnabled"] for run in reports["audit"]["runs"])
assert all(not run["linearDiagnosticsEnabled"] for run in reports["preview"]["runs"])
summary = {"rawPreviewPixelsIdentical": True, "previewSize": [2560, 3840], "measurements": {}}
for name, report in reports.items():
    runs = report["runs"]
    timings = {}
    for key in runs[0]["timings"]:
        timings[key] = statistics.median(run["timings"][key] for run in runs)
    summary["measurements"][name] = {"runsMs": [run["wallDecodeToBufferMs"] for run in runs],
                                   "medianMs": statistics.median(run["wallDecodeToBufferMs"] for run in runs),
                                   "stageMediansMs": timings}
before, after = [summary["measurements"][mode]["medianMs"] for mode in ("audit", "preview")]
summary["rawTimeReductionPercent"] = (before - after) / before * 100
manifest = json.loads(Path("tests/fixtures/manifest.json").read_text(encoding="utf-8"))
for sample in manifest["samples"]:
    assert hashlib.sha256(Path(sample["path"]).read_bytes()).hexdigest() == sample["sha256"]
summary["originalSourceHashesUnchanged"] = True
summary["scope"] = "Same local 32MP sample; uncontrolled filesystem cache; sequential modes; no display latency/p95/target-machine claim"
(root / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
