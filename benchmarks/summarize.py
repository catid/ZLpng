#!/usr/bin/env python3
"""Summarize complete-image timings and render the current codec comparison."""
import argparse
import collections
import csv
import hashlib
import json
import math
import pathlib
import statistics


SPECIALIZED = {'medical', 'microscopy', 'astronomy', 'sensor_cfa',
               'multiband_scientific', 'materials'}


def image_type(image):
    if image['domain'].startswith('synthetic') or image['source'].startswith('generated:'):
        return 'synthetic'
    return 'specialized' if image['domain'] in SPECIALIZED else 'real'


def geometric(values):
    values = list(values)
    return math.exp(sum(math.log(x) for x in values) / len(values))


def load_results(path, manifest):
    metadata = json.loads(path.with_suffix('.json').read_text())
    assert metadata['results_sha256'] == hashlib.sha256(path.read_bytes()).hexdigest(), 'Result hash mismatch'
    assert metadata['manifest_sha256'] == hashlib.sha256(manifest.read_bytes()).hexdigest(), 'Manifest hash mismatch'
    for key in ('binary_sha256', 'reference_sha256'):
        assert len(metadata[key]) == 64 and all(c in '0123456789abcdef' for c in metadata[key]), 'Missing executable hash'
    images = {image['id']: image for image in json.loads(manifest.read_text())['images']}
    rows = list(csv.DictReader(path.open()))
    assert len(rows) == metadata['rows'], 'Metadata row count mismatch'
    seen = set()
    by_image = collections.defaultdict(dict)
    for row in rows:
        for key in ('effort', 'threads', 'bytes', 'raw_bytes', 'encode_ns', 'decode_ns', 'verified'):
            row[key] = int(row[key])
        key = row['codec'], row['effort'], row['threads']
        assert (row['image_id'], key) not in seen, 'Duplicate result'
        seen.add((row['image_id'], key))
        assert row['verified'] == 1 and min(row['bytes'], row['encode_ns'], row['decode_ns']) > 0
        assert int(row['warmups']) == metadata['warmups'] and int(row['repeats']) == metadata['repeats']
        for metric in ('encode', 'decode'):
            values = json.loads(row[metric + '_ns_samples'])
            assert len(values) == metadata['repeats'] and min(values) > 0
            assert statistics.median(values) == row[metric + '_ns'], 'Sample median mismatch'
        shape = images[row['image_id']]
        assert row['raw_bytes'] == shape['width'] * shape['height'] * shape['channels'] * shape['bits'] // 8
        row['domain'] = images[row['image_id']]['domain']
        row['type'] = image_type(images[row['image_id']])
        row['split'] = images[row['image_id']]['split']
        by_image[row['image_id']][key] = row
    coverage = [set(cases) for cases in by_image.values()]
    assert coverage and all(x == coverage[0] for x in coverage), 'Inconsistent case coverage'
    assert ('png', 0, 1) in coverage[0] and ('original_zpng', 0, 1) in coverage[0]
    assert len(by_image) == metadata['images'] and set(by_image) == set(metadata['image_order'])
    assert coverage[0] == {('png', 0, 1), ('original_zpng', 0, 1),
        *(('zlpng', effort, threads) for effort in metadata['efforts'] for threads in metadata['threads'])}
    assert metadata['verified_roundtrips'] == len(rows) * (metadata['warmups'] + metadata['repeats'])
    for cases in by_image.values():
        assert len({row['raw_bytes'] for row in cases.values()}) == 1
        for row in cases.values():
            row['png'] = cases['png', 0, 1]
            row['original_zpng'] = cases['original_zpng', 0, 1]
    return rows, images, by_image


def metrics(rows):
    raw, packed = sum(x['raw_bytes'] for x in rows), sum(x['bytes'] for x in rows)
    encode, decode = sum(x['encode_ns'] for x in rows), sum(x['decode_ns'] for x in rows)
    result = dict(images=len(rows), raw_bytes=raw, compressed_bytes=packed,
        compression_ratio=raw / packed, compressed_raw_pct=100 * packed / raw,
        encode_ms=encode / len(rows) / 1e6, decode_ms=decode / len(rows) / 1e6,
        encode_mbps=raw * 1e3 / encode, decode_mbps=raw * 1e3 / decode)
    for reference in ('original_zpng', 'png'):
        result[reference + '_geomean_size_ratio'] = geometric(x['bytes'] / x[reference]['bytes'] for x in rows)
        result[reference + '_geomean_savings_pct'] = 100 * (1 - result[reference + '_geomean_size_ratio'])
        result[reference + '_pooled_savings_pct'] = 100 * (1 - packed / sum(x[reference]['bytes'] for x in rows))
        result[reference + '_smaller_images'] = sum(x['bytes'] < x[reference]['bytes'] for x in rows)
        result[reference + '_encode_speedup'] = sum(x[reference]['encode_ns'] for x in rows) / encode
        result[reference + '_decode_speedup'] = sum(x[reference]['decode_ns'] for x in rows) / decode
    return result


def make_plot(summary, path):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D
    plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 10,
                         'svg.fonttype': 'none', 'axes.spines.top': False,
                         'axes.spines.right': False})
    colors = {'png': '#a16f26', 'original_zpng': '#64748b', 1: '#147d92', 8: '#da4b54'}
    cohorts = [('all', 'all', 'All images'), ('type', 'real', 'Real images'),
               ('type', 'synthetic', 'Synthetic images'), ('type', 'specialized', 'Specialized images')]
    fig, axes = plt.subplots(4, 3, figsize=(14, 11), constrained_layout=False)
    measures = [('png_geomean_size_ratio', 'File size vs PNG (%)', 'lower'),
                ('encode_mbps', 'Encode throughput (MB/s)', 'higher'),
                ('decode_mbps', 'Decode throughput (MB/s)', 'higher')]
    for row_index, (group, value, title) in enumerate(cohorts):
        data = [r for r in summary['metrics'] if r['group'] == group and r['value'] == value]
        for column, (metric, label, better) in enumerate(measures):
            axis = axes[row_index, column]
            for codec in ('png', 'original_zpng'):
                r = next(x for x in data if x['codec'] == codec)
                y = r[metric] * (100 if column == 0 else 1)
                axis.axhline(y, color=colors[codec], linewidth=1.6, linestyle=':')
            for threads in sorted({x['threads'] for x in data if x['codec'] == 'zlpng'}):
                points = sorted((x for x in data if x['codec'] == 'zlpng' and x['threads'] == threads),
                                key=lambda x: x['effort'])
                axis.plot([x['effort'] for x in points],
                          [x[metric] * (100 if column == 0 else 1) for x in points],
                          color=colors.get(threads, '#da4b54'), linewidth=2,
                          marker='o', markersize=4, linestyle='-' if threads == 1 else '--')
            axis.set_title(f'{title} · {data[0]["images"]} images' if column == 0 else title,
                           loc='left', fontsize=11, fontweight='bold')
            axis.set_ylabel(label)
            axis.grid(axis='y', alpha=.18)
            axis.set_xticks(summary['efforts'])
            axis.set_xlim(min(summary['efforts']) - .25, max(summary['efforts']) + .25)
            axis.set_ylim(bottom=0)
            if row_index == 3:
                axis.set_xlabel('ZLpng compression effort')
            if column == 0:
                axis.text(.98, .93, 'smaller is better', transform=axis.transAxes,
                          ha='right', fontsize=8, color='#475569')
    legend = [Line2D([0], [0], color=colors[1], marker='o', label='ZLpng · 1 thread'),
              Line2D([0], [0], color=colors[8], marker='o', linestyle='--', label='ZLpng · up to 8 threads'),
              Line2D([0], [0], color=colors['original_zpng'], linestyle=':', label='Original ZPNG · 1 thread'),
              Line2D([0], [0], color=colors['png'], linestyle=':', label='PNG level 6 · 1 thread')]
    fig.suptitle('Lossless image compression by image type', x=.06, y=.985,
                 ha='left', fontsize=20, fontweight='bold')
    fig.legend(handles=legend, loc='upper left', bbox_to_anchor=(.055, .96), ncol=4, frameon=False)
    fig.text(.06, .015,
        'Size: geometric mean per-image ratio to PNG. Throughput: total raw bytes / sum of per-image median times.\n'
        'All complete files and decoded pixels verified; 1 warmup + 3 timed passes. Parallel results use eight physical cores.',
        fontsize=9, color='#475569')
    fig.subplots_adjust(left=.065, right=.985, top=.895, bottom=.09, hspace=.49, wspace=.29)
    fig.savefig(path, format='svg', metadata={'Date': None})
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', default='benchmarks/results.csv', type=pathlib.Path)
    parser.add_argument('--manifest', default='benchmarks/corpus.json', type=pathlib.Path)
    parser.add_argument('--output', default='benchmarks/summary.csv', type=pathlib.Path)
    parser.add_argument('--plot', default='benchmarks/performance.svg', type=pathlib.Path)
    parser.add_argument('--allow-subset', action='store_true')
    args = parser.parse_args()
    rows, images, by_image = load_results(args.results, args.manifest)
    assert args.allow_subset or set(by_image) == set(images), 'Results do not cover the complete corpus'
    groups = [('all', 'all')] + [(key, value) for key in ('type', 'domain', 'split')
        for value in sorted({row[key] for row in rows})]
    entries = []
    for group, value in groups:
        cohort = [row for row in rows if group == 'all' or row[group] == value]
        cases = collections.defaultdict(list)
        for row in cohort:
            cases[row['codec'], row['effort'], row['threads']].append(row)
        for (codec, effort, threads), members in sorted(cases.items()):
            entries.append(dict(group=group, value=value, codec=codec, effort=effort,
                                threads=threads, **metrics(members)))
    summary = dict(schema_version=1, images=len(by_image), rows=len(rows),
        raw_bytes=sum(next(iter(cases.values()))['raw_bytes'] for cases in by_image.values()),
        results_sha256=hashlib.sha256(args.results.read_bytes()).hexdigest(),
        aggregation='Size savings: geometric mean of complete-file size ratios, one vote per image. Compression ratio and throughput: pooled bytes and sum of per-image median API times. Milliseconds: arithmetic mean of image medians. Splits are previously inspected exploratory cohorts.',
        image_types='Synthetic includes synthetic domains or generated sources. Specialized includes medical, microscopy, astronomy, sensor CFA, multiband scientific and materials. Remaining photographs, graphics, text, textures and alpha images are real.',
        efforts=sorted({row['effort'] for row in rows if row['codec'] == 'zlpng'}),
        threads=sorted({row['threads'] for row in rows if row['codec'] == 'zlpng'}),
        metrics=entries)
    with args.output.open('w') as output:
        writer = csv.DictWriter(output, fieldnames=list(entries[0]))
        writer.writeheader()
        writer.writerows(entries)
    make_plot(summary, args.plot)
    print(f'Summarized {len(rows)} rows / {len(by_image)} images; wrote {args.output} and {args.plot}')


if __name__ == '__main__':
    main()
