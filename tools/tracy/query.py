"""Queries over a parsed trace: frame extraction, zone statistics, validation."""

import math

import tracefile as T


def frameEnd(trace, frameSet, idx):
    if frameSet.continuous:
        if idx < len(frameSet.frames) - 1:
            return frameSet.frames[idx + 1][0]
        return trace.lastTime
    end = frameSet.frames[idx][1]
    return end if end >= 0 else trace.lastTime


def frameNumber(trace, frameSet, idx):
    """Frame number as the Tracy viewer displays it."""
    if frameSet.name == 0:
        if trace.frameOffset == 0:
            return idx
        return idx + trace.frameOffset - 1
    return idx + 1


def frameIndexFromNumber(trace, frameSet, number):
    if frameSet.name == 0:
        base = 0 if trace.frameOffset == 0 else trace.frameOffset - 1
    else:
        base = 1
    return number - base


def frameSetName(trace, frameSet):
    if frameSet.name == 0:
        return "main"
    return trace.strings.get(frameSet.name) or "<frameset %d>" % frameSet.name


def findFrameSet(trace, name):
    if name is None:
        return trace.frameSets[0]
    for fs in trace.frameSets:
        if frameSetName(trace, fs) == name:
            return fs
    return None


class ZoneNode:
    __slots__ = ("srcloc", "start", "end", "childTime", "children")

    def __init__(self, srcloc, start, end, childTime):
        self.srcloc = srcloc
        self.start = start
        self.end = end
        self.childTime = childTime
        self.children = []


class FrameCollector(T.Visitor):
    """Retains only the zones overlapping a time window, as a tree per thread.

    Zones arrive post-order, so a completed zone claims every pending zone one
    level deeper as its children.
    """

    def __init__(self, begin, end):
        self.begin = begin
        self.end = end
        self.threads = []
        self.gpu = []
        self._pending = None
        self._sink = None

    def _open(self, sink):
        self._pending = {}
        self._sink = sink

    def _close(self):
        roots = []
        for depth in sorted(self._pending):
            roots.extend(self._pending[depth])
        self._sink.extend(roots)
        self._pending = None
        self._sink = None

    def _add(self, node, depth):
        claimed = self._pending.pop(depth + 1, None)
        if claimed:
            node.children = claimed
        self._pending.setdefault(depth, []).append(node)

    def beginThread(self, thread):
        entry = {"thread": thread, "zones": []}
        self.threads.append(entry)
        self._open(entry["zones"])

    def endThread(self, thread):
        self._close()

    def zone(self, srcloc, start, end, childTime, depth):
        if end < self.begin or start > self.end:
            # A parent may still straddle the window while this child does not,
            # so only drop it once nothing deeper is pending beneath it.
            if (depth + 1) not in self._pending:
                return
        self._add(ZoneNode(srcloc, start, end, childTime), depth)

    def beginGpuContext(self, ctx):
        for entry in self.gpu:
            if entry["ctx"] is ctx:
                self._open(entry["zones"])
                return
        entry = {"ctx": ctx, "zones": []}
        self.gpu.append(entry)
        self._open(entry["zones"])

    def endGpuContext(self, ctx):
        self._close()

    def gpuZone(self, srcloc, gpuStart, gpuEnd, cpuStart, cpuEnd, childTime, depth):
        if gpuEnd < self.begin or gpuStart > self.end:
            if (depth + 1) not in self._pending:
                return
        self._add(ZoneNode(srcloc, gpuStart, gpuEnd, childTime), depth)


class StatsCollector(T.Visitor):
    """Accumulates per-source-location zone statistics without retaining zones."""

    def __init__(self, keepSamples=False, window=None):
        self.stats = {}
        self.gpuStats = {}
        self.keepSamples = keepSamples
        self.samples = {}
        self.window = window

    def _record(self, table, srcloc, start, end, childTime):
        if self.window is not None:
            if start < self.window[0] or end > self.window[1]:
                return
        if end < start:
            return
        span = end - start
        self_ = span - childTime
        entry = table.get(srcloc)
        if entry is None:
            table[srcloc] = [1, span, self_, span, span, float(span) * span]
        else:
            entry[0] += 1
            entry[1] += span
            entry[2] += self_
            if span < entry[3]:
                entry[3] = span
            if span > entry[4]:
                entry[4] = span
            entry[5] += float(span) * span
        if self.keepSamples:
            self.samples.setdefault(srcloc, []).append(span)

    def zone(self, srcloc, start, end, childTime, depth):
        self._record(self.stats, srcloc, start, end, childTime)

    def gpuZone(self, srcloc, gpuStart, gpuEnd, cpuStart, cpuEnd, childTime, depth):
        if gpuStart <= 0 or gpuEnd <= 0:
            return
        self._record(self.gpuStats, srcloc, gpuStart, gpuEnd, childTime)


class Validator(T.Visitor):
    """Checks structural invariants that a misparse would violate."""

    def __init__(self):
        self.zones = 0
        self.inverted = 0
        self.unterminated = 0
        self.containment = 0
        self.ordering = 0
        self.maxDepth = 0
        self._pending = {}
        self._lastAtDepth = {}

    def beginThread(self, thread):
        self._pending = {}
        self._lastAtDepth = {}

    endThread = beginThread
    beginGpuContext = beginThread
    endGpuContext = beginThread

    def zone(self, srcloc, start, end, childTime, depth):
        self.zones += 1
        if depth > self.maxDepth:
            self.maxDepth = depth
        if end < 0:
            self.unterminated += 1
        elif end < start:
            self.inverted += 1

        children = self._pending.pop(depth + 1, None)
        if children and end >= 0:
            for cs, ce in children:
                if cs < start or (ce >= 0 and ce > end):
                    self.containment += 1

        prev = self._lastAtDepth.get(depth)
        if prev is not None and start < prev:
            self.ordering += 1
        self._lastAtDepth[depth] = start
        for d in list(self._lastAtDepth):
            if d > depth:
                del self._lastAtDepth[d]

        self._pending.setdefault(depth, []).append((start, end))

    def gpuZone(self, srcloc, gpuStart, gpuEnd, cpuStart, cpuEnd, childTime, depth):
        # Tracy marks an unresolved GPU query with a negative timestamp.
        if gpuStart < 0 or gpuEnd < 0:
            self.zones += 1
            self.unterminated += 1
            return
        self.zone(srcloc, gpuStart, gpuEnd, childTime, depth)


def matchesZone(trace, srcloc, pattern):
    """Match a zone by name substring, or exactly by `name@file:line`."""
    name, sep, loc = pattern.partition("@")
    if sep and loc:
        if trace.zoneName(srcloc) != name:
            return False
        actual = trace.zoneLocation(srcloc)
        return actual is not None and actual.rsplit("/", 1)[-1] == loc
    return name.lower() in trace.zoneName(srcloc).lower()


def findSubtrees(trace, nodes, pattern):
    """Outermost zones matching `pattern`, without descending into a match."""
    found = []

    def walk(items):
        for node in items:
            if matchesZone(trace, node.srcloc, pattern):
                found.append(node)
            else:
                walk(node.children)

    walk(nodes)
    return found


def aggregate(trace, nodes, table=None):
    """Sum every zone in the given subtrees by name.

    Returns name -> [count, total, self, min, max, location].
    """
    if table is None:
        table = {}
    for node in nodes:
        if node.end >= 0:
            span = node.end - node.start
            name = trace.zoneName(node.srcloc)
            entry = table.get(name)
            if entry is None:
                table[name] = [1, span, span - node.childTime, span, span,
                               trace.zoneLocation(node.srcloc)]
            else:
                entry[0] += 1
                entry[1] += span
                entry[2] += span - node.childTime
                if span < entry[3]:
                    entry[3] = span
                if span > entry[4]:
                    entry[4] = span
        aggregate(trace, node.children, table)
    return table


def percentile(sortedValues, p):
    if not sortedValues:
        return 0
    idx = p * (len(sortedValues) - 1)
    low = int(math.floor(idx))
    high = min(low + 1, len(sortedValues) - 1)
    frac = idx - low
    return sortedValues[low] + (sortedValues[high] - sortedValues[low]) * frac


def stddev(count, total, sumSq):
    if count < 2:
        return 0.0
    mean = total / count
    ss = sumSq - 2.0 * total * mean + mean * mean * count
    if ss <= 0:
        return 0.0
    return math.sqrt(ss / (count - 1))
