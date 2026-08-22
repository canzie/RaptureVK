#!/usr/bin/env python3
"""Query a Tracy .tracy capture from the command line.

Reads the trace format directly (Tracy 0.13.x), no Tracy build required.
"""

import argparse
import json
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
sys.setrecursionlimit(100000)

import query
import tracefile


def formatTime(ns):
    if ns is None:
        return "-"
    if abs(ns) < 1000:
        return "%dns" % ns
    if abs(ns) < 1000000:
        return "%.2fus" % (ns / 1e3)
    if abs(ns) < 1000000000:
        return "%.3fms" % (ns / 1e6)
    return "%.3fs" % (ns / 1e9)


def shortLocation(trace, srcloc):
    loc = trace.zoneLocation(srcloc)
    if loc is None:
        return ""
    path, _, line = loc.rpartition(":")
    return "%s:%s" % (path.rsplit("/", 1)[-1], line)


def cmdInfo(trace, args):
    print("capture     : %s" % trace.captureName)
    print("program     : %s" % trace.captureProgram)
    print("tracy       : %d.%d.%d" % trace.version)
    print("duration    : %s" % formatTime(trace.lastTime))
    print("frameOffset : %d" % trace.frameOffset)
    print("host        :")
    for line in trace.hostInfo.strip().splitlines():
        print("    %s" % line)
    print("threads     :")
    for th in trace.threads:
        print("    %-20s tid=%-12d%s" % (th.name or "<unnamed>", th.tid,
                                         " (fiber)" if th.isFiber else ""))
    print("gpu contexts:")
    for ctx in trace.gpuContexts:
        print("    %-20s type=%d period=%g" % (ctx.name or "<unnamed>", ctx.type, ctx.period))
    print("frame sets  :")
    for fs in trace.frameSets:
        lo = query.frameNumber(trace, fs, 0)
        hi = query.frameNumber(trace, fs, len(fs.frames) - 1)
        print("    %-20s %d frames, numbered %d..%d" % (
            query.frameSetName(trace, fs), len(fs.frames), lo, hi))
    print("source locs : %d" % len(trace.sourceLocations))


def cmdFrames(trace, args):
    fs = query.findFrameSet(trace, args.set)
    if fs is None:
        print("no such frame set: %s" % args.set, file=sys.stderr)
        return 1

    times = []
    for i in range(len(fs.frames)):
        begin = fs.frames[i][0]
        times.append((query.frameNumber(trace, fs, i), begin, query.frameEnd(trace, fs, i) - begin))

    durations = sorted(t[2] for t in times)
    total = sum(durations)
    print("frame set %s: %d frames" % (query.frameSetName(trace, fs), len(times)))
    print("  mean %s  median %s  min %s  max %s" % (
        formatTime(total // len(times)), formatTime(query.percentile(durations, 0.5)),
        formatTime(durations[0]), formatTime(durations[-1])))
    print("  p90 %s  p99 %s" % (formatTime(query.percentile(durations, 0.90)),
                                formatTime(query.percentile(durations, 0.99))))

    if args.slowest:
        print("\nslowest %d frames:" % args.slowest)
        for number, begin, dur in sorted(times, key=lambda t: -t[2])[:args.slowest]:
            print("  frame %-8d begin %-10s dur %s" % (number, formatTime(begin), formatTime(dur)))


def _renderTree(trace, nodes, frameBegin, args, out, depth=0):
    for node in sorted(nodes, key=lambda n: n.start):
        span = (node.end - node.start) if node.end >= 0 else -1
        if span >= 0 and span < args.minNs:
            continue
        if args.maxDepth is not None and depth > args.maxDepth:
            continue
        selfTime = (span - node.childTime) if span >= 0 else -1
        out.append("  %9s %10s %8s  %s%s  %s" % (
            formatTime(node.start - frameBegin),
            formatTime(span) if span >= 0 else "OPEN",
            formatTime(selfTime) if selfTime >= 0 else "-",
            "  " * depth,
            trace.zoneName(node.srcloc),
            shortLocation(trace, node.srcloc)))
        _renderTree(trace, node.children, frameBegin, args, out, depth + 1)


def _treeToJson(trace, nodes, frameBegin, args, depth=0):
    result = []
    for node in sorted(nodes, key=lambda n: n.start):
        span = (node.end - node.start) if node.end >= 0 else -1
        if span >= 0 and span < args.minNs:
            continue
        if args.maxDepth is not None and depth > args.maxDepth:
            continue
        entry = {
            "name": trace.zoneName(node.srcloc),
            "loc": shortLocation(trace, node.srcloc),
            "startUs": round((node.start - frameBegin) / 1e3, 3),
            "durUs": round(span / 1e3, 3) if span >= 0 else None,
            "selfUs": round((span - node.childTime) / 1e3, 3) if span >= 0 else None,
        }
        children = _treeToJson(trace, node.children, frameBegin, args, depth + 1)
        if children:
            entry["children"] = children
        result.append(entry)
    return result


def cmdFrame(trace, args, path):
    fs = query.findFrameSet(trace, args.set)
    if fs is None:
        print("no such frame set: %s" % args.set, file=sys.stderr)
        return 1

    if args.index is not None:
        idx = args.index
    else:
        idx = query.frameIndexFromNumber(trace, fs, args.number)
    if idx < 0 or idx >= len(fs.frames):
        lo = query.frameNumber(trace, fs, 0)
        hi = query.frameNumber(trace, fs, len(fs.frames) - 1)
        print("frame out of range; this capture has frames %d..%d" % (lo, hi), file=sys.stderr)
        return 1

    begin = fs.frames[idx][0]
    end = query.frameEnd(trace, fs, idx)
    collector = query.FrameCollector(begin, end)
    tracefile.parse(path, collector)

    if args.json:
        doc = {
            "frame": query.frameNumber(trace, fs, idx),
            "frameSet": query.frameSetName(trace, fs),
            "beginNs": begin,
            "durationUs": round((end - begin) / 1e3, 3),
            "threads": [],
            "gpu": [],
        }
        for entry in collector.threads:
            roots = query.findSubtrees(trace, entry["zones"], args.under) if args.under else entry["zones"]
            zones = _treeToJson(trace, roots, begin, args)
            if zones:
                doc["threads"].append({
                    "name": entry["thread"].name or "<unnamed>",
                    "fiber": entry["thread"].isFiber,
                    "zones": zones,
                })
        for entry in collector.gpu:
            roots = query.findSubtrees(trace, entry["zones"], args.under) if args.under else entry["zones"]
            zones = _treeToJson(trace, roots, begin, args)
            if zones:
                doc["gpu"].append({"context": entry["ctx"].name or "<unnamed>", "zones": zones})
        print(json.dumps(doc, indent=1))
        return

    print("frame %d  (%s)  begin %s  duration %s" % (
        query.frameNumber(trace, fs, idx), query.frameSetName(trace, fs),
        formatTime(begin), formatTime(end - begin)))

    groups = [(entry["thread"].name or "<unnamed>", entry["thread"].isFiber, entry["zones"])
              for entry in collector.threads]
    groups += [("GPU: " + (entry["ctx"].name or "<unnamed>"), False, entry["zones"])
               for entry in collector.gpu]

    if not args.flat:
        print("  %9s %10s %8s  %s" % ("start", "dur", "self", "zone"))

    for label, isFiber, zones in groups:
        roots = query.findSubtrees(trace, zones, args.under) if args.under else zones
        if not roots:
            continue
        if args.flat:
            _printFlat(trace, label, roots, args)
            continue
        out = []
        _renderTree(trace, roots, begin, args, out)
        if not out:
            continue
        print("\n%s%s" % (label, "  (fiber)" if isFiber else ""))
        print("\n".join(out))


def _printFlat(trace, label, roots, args):
    span = sum(n.end - n.start for n in roots if n.end >= 0)
    table = query.aggregate(trace, roots)
    print("\n%s  --  %d subtree(s), %s total" % (label, len(roots), formatTime(span)))
    print("  %-38s %6s %11s %11s %9s %9s  %s" % (
        "zone", "count", "total", "self", "min", "max", "loc"))
    rows = sorted(table.items(), key=lambda kv: -kv[1][2])[:args.top]
    for name, (count, total, selfTime, tmin, tmax, loc) in rows:
        print("  %-38s %6d %11s %11s %9s %9s  %s" % (
            name[:38], count, formatTime(total), formatTime(selfTime),
            formatTime(tmin), formatTime(tmax),
            (loc or "").rsplit("/", 1)[-1]))


def cmdZones(trace, args, path):
    collector = query.StatsCollector()
    tracefile.parse(path, collector)
    table = collector.gpuStats if args.gpu else collector.stats

    rows = []
    for srcloc, (count, total, selfTotal, tmin, tmax, sumSq) in table.items():
        name = trace.zoneName(srcloc)
        if args.filter and args.filter.lower() not in name.lower():
            continue
        rows.append((srcloc, name, count, total, selfTotal, tmin, tmax, sumSq))

    key = 4 if args.self else 3
    rows.sort(key=lambda r: -r[key])
    rows = rows[:args.top]

    print("%-44s %8s %11s %11s %10s %10s %10s" % (
        "zone", "count", "total", "self", "mean", "min", "max"))
    for srcloc, name, count, total, selfTotal, tmin, tmax, sumSq in rows:
        print("%-44s %8d %11s %11s %10s %10s %10s" % (
            name[:44], count, formatTime(total), formatTime(selfTotal),
            formatTime(total // count), formatTime(tmin), formatTime(tmax)))


def cmdZone(trace, args, path):
    collector = query.StatsCollector(keepSamples=True)
    tracefile.parse(path, collector)
    table = collector.gpuStats if args.gpu else collector.stats

    matches = [s for s in table if args.name.lower() in trace.zoneName(s).lower()]
    if not matches:
        print("no zone matching %r" % args.name, file=sys.stderr)
        return 1

    for srcloc in sorted(matches, key=lambda s: -table[s][1]):
        count, total, selfTotal, tmin, tmax, sumSq = table[srcloc]
        samples = sorted(collector.samples.get(srcloc, []))
        print("%s   %s" % (trace.zoneName(srcloc), shortLocation(trace, srcloc)))
        print("  count   %d" % count)
        print("  total   %s   (%.2f%% of capture)" % (
            formatTime(total), 100.0 * total / trace.lastTime))
        print("  self    %s" % formatTime(selfTotal))
        print("  mean    %s     median %s" % (
            formatTime(total // count), formatTime(query.percentile(samples, 0.5))))
        print("  min     %s     max    %s" % (formatTime(tmin), formatTime(tmax)))
        print("  p90     %s     p99    %s" % (
            formatTime(query.percentile(samples, 0.90)),
            formatTime(query.percentile(samples, 0.99))))
        print("  stddev  %s" % formatTime(query.stddev(count, total, sumSq)))
        print()


def cmdValidate(trace, args, path):
    validator = query.Validator()
    tracefile.parse(path, validator)
    reader = trace._reader
    print("zones parsed      : %d" % validator.zones)
    print("max nesting depth : %d" % validator.maxDepth)
    print("unterminated      : %d  (open at capture end, expected to be small)" % validator.unterminated)
    print("inverted (end<start): %d" % validator.inverted)
    print("containment breaks: %d" % validator.containment)
    print("ordering breaks   : %d" % validator.ordering)
    failed = validator.inverted or validator.containment or validator.ordering
    print("\n%s" % ("FAILED - parse is not trustworthy" if failed else "OK"))
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(prog="tracy", description=__doc__)
    parser.add_argument("trace", help="path to a .tracy file")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("info", help="capture metadata, threads, frame sets")

    p = sub.add_parser("frames", help="frame time statistics")
    p.add_argument("--set", help="frame set name (default: main)")
    p.add_argument("--slowest", type=int, default=0, help="list the N slowest frames")

    p = sub.add_parser("frame", help="full zone tree for one frame")
    p.add_argument("number", type=int, nargs="?", help="frame number as shown in the viewer")
    p.add_argument("--index", type=int, help="address the frame by raw index instead")
    p.add_argument("--set", help="frame set name (default: main)")
    p.add_argument("--min-us", dest="minUs", type=float, default=0.0,
                   help="hide zones shorter than this many microseconds")
    p.add_argument("--max-depth", dest="maxDepth", type=int, help="limit nesting depth")
    p.add_argument("--under", help="restrict to subtrees rooted at zones matching this name")
    p.add_argument("--flat", action="store_true",
                   help="aggregate descendants by zone instead of printing a tree")
    p.add_argument("--top", type=int, default=20, help="rows to show with --flat")
    p.add_argument("--json", action="store_true", help="emit JSON instead of a tree")

    p = sub.add_parser("zones", help="aggregate statistics for every zone")
    p.add_argument("--top", type=int, default=30)
    p.add_argument("--self", action="store_true", help="rank by self time")
    p.add_argument("--gpu", action="store_true", help="report GPU zones")
    p.add_argument("--filter", help="substring match on zone name")

    p = sub.add_parser("zone", help="detailed statistics for one zone")
    p.add_argument("name")
    p.add_argument("--gpu", action="store_true", help="report GPU zones")

    sub.add_parser("validate", help="check structural invariants of the parse")

    args = parser.parse_args()
    if args.command == "frame":
        if args.number is None and args.index is None:
            parser.error("frame requires a frame number or --index")
        args.minNs = int(args.minUs * 1000)

    trace = tracefile.parse(args.trace)

    if args.command == "info":
        return cmdInfo(trace, args)
    if args.command == "frames":
        return cmdFrames(trace, args)
    if args.command == "frame":
        return cmdFrame(trace, args, args.trace)
    if args.command == "zones":
        return cmdZones(trace, args, args.trace)
    if args.command == "zone":
        return cmdZone(trace, args, args.trace)
    if args.command == "validate":
        return cmdValidate(trace, args, args.trace)


if __name__ == "__main__":
    sys.exit(main() or 0)
