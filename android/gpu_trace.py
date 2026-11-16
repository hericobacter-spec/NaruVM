#!/usr/bin/env python3
"""Read the bounded PanVK trace; timings are host API durations, not GPU timestamps."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import struct

HEADER = struct.Struct('<4Q')
EVENT = struct.Struct('<8QqQ')
KINDS = {1:'allocate', 2:'free', 3:'submit', 4:'wait', 5:'idle',
         6:'pipeline', 7:'unmap', 8:'timeline', 9:'counter'}

def read_trace(path):
    data = Path(path).read_bytes()
    if len(data) < HEADER.size:
        raise ValueError('Truncated trace header')
    magic, stride, slots, pid = HEADER.unpack_from(data)
    if magic != 0x3145434152545048 or stride != EVENT.size or not 1 <= slots <= 8192:
        raise ValueError('Unsupported trace header')
    if len(data) != HEADER.size + slots * stride:
        raise ValueError('Truncated trace slots')
    rows = []
    torn = 0
    for i in range(slots):
        row = EVENT.unpack_from(data, HEADER.size + i * stride)
        if row[0] == 0:
            continue
        if row[0] != row[-1] or (row[0]-1) % slots != i:
            torn += 1
            continue
        rows.append(row)
    rows.sort()
    pending = {}
    durations = defaultdict(list)
    errors = []
    counts = Counter()
    for seq, ns, thread, kind, phase, obj, a, b, result, _ in rows:
        counts[KINDS.get(kind, str(kind))] += 1
        if phase == 1:
            pending[seq] = (ns, kind, obj)
        elif phase == 2:
            begin = pending.pop(a, None)
            if begin and begin[1:] == (kind, obj):
                durations[KINDS.get(kind, str(kind))].append((ns-begin[0])/1e6)
            if result < 0:
                errors.append(dict(sequence=seq, kind=KINDS.get(kind), result=result))
    summary = {}
    for kind, values in durations.items():
        values.sort()
        summary[kind] = dict(calls=len(values), total_ms=sum(values),
                             p50_ms=values[(len(values)-1)//2],
                             p95_ms=values[(95*len(values)+99)//100-1], max_ms=values[-1])
    return dict(pid=pid, retained_events=len(rows), first_sequence=rows[0][0] if rows else None,
                last_sequence=rows[-1][0] if rows else None, torn_slots=torn,
                counts=counts, host_api_ms=summary, errors=errors,
                unmatched_begins=len(pending), tail=[dict(sequence=r[0], ns=r[1],
                kind=KINDS.get(r[3]), phase=r[4], object=hex(r[5]), a=r[6], b=r[7], result=r[8])
                for r in rows[-24:]],
                limits='Circular tail only; API timings include tracing overhead. Unmatched begins may be in-flight; no resource-use causality inferred.')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    args = parser.parse_args()
    print(json.dumps(read_trace(args.trace), indent=2))
