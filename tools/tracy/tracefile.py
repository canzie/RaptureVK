"""Parser for the Tracy payload stream (Tracy 0.13.x).

Transcribed from Worker::Worker( FileRead& ) in server/TracyWorker.cpp. Sections
are strictly sequential, so every section up to the last one of interest must be
parsed even if its contents are discarded. Parsing stops after GPU zones;
plots, memory, callstacks, frame images, context switches and symbols follow and
are never read.
"""

import struct

from container import decompressPayload
from reader import Reader

MIN_VERSION = (0, 9, 0)
MAX_VERSION = (0, 13, 255)

# Event structs live inside #pragma pack(push, 1) in server/TracyEvent.hpp, so
# they carry no padding.
CRASH_EVENT_SIZE = 28

_srcLocStruct = struct.Struct("<QBQBQBII")
_zoneHeadStruct = struct.Struct("<hqII")
_zoneTailStruct = struct.Struct("<qhqII")
_zoneEndStruct = struct.Struct("<q")
_gpuHeadStruct = struct.Struct("<qqh3xHQ")
_gpuTailStruct = struct.Struct("<qq")


class UnsupportedVersion(Exception):
    pass


class SourceLocation:
    __slots__ = ("name", "function", "file", "line", "color")

    def __init__(self, name, function, file, line, color):
        self.name = name
        self.function = function
        self.file = file
        self.line = line
        self.color = color


class Zone:
    __slots__ = ("srcloc", "start", "end", "children")

    def __init__(self, srcloc, start):
        self.srcloc = srcloc
        self.start = start
        self.end = -1
        self.children = None


class GpuZone:
    __slots__ = ("srcloc", "cpuStart", "cpuEnd", "gpuStart", "gpuEnd", "thread", "children")

    def __init__(self, srcloc):
        self.srcloc = srcloc
        self.cpuStart = -1
        self.cpuEnd = -1
        self.gpuStart = -1
        self.gpuEnd = -1
        self.thread = 0
        self.children = None


class Thread:
    __slots__ = ("tid", "name", "isFiber", "timeline")

    def __init__(self, tid):
        self.tid = tid
        self.name = None
        self.isFiber = False
        self.timeline = []


class GpuContext:
    __slots__ = ("name", "thread", "period", "timeDiff", "type", "timelines")

    def __init__(self):
        self.name = None
        self.thread = 0
        self.period = 1.0
        self.timeDiff = 0
        self.type = 0
        self.timelines = []


class FrameSet:
    __slots__ = ("name", "continuous", "frames")

    def __init__(self, name, continuous):
        self.name = name
        self.continuous = continuous
        self.frames = []


class Trace:
    def __init__(self):
        self.version = None
        self.resolution = 0
        self.timerMul = 1.0
        self.lastTime = 0
        self.frameOffset = 0
        self.pid = 0
        self.samplingPeriod = 0
        self.cpuArch = 0
        self.cpuId = 0
        self.cpuManufacturer = ""
        self.onDemand = False
        self.captureName = ""
        self.captureProgram = ""
        self.captureTime = 0
        self.executableTime = 0
        self.hostInfo = ""
        self.frameSets = []
        self.stringData = []
        self.strings = {}
        self.threadNames = {}
        self.sourceLocations = {}
        self.threads = []
        self.gpuContexts = []

    def sourceLocation(self, srcloc):
        return self.sourceLocations.get(srcloc)

    def zoneName(self, srcloc):
        loc = self.sourceLocations.get(srcloc)
        if loc is None:
            return "<unknown srcloc %d>" % srcloc
        return loc.name or loc.function or "<unnamed>"

    def zoneLocation(self, srcloc):
        loc = self.sourceLocations.get(srcloc)
        if loc is None or not loc.file:
            return None
        return "%s:%d" % (loc.file, loc.line)


def _version(major, minor, patch):
    return (major << 16) | (minor << 8) | patch


def _readStringRefTarget(value, active, isIdx, pointerMap, stringData):
    """Resolve a StringRef to text, or None when inactive."""
    if not active:
        return None
    if isIdx:
        if value < len(stringData):
            return stringData[value]
        return None
    return pointerMap.get(value)


def _parseHeader(r, trace):
    hdr = r.bytes(8)
    if hdr[:5] != b"tracy":
        raise UnsupportedVersion("payload is not a tracy dump")
    trace.version = (hdr[5], hdr[6], hdr[7])
    fileVer = _version(*trace.version)
    if fileVer < _version(*MIN_VERSION) or fileVer > _version(*MAX_VERSION):
        raise UnsupportedVersion("unsupported trace version %d.%d.%d" % trace.version)
    if fileVer < _version(0, 12, 3):
        r.skip(8)
    return fileVer


def _parseMeta(r, trace, fileVer):
    trace.resolution = r.i64()
    trace.timerMul = r.f64()
    trace.lastTime = r.i64()
    trace.frameOffset = r.u64()
    trace.pid = r.u64()
    trace.samplingPeriod = r.i64()
    trace.cpuArch = r.u8()
    trace.cpuId = r.u32()
    trace.cpuManufacturer = r.bytes(12).split(b"\0")[0].decode("utf-8", "replace")

    if fileVer >= _version(0, 9, 2):
        trace.onDemand = r.u8() != 0
    else:
        trace.onDemand = trace.frameOffset != 0

    trace.captureName = r.lengthPrefixedString()
    trace.captureProgram = r.lengthPrefixedString()
    trace.captureTime = r.u64()
    trace.executableTime = r.u64()
    trace.hostInfo = r.lengthPrefixedString()


def _parseCpuTopology(r, fileVer):
    packages = r.u64()
    for _ in range(packages):
        r.u32()
        dieCount = r.u64()
        for _ in range(dieCount):
            if fileVer >= _version(0, 11, 2):
                r.u32()
                coreCount = r.u64()
                for _ in range(coreCount):
                    r.u32()
                    threadCount = r.u64()
                    r.skip(4 * threadCount)
            else:
                r.u32()
                threadCount = r.u64()
                r.skip(4 * threadCount)


def _parseFrames(r, trace):
    count = r.u64()
    for _ in range(count):
        name = r.u64()
        continuous = r.u8()
        frameCount = r.u64()
        fs = FrameSet(name, continuous != 0)
        refTime = 0
        frames = fs.frames
        if continuous:
            for _ in range(frameCount):
                refTime += r.i64()
                frames.append((refTime, -1))
                r.skip(4)
        else:
            for _ in range(frameCount):
                refTime += r.i64()
                start = refTime
                refTime += r.i64()
                frames.append((start, refTime))
                r.skip(4)
        trace.frameSets.append(fs)


def _parseStrings(r, trace):
    pointerMap = {}

    count = r.u64()
    stringData = []
    for _ in range(count):
        ptr = r.u64()
        length = r.u64()
        text = r.bytes(length).decode("utf-8", "replace")
        stringData.append(text)
        pointerMap[ptr] = text
    trace.stringData = stringData

    count = r.u64()
    for _ in range(count):
        ident = r.u64()
        ptr = r.u64()
        if ptr in pointerMap:
            trace.strings[ident] = pointerMap[ptr]

    count = r.u64()
    for _ in range(count):
        ident = r.u64()
        ptr = r.u64()
        if ptr in pointerMap:
            trace.threadNames[ident] = pointerMap[ptr]

    count = r.u64()
    for _ in range(count):
        r.u64()
        r.u64()
        r.u64()

    return pointerMap


def _parseThreadCompress(r):
    """Read a ThreadCompress table, returning its expansion list."""
    count = r.u64()
    if count == 0:
        return []
    return list(r.u64Array(count))


def _readSourceLocation(r):
    return r.unpack(_srcLocStruct)


def _parseSourceLocations(r, trace, pointerMap):
    """Parse static + dynamic source locations into a single srcloc-id table.

    Static locations are addressed by negative ids resolved through
    sourceLocationExpand; dynamic payload locations use non-negative ids.
    """
    staticLocs = {}
    count = r.u64()
    for _ in range(count):
        ptr = r.u64()
        staticLocs[ptr] = _readSourceLocation(r)

    count = r.u64()
    expand = list(r.u64Array(count))

    count = r.u64()
    payload = [_readSourceLocation(r) for _ in range(count)]

    stringData = trace.stringData
    strings = trace.strings

    def resolve(value, flags):
        if flags & 1:
            return stringData[value] if value < len(stringData) else None
        if not (flags >> 1) & 1:
            return None
        return strings.get(value)

    def build(fields):
        nameStr, nameFlags, funcStr, funcFlags, fileStr, fileFlags, line, color = fields
        return SourceLocation(resolve(nameStr, nameFlags), resolve(funcStr, funcFlags),
                              resolve(fileStr, fileFlags), line, color)

    # Worker::GetSourceLocation: non-negative ids index sourceLocationExpand,
    # negative ids address the dynamic payload as -srcloc-1.
    locations = {}
    for idx, ptr in enumerate(expand):
        if ptr in staticLocs:
            locations[idx] = build(staticLocs[ptr])
    for idx, fields in enumerate(payload):
        locations[-idx - 1] = build(fields)

    trace.sourceLocations = locations

    # sourceLocationZones / gpuSourceLocationZones reservation hints
    count = r.u64()
    r.skip(count * 10)
    count = r.u64()
    r.skip(count * 10)


LOCK_TIMELINE_ENTRY_SIZE = 8 + 2 + 1 + 1
MESSAGE_ENTRY_SIZE = 8 + 8 + 9 + 4 + 3
ZONE_EXTRA_SIZE = 12
SAMPLE_ENTRY_SIZE = 8 + 3
GPU_CTX_HEADER_SIZE = 8 + 1 + 8 + 4 + 1 + 3 + 8
GPU_NOTE_NAME_SIZE = 8 + 3


def _parseLocks(r):
    count = r.u64()
    for _ in range(count):
        r.skip(3 + 4 + 2 + 1 + 1 + 8 + 8)
        threadCount = r.u64()
        r.skip(threadCount * 8)
        eventCount = r.u64()
        r.skip(eventCount * LOCK_TIMELINE_ENTRY_SIZE)


def _parseMessages(r):
    count = r.u64()
    r.skip(count * MESSAGE_ENTRY_SIZE)


def _parseZoneExtra(r):
    count = r.u64()
    r.skip(count * ZONE_EXTRA_SIZE)


def _readZoneTimeline(r, size, refTime, depth, visitor):
    """Read one CPU zone timeline level, returning (refTime, totalChildTime).

    Mirrors Worker::ReadTimeline: timestamps are deltas against a running
    refTime threaded through the depth-first walk, and each iteration's tail
    read is fused with the next iteration's head.
    """
    data = r.data
    pos = r.pos
    srcloc, tstart, extra, childSz = _zoneHeadStruct.unpack_from(data, pos)
    pos += 18
    spanTotal = 0
    onZone = visitor.zone

    for i in range(size):
        last = i == size - 1
        refTime += tstart
        start = refTime
        if childSz != 0:
            r.pos = pos
            refTime, childTime = _readZoneTimeline(r, childSz, refTime, depth + 1, visitor)
            pos = r.pos
        else:
            childTime = 0
        if last:
            (tend,) = _zoneEndStruct.unpack_from(data, pos)
            pos += 8
        else:
            tend, nextSrcloc, nextStart, nextExtra, nextChildSz = _zoneTailStruct.unpack_from(data, pos)
            pos += 26
        refTime += tend
        end = refTime
        onZone(srcloc, start, end, childTime, depth)
        spanTotal += end - start
        if not last:
            srcloc, tstart, extra, childSz = nextSrcloc, nextStart, nextExtra, nextChildSz

    r.pos = pos
    return refTime, spanTotal


def _parseThreads(r, trace, fileVer, visitor):
    r.u64()  # total zone count, progress reporting only
    r.u64()  # zoneChildren count
    threadCount = r.u64()

    for _ in range(threadCount):
        tid = r.u64()
        r.u64()  # zone count
        r.u64()  # kernel sample count
        isFiber = r.u8()
        if fileVer >= _version(0, 11, 1):
            r.i32()  # groupHint

        thread = Thread(tid)
        thread.name = trace.threadNames.get(tid)
        thread.isFiber = isFiber != 0
        trace.threads.append(thread)

        visitor.beginThread(thread)
        timelineSize = r.u32()
        if timelineSize != 0:
            _readZoneTimeline(r, timelineSize, 0, 0, visitor)
        visitor.endThread(thread)

        messageCount = r.u64()
        r.skip(messageCount * 8)
        ctxSwitchSamples = r.u64()
        r.skip(ctxSwitchSamples * SAMPLE_ENTRY_SIZE)
        samples = r.u64()
        r.skip(samples * SAMPLE_ENTRY_SIZE)


def _readGpuTimeline(r, size, refTime, refGpuTime, depth, hasQueryId, visitor):
    data = r.data
    onZone = visitor.gpuZone
    spanTotal = 0
    for _ in range(size):
        tcpu, tgpu, srcloc, thread, childSz = _gpuHeadStruct.unpack_from(data, r.pos)
        r.pos += 31
        refTime += tcpu
        refGpuTime += tgpu
        cpuStart = refTime
        gpuStart = refGpuTime
        if childSz != 0:
            refTime, refGpuTime, childTime = _readGpuTimeline(
                r, childSz, refTime, refGpuTime, depth + 1, hasQueryId, visitor)
        else:
            childTime = 0
        tcpu, tgpu = _gpuTailStruct.unpack_from(data, r.pos)
        r.pos += 16
        refTime += tcpu
        refGpuTime += tgpu
        if hasQueryId:
            r.pos += 2
        onZone(srcloc, gpuStart, refGpuTime, cpuStart, refTime, childTime, depth)
        if gpuStart >= 0 and refGpuTime >= 0:
            spanTotal += refGpuTime - gpuStart
    return refTime, refGpuTime, spanTotal


def _parseGpu(r, trace, fileVer, visitor):
    hasQueryId = fileVer >= _version(0, 12, 4)
    r.u64()  # total gpu zone count
    r.u64()  # gpuChildren count
    ctxCount = r.u64()

    for _ in range(ctxCount):
        ctx = GpuContext()
        ctx.thread = r.u64()
        r.u8()  # calibration
        r.u64()  # count
        ctx.period = r.f32()
        ctx.type = r.u8()
        nameIdx = r.bytes(3)
        r.u64()  # overflow
        idx = int.from_bytes(nameIdx, "little")
        if idx != 0 and (idx - 1) < len(trace.stringData):
            ctx.name = trace.stringData[idx - 1]

        if hasQueryId:
            noteNames = r.u64()
            r.skip(noteNames * GPU_NOTE_NAME_SIZE)

        trace.gpuContexts.append(ctx)

        # Each thread's timeline restarts its delta accumulators, so it is
        # bracketed separately even though they share one context.
        threadCount = r.u64()
        for _ in range(threadCount):
            r.u64()  # tid
            size = r.u64()
            if size != 0:
                visitor.beginGpuContext(ctx)
                _readGpuTimeline(r, size, 0, 0, 0, hasQueryId, visitor)
                visitor.endGpuContext(ctx)

        if hasQueryId:
            noteCount = r.u64()
            for _ in range(noteCount):
                r.u16()
                perQuery = r.u64()
                r.skip(perQuery * 16)


class Visitor:
    """No-op sink for the timeline walk."""

    def beginThread(self, thread):
        pass

    def endThread(self, thread):
        pass

    def zone(self, srcloc, start, end, childTime, depth):
        pass

    def beginGpuContext(self, ctx):
        pass

    def endGpuContext(self, ctx):
        pass

    def gpuZone(self, srcloc, gpuStart, gpuEnd, cpuStart, cpuEnd, childTime, depth):
        pass


def parse(path, visitor=None, withGpu=True):
    trace = Trace()
    r = Reader(decompressPayload(path))

    fileVer = _parseHeader(r, trace)
    _parseMeta(r, trace, fileVer)
    _parseCpuTopology(r, fileVer)
    r.skip(CRASH_EVENT_SIZE)
    _parseFrames(r, trace)
    pointerMap = _parseStrings(r, trace)
    _parseThreadCompress(r)
    _parseThreadCompress(r)
    _parseSourceLocations(r, trace, pointerMap)

    if visitor is None:
        visitor = Visitor()
    _parseLocks(r)
    _parseMessages(r)
    _parseZoneExtra(r)
    _parseThreads(r, trace, fileVer, visitor)
    if withGpu:
        _parseGpu(r, trace, fileVer, visitor)

    trace._reader = r
    trace._fileVer = fileVer
    return trace
