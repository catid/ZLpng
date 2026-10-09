#!/usr/bin/env python3
"""Run shuffled, paired, verified API timings on an external ZRAW corpus."""
import argparse
import csv
import hashlib
import json
import os
import pathlib
import platform
import random
import statistics
import subprocess
import tempfile
import time


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def smoke_images(images):
    """Cover every represented sample format, then add distinct domains."""
    chosen, formats, domains = [], set(), set()
    for image in sorted(images, key=lambda x: x['width'] * x['height']):
        key = image['channels'], image['bits']
        if key not in formats:
            chosen.append(image)
            formats.add(key)
            domains.add(image['domain'])
    for image in images:
        if len(chosen) == 12:
            break
        if image['domain'] not in domains:
            chosen.append(image)
            domains.add(image['domain'])
    return chosen


def validate(rows, image, efforts, threads, repeats, warmups):
    assert len(rows) == len(efforts) * len(threads) + 2, 'Incomplete case coverage'
    assert {(row['codec'], int(row['effort']), int(row['threads'])) for row in rows} == {
        ('original_zpng', 0, 1), ('png', 0, 1), *(('zlpng', x, t) for x in efforts for t in threads)}
    raw = image['width'] * image['height'] * image['channels'] * image['bits'] // 8
    for row in rows:
        assert row['image_id'] == image['id'] and int(row['raw_bytes']) == raw
        assert int(row['verified']) == 1 and int(row['bytes']) > 0
        assert int(row['repeats']) == repeats and int(row['warmups']) == warmups
        for metric in ('encode', 'decode'):
            samples = json.loads(row[metric + '_ns_samples'])
            assert len(samples) == repeats and min(samples) > 0
            assert statistics.median(samples) == int(row[metric + '_ns'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/zlpng_bench')
    parser.add_argument('--reference', required=True)
    parser.add_argument('--data-root', required=True, type=pathlib.Path)
    parser.add_argument('--manifest', default='benchmarks/corpus.json', type=pathlib.Path)
    parser.add_argument('--output', default='benchmarks/results.csv', type=pathlib.Path)
    parser.add_argument('--cpus', default='120,121,122,123,124,125,126,127')
    parser.add_argument('--seed', default=20261010, type=int)
    parser.add_argument('--efforts', default='1,2,3,4,5,6,7,8')
    parser.add_argument('--threads', default='1,8')
    parser.add_argument('--repeats', default=3, type=int)
    parser.add_argument('--warmups', default=1, type=int)
    parser.add_argument('--smoke', action='store_true')
    args = parser.parse_args()
    efforts = [int(x) for x in args.efforts.split(',')]
    threads = [int(x) for x in args.threads.split(',')]
    cpus = {int(x) for x in args.cpus.split(',')}
    assert efforts and len(set(efforts)) == len(efforts) and all(1 <= x <= 8 for x in efforts)
    assert threads and len(set(threads)) == len(threads) and all(1 <= x <= min(64, len(cpus)) for x in threads)
    assert args.repeats >= 3 and args.repeats % 2 and args.warmups >= 1
    assert cpus <= os.sched_getaffinity(0), 'Requested CPU is unavailable'
    os.sched_setaffinity(0, cpus)
    binary, reference = pathlib.Path(args.binary).resolve(), pathlib.Path(args.reference).resolve()
    images = json.loads(args.manifest.read_text())['images']
    assert len({x['id'] for x in images}) == len(images), 'Duplicate image IDs'
    if args.smoke:
        images = smoke_images(images)
    random.Random(args.seed).shuffle(images)
    # Verify inputs before the measured process loop.
    for image in images:
        path = args.data_root / image['path']
        assert sha256(path) == image['sha256'], 'Corpus hash mismatch: ' + image['id']
    args.output.parent.mkdir(parents=True, exist_ok=True)
    partial = args.output.with_suffix('.partial.csv')
    metadata = dict(schema_version=1, started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        binary_sha256=sha256(binary), reference_sha256=sha256(reference),
        manifest_sha256=sha256(args.manifest), cpus=sorted(cpus), serial_cpu=max(cpus), threads=threads, platform=platform.platform(),
        machine=platform.machine(), seed=args.seed, efforts=efforts, repeats=args.repeats,
        warmups=args.warmups, images=len(images), image_order=[x['id'] for x in images],
        protocol='One process per image; shuffled cases each pass; serial cases pinned to highest selected CPU, parallel cases allowed selected CPU set; median API wall time; all warmup and timed decodes verified; file I/O, affinity changes and verification excluded.')
    metadata.update(json.loads(subprocess.check_output([str(binary), '--versions'], text=True)))
    metadata['cpu_topology'] = []
    for cpu in sorted(cpus):
        topology = pathlib.Path(f'/sys/devices/system/cpu/cpu{cpu}/topology')
        metadata['cpu_topology'].append(dict(cpu=cpu,
            core=int((topology / 'core_id').read_text()),
            socket=int((topology / 'physical_package_id').read_text())))
    for line in pathlib.Path('/proc/cpuinfo').read_text().splitlines():
        if line.startswith('model name'):
            metadata['cpu_model'] = line.split(':', 1)[1].strip()
            break
    started = time.monotonic()
    count = 0
    with tempfile.TemporaryDirectory(prefix='zlpng-bench-') as temp, partial.open('w') as out:
        writer = None
        for index, image in enumerate(images):
            rows_path = pathlib.Path(temp) / 'rows.csv'
            command = [str(binary), str(args.data_root / image['path']), '--reference', str(reference),
                '--output', str(rows_path), '--image-id', image['id'], '--efforts', args.efforts, '--threads', args.threads,
                '--repeats', str(args.repeats), '--warmups', str(args.warmups),
                '--seed', str((args.seed + index) & 0xffffffff)]
            subprocess.run(command, check=True)
            with rows_path.open() as rows_file:
                rows = list(csv.DictReader(rows_file))
            validate(rows, image, efforts, threads, args.repeats, args.warmups)
            if writer is None:
                writer = csv.DictWriter(out, fieldnames=list(rows[0]))
                writer.writeheader()
            writer.writerows(rows)
            out.flush()
            count += len(rows)
            print(f'{index + 1}/{len(images)} {image["id"]} {time.monotonic() - started:.1f}s', flush=True)
    assert sha256(binary) == metadata['binary_sha256'], 'Binary changed during benchmark'
    assert sha256(reference) == metadata['reference_sha256'], 'Reference changed during benchmark'
    assert sha256(args.manifest) == metadata['manifest_sha256'], 'Manifest changed during benchmark'
    partial.replace(args.output)
    metadata.update(completed_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        elapsed_seconds=time.monotonic() - started, rows=count,
        verified_roundtrips=count * (args.repeats + args.warmups), results_sha256=sha256(args.output))
    args.output.with_suffix('.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(f'Saved {count} verified results to {args.output}')


if __name__ == '__main__':
    main()
