"""Verify experimental PPM16 bytes independently of LibRaw/Qt; no photo upload."""
import array
import hashlib
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
summary = {}
for algorithm in ("ahd", "dcb"):
    report = json.loads((root / f"{algorithm}.json").read_text(encoding="utf-8"))
    path = root / f"{algorithm}-linear16.ppm"
    native_hash = hashlib.sha256()
    file_hash = hashlib.sha256()
    with path.open("rb") as stream:
        magic, dimensions, maxval = stream.readline(), stream.readline(), stream.readline()
        assert magic == b"P6\n" and maxval == b"65535\n"
        width, height = map(int, dimensions.split())
        assert [width, height] == [report["linearRgb16"]["width"], report["linearRgb16"]["height"]]
        file_hash.update(magic + dimensions + maxval)
        length = 0
        while block := stream.read(1024 * 1024):
            assert len(block) % 2 == 0
            length += len(block)
            file_hash.update(block)
            samples = array.array("H")
            samples.frombytes(block)
            if sys.byteorder == "little":
                samples.byteswap()
            native_hash.update(samples.tobytes())
        assert length == width * height * 3 * 2
    assert native_hash.hexdigest() == report["linearRgb16"]["sha256"]
    assert file_hash.hexdigest() == report["linearFileSha256"]
    summary[algorithm] = {"ppm16Valid": True, "bigEndianSamplesMatchLibRawBuffer": True,
                          "width": width, "height": height, "payloadBytes": length,
                          "linearFileSha256": file_hash.hexdigest()}
assert summary["ahd"]["linearFileSha256"] != summary["dcb"]["linearFileSha256"]
(root / "ppm-verification.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
