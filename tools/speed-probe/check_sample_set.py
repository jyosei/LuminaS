"""Read-only local JPEG/CR3 set comparison; no image uploads or output overwrites."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument("directory",type=Path)
parser.add_argument("output",type=Path)
parser.add_argument("--repetitions",type=int,default=3)
args=parser.parse_args()
if not 1 <= args.repetitions <= 10:
    parser.error("repetitions must be 1..10")
args.output.mkdir(parents=True,exist_ok=False)
env=os.environ.copy()
env["PATH"]=str(Path(".tools/Qt/6.8.3/msvc2022_64/bin").resolve())+os.pathsep+env["PATH"]
env["OMP_NUM_THREADS"]="4"
env["OMP_WAIT_POLICY"]="PASSIVE"
files=sorted(p for p in args.directory.rglob("*") if p.is_file())
def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream,"sha256").hexdigest()
before={str(p):digest(p) for p in files}
samples=[]
manifest=[]
for raw in (p for p in files if p.suffix.lower()==".cr3"):
    paired=next((p for p in files if p.stem==raw.stem and p.suffix.lower() in (".jpg",".jpeg")),None)
    modes=[("baseline",Path("build/dev/speed_probe.exe"),raw,"preview"),
           ("candidate",Path("build/openmp/RelWithDebInfo/speed_probe.exe"),raw,"direct")]
    if paired:
        modes.append(("jpeg",Path("build/dev/speed_probe.exe"),paired,"jpeg"))
    reports={}
    for label,program,source,mode in modes:
        output=args.output/f"{raw.stem}-{label}.json"
        print(f"CHECK {raw.stem} {label}",flush=True)
        subprocess.run([str(program.resolve()),str(source.resolve()),mode,str(output.resolve()),str(args.repetitions)],env=env,check=True)
        reports[label]=json.loads(output.read_text(encoding="utf-8"))
    base=reports["baseline"]["runs"]
    candidate=reports["candidate"]["runs"]
    assert len({r["previewRgb32Sha256"] for r in base+candidate})==1, f"Pixel mismatch: {raw.name}"
    assert all(r["sourceUnchanged"] for r in base+candidate)
    assert all(max(r["width"],r["height"])==3840 for r in base+candidate)
    base_ms=statistics.median(r["wallDecodeToBufferMs"] for r in base)
    fast_ms=statistics.median(r["wallDecodeToBufferMs"] for r in candidate)
    metadata=base[0]["metadata"]
    linear_size=[base[0]["preview"]["width"],base[0]["preview"]["height"]]
    # original size remains in crop metadata, with LibRaw's orientation swap.
    crop=base[0]["crop"]
    normalized=[crop["width"],crop["height"]]
    if metadata["flip"] & 4:
        normalized.reverse()
    record={"rawFile":raw.name,"camera":metadata["model"],"normalizedSize":normalized,
            "previewSize":linear_size,"rawFlip":metadata["flip"],"pixelsIdentical":True,
            "baselineMedianMs":base_ms,"candidateMedianMs":fast_ms,
            "timeReductionPercent":(base_ms-fast_ms)/base_ms*100,
            "baselinePeakPrivateMiB":max(r["memory"]["peakPrivateCommitBytes"] for r in base)/1048576,
            "candidatePeakPrivateMiB":max(r["memory"]["peakPrivateCommitBytes"] for r in candidate)/1048576}
    if paired:
        jpg=reports["jpeg"]["runs"]
        assert all([r["width"],r["height"]]==linear_size for r in jpg),f"JPEG/RAW geometry differs: {raw.name}"
        assert all(r["normalizedSourceSize"]==normalized for r in jpg),f"Full JPEG/RAW dimensions differ: {raw.name}"
        record["jpegFile"]=paired.name
        record["jpegMedianMs"]=statistics.median(r["wallDecodeToBufferMs"] for r in jpg)
    samples.append(record)
    for path in (raw,paired):
        if path is None: continue
        entry={"path":path.as_posix(),"bytes":path.stat().st_size,"sha256":before[str(path)],
               "camera":"Canon EOS R7 (user confirmed)","normalizedSize":normalized,"expectedPreview":linear_size,
               "role":"local RAW/JPEG regression; not redistribution or colorimetric ground truth"}
        if path==raw:
            entry.update(rawSensorSize=[metadata["rawWidth"],metadata["rawHeight"]],
                         visibleBeforeInsetCrop=[metadata["visibleWidthBeforeCrop"],metadata["visibleHeightBeforeCrop"]],
                         vendorInsetCrop=[crop["width"],crop["height"]],flip=metadata["flip"])
        manifest.append(entry)
    print(f"PASS {raw.name}: {base_ms} -> {fast_ms} ms; pixels match",flush=True)
after={str(p):digest(p) for p in files}
assert before==after,"Original sample changed"
summary={"samples":samples,"originalSourceHashesUnchanged":True,"rawSampleCount":len(samples),
         "fileCount":len(files),"repetitionsPerModePerRaw":args.repetitions,
         "scope":"same R7 local set; 4-thread OpenMP/direct candidate vs official reference; OS cache uncontrolled; no screen latency/p95/45MP certification"}
(args.output/"summary.json").write_text(json.dumps(summary,indent=2),encoding="utf-8")
manifest_doc={"scope":"local R7 expanded S1 regression manifest; pictures ignored",
              "permission":"User authorized local tests; no upload/redistribution inferred",
              "samples":manifest,"missing":["fixed quality reference/RAW editing validation","representative target-machine sustained-load tests"],
              "unverified":["real near-45MP CR3; user says not required to block current R7 work"]}
(args.output/"manifest.json").write_text(json.dumps(manifest_doc,indent=2),encoding="utf-8")
print("ALL SAMPLES PASS",flush=True)
