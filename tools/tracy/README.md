# tracy-query

Reads Tracy `.tracy` captures directly in Python. No Tracy build, no compiled dependency, no third-party packages — decompression uses the `compression.zstd` stdlib module, so it needs Python 3.14+.

Written against Tracy **0.13.x**. The trace format is version-locked and not self-describing, so a Tracy upgrade may require revisiting `tracefile.py`.

## Commands

```bash
python3 tools/tracy/cli.py <trace.tracy> <command>
```

| Command | Purpose |
| --- | --- |
| `info` | Capture metadata, threads, GPU contexts, frame sets, frame number range |
| `frames [--set N] [--slowest N]` | Frame time distribution and the slowest frames |
| `frame <number>` | Full zone tree for one frame, all threads plus GPU |
| `zones [--top N] [--self] [--gpu] [--filter S]` | Aggregate statistics for every zone |
| `zone <name> [--gpu]` | count / total / self / mean / median / min / max / p90 / p99 / stddev |
| `validate` | Structural invariant check on the parse |

Frame numbers are the ones the viewer displays, which are offset by `frameOffset`; `info` prints the valid range. `--index` addresses a frame by raw index instead.

`frame` prints an indented tree by default and takes `--min-us` and `--max-depth` to keep output small; `--json` emits the same tree as nested objects. Note that `selfUs` always subtracts *all* children, including any hidden by `--min-us`.

`--under NAME` restricts output to subtrees rooted at matching zones, without descending into a match, and `--flat` aggregates their descendants by zone instead of printing the tree — the usual way to ask "where did this 200 µs go":

```bash
frame 21234 --under arrange --flat
frame 21234 --under "draw@window.cpp:60" --flat
```

`--under` matches a name substring, or a zone exactly with `name@file.cpp:line`. Use the qualified form when a name like `draw` appears on many components.

## Design

`tracefile.parse(path, visitor)` walks the timeline and pushes zones to a visitor rather than materializing them. A capture with a million zones would cost hundreds of megabytes as Python objects, so `StatsCollector` accumulates counters online and `FrameCollector` retains only zones overlapping one frame's time window. A full parse of a 4.5 MB / 26 MB-decompressed capture takes about half a second.

- `container.py` — block framing and zstd decompression
- `reader.py` — little-endian cursor
- `tracefile.py` — section parser, transcribed from `Worker::Worker( FileRead& )`
- `query.py` — frame extraction, statistics, validation visitors
- `cli.py` — argument parsing and output formatting

## Format notes

Hard-won details, all verified against `server/` in the Tracy source:

**Container.** 6-byte header (`"tr" 0xFD 'P'`, type, stream count), then repeating `u32 blockSize` + compressed bytes. Block `i` feeds stream `i % streamCount`. Each stream is one continuous zstd frame written with `ZSTD_e_flush`, so decompressor state carries across that stream's blocks. The logical payload is the decompressed blocks concatenated in file order.

**Everything in `TracyEvent.hpp` between lines 21 and 724 is inside `#pragma pack(push, 1)`.** `StringRef` is 9 bytes, `SourceLocationBase` is 35, `CrashEvent` is 28 — not the 16/56/32 that natural alignment would give. Getting this wrong desynchronizes the whole stream.

**Source location ids.** Non-negative ids index `sourceLocationExpand`, which yields a pointer into the static location map. Negative ids address the dynamic payload as `-srcloc-1`. `INT16_MAX` means empty.

**`StringRef` resolution.** When `isidx` is set, `str` indexes `stringData`. Otherwise it is a key into the id-to-text `strings` map — *not* a raw pointer into the string table.

**Zone timelines are delta-encoded depth-first.** Timestamps accumulate into a `refTime` threaded through the recursion by value and returned. Each iteration's tail read is fused with the next iteration's head, and the final element's tail is a lone `i64`. A single wrong field width yields plausible-looking but wrong timestamps rather than an error, which is why `validate` exists.

**Negative GPU timestamps mean an unresolved query,** not a parse error — Tracy's own view code tests `GpuEnd() >= 0`. Captures normally end with a few of these, along with a few CPU zones still open (`end == -1`).

**Sections are strictly sequential.** Order is header, meta, CPU topology, crash event, frames, strings, thread compression tables, source locations, locks, messages, zone extras, zones, GPU zones, then plots, memory, callstacks, frame images, context switches and symbols. Parsing stops after GPU zones; anything earlier must still be parsed (or exactly skipped) to stay in sync.
