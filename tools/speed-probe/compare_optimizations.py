"""Verify isolated OpenMP/direct-buffer/cache experiments and summarize measurements."""
import hashlib
import json
from pathlib import Path
import statistics
import sys

root=Path(sys.argv[1])
files=["official-preview", "official-direct", "openmp-1", "openmp-2", "openmp-4", "openmp-6"]
summary={"measurements": {}, "scope": "one 32MP local sample; 3 sequential runs/mode; filesystem cache uncontrolled; memory is process-lifetime high-water; no display/p95/target-machine certification"}
digests=set()
for name in files:
    report=json.loads((root/f"{name}.json").read_text(encoding="utf-8"))
    runs=report["runs"]
    assert len(runs)==3
    assert all(run["sourceUnchanged"] and (run["width"],run["height"])==(2560,3840) for run in runs)
    digests.update(run["previewRgb32Sha256"] for run in runs)
    summary["measurements"][name]={
        "runsMs": [run["wallDecodeToBufferMs"] for run in runs],
        "medianMs": statistics.median(run["wallDecodeToBufferMs"] for run in runs),
        "peakPrivateMiB": max(run["memory"]["peakPrivateCommitBytes"] for run in runs)/1048576,
        "peakWorkingSetMiB": max(run["memory"]["peakWorkingSetBytes"] for run in runs)/1048576,
        "processMedianMs": statistics.median(run["timings"]["processMs"] for run in runs)}
assert len(digests)==1
previous=json.loads(Path("build/private-qa/speed/raw-preview.json").read_text(encoding="utf-8"))
assert next(iter(digests))==previous["runs"][0]["previewRgb32Sha256"]
summary["all18RawPreviewsMatchPreviousPixels"]=True
summary["previewRgb32Sha256"]=next(iter(digests))
summary["previewSize"]=[2560,3840]
cache=json.loads((root/"cache.json").read_text(encoding="utf-8"))
assert cache["records"]["raw"]["previewRgb32Sha256"]==next(iter(digests))
assert cache["twoFrameCostBytes"] <= cache["cacheBudgetBytes"]
assert cache["sameSizeSameTimestampMutationInvalidated"] and cache["evictionVerified"]
summary["cacheExperiment"]={
    "rawHitMedianMs":statistics.median(cache["records"]["raw"]["hitMs"]),
    "jpegHitMedianMs":statistics.median(cache["records"]["jpeg"]["hitMs"]),
    "budgetMiB":cache["cacheBudgetBytes"]/1048576,
    "twoFrameCostMiB":cache["twoFrameCostBytes"]/1048576,
    "fullSourceHashOnEveryHit":True, "invalidationAndEvictionVerified":True,
    "experimentOnly":True}
for sample in json.loads(Path("tests/fixtures/manifest.json").read_text(encoding="utf-8"))["samples"]:
    assert hashlib.sha256(Path(sample["path"]).read_bytes()).hexdigest()==sample["sha256"]
summary["originalSourcesUnchanged"]=True
(root/"summary.json").write_text(json.dumps(summary,indent=2),encoding="utf-8")
print(json.dumps(summary,indent=2))
