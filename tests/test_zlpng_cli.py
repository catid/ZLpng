#!/usr/bin/env python3
"""Lossless CLI integration checks for every channel/depth combination."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/zlpng")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    cases = 0
    with tempfile.TemporaryDirectory(prefix="zlpng-cli-test-") as directory:
        temporary = Path(directory)

        def roundtrip(source):
            nonlocal cases
            for effort in (1, 8):
                encoded, restored = temporary / "encoded.zlp", temporary / "restored.zraw"
                for command in ([str(args.binary), "compress", str(source), str(encoded), "--effort", str(effort)],
                                [str(args.binary), "decompress", str(encoded), str(restored)]):
                    subprocess.run(command, check=True, capture_output=True, text=True)
                assert source.read_bytes() == restored.read_bytes()
                assert encoded.read_bytes().startswith(b"ZLP2")
                cases += 1

        for channels in range(1, 5):
            for bits in (8, 16):
                source = temporary / f"c{channels}b{bits}.zraw"
                raw = bytes((i * 17 + i // 5) % 256 for i in range(33 * 5 * channels * (bits // 8)))
                source.write_bytes(b"ZRAW" + struct.pack("<4I", 33, 5, channels, bits) + raw)
                roundtrip(source)
        bad = temporary / "bad.zlp"
        bad.write_bytes(b"ZLP2")
        rejected = subprocess.run([str(args.binary), "decompress", str(bad), str(temporary / "bad.zraw")], capture_output=True)
        assert rejected.returncode != 0 and not (temporary / "bad.zraw").exists()
    result = dict(cli_roundtrips=cases, rejected_truncated_stream=True,
                  binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest())
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
