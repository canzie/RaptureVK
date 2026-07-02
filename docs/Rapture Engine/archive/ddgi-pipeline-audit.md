# DDGI Pipeline Audit — Verified Happens-Before Model and Root-Cause Analysis

Status: rewritten from source (zero-assumption pass). Every claim below is cited to `file:line`.
Where this contradicts INVESTIGATION_NOTES.md or the previous version of this file, the correction is
called out explicitly. **The two load-bearing corrections are in §7.3 and §9** — the prior investigation's
central deduction ("a full-frame fence == waitIdle on the GPU, therefore GPU overlap is ruled out") is
FALSE, and that false deduction is why the bug was never fixed.

---

## 0. TL;DR

- The irradiance atlas (`m_RadianceTexture`) and distance atlas (`m_VisibilityTexture`) are **single-buffered**
  (`DynamicDiffuseGI.cpp:763-764`, `.h:112-113`).
- Per frame, the DDGI trace/blend passes and the lighting pass are recorded into **one** graphics command
  buffer (`DeferredRenderer.cpp:296` records DDGI inline; lighting at `:446-451`). That CB is `addToBatch`'d
  (`:148`) and later submitted by the UI layer (`RenderWindow.cpp:117`) as one of a 2-`VkSubmitInfo`
  `vkQueueSubmit` (`VulkanQueue.cpp:274`). Intra-frame ordering (blend → lighting) is correct via the
  post-blend barrier (`DynamicDiffuseGI.cpp:708-712`).
- **Consecutive frames' scene command buffers are SEPARATE `vkQueueSubmit` calls, and the scene submit has
  NO wait semaphore** (`VulkanQueue.cpp:262-268` — `batchSubmitInfo` sets only a signal, no waits). There is
  therefore **zero GPU synchronization on the atlas across frames.**
- The engine keeps **`imageCount` frames in flight**, and `imageCount = minImageCount + 1`
  (`SwapChain.cpp:87-88`) — with the chosen MAILBOX/FIFO present mode this is almost certainly **3 or 4**, not
  2. So up to 3-4 frames concurrently read/write the one atlas with no ordering between them.
- Root cause: **unsynchronized concurrent access to the single-buffered atlas across the multiple in-flight
  frames.** `vkDeviceWaitIdle` is the only thing ever tried that forces exactly **1** frame in flight, which
  is why it is the only thing that fixes it. Every fence attempt left `imageCount` (≥2) frames overlapping;
  every barrier attempt cannot cross a submit boundary. None addressed the actual hazard.

---

## 1. Queues (verified)

- `m_queues` is `std::map<uint32_t, shared_ptr<VulkanQueue>>` keyed by **queue family index**
  (`VulkanContext.h`; `s_createQueues` inserts `queues[queueFamily] = primaryQueue`,
  `VulkanContext.cpp:1185-1186`). One `VulkanQueue` per *unique* family (`:1151-1154`).
- `getGraphicsQueue()/getComputeQueue()/getPresentQueue()/getTransferQueue()` each look up
  `m_queueFamilyIndices.familyIndices[...]` and return `m_queues[thatFamily]`
  (`VulkanContext.cpp:199-253`). **If two roles share a family they return the identical `VulkanQueue`
  object** (same `VkQueue`, same internal timeline semaphores, same mutex). On this machine all four share one
  family → one queue. Confirmed in source; the analysis below does not depend on it (see §10).
- Relevance: in the current code the per-frame DDGI is **not** submitted on the compute queue at all — it is
  recorded into the graphics CB (`DeferredRenderer.cpp:296`, `DynamicDiffuseGI.cpp:301-309`). `m_computeQueue`
  is only used by `clearTextures()` once at init (`DynamicDiffuseGI.cpp:240`). So DDGI-writes and
  lighting-reads are always on the **same** `VulkanQueue`, in the **same** submit, per frame.

---

## 2. Submission — complete per-frame trace (OFFSCREEN / Editor)

`Application::run()` loop body, in order (`Application.cpp:129-188`):

1. `getComputeQueue()->flush(); getGraphicsQueue()->flush(); getTransferQueue()->flush();` (`:135-137`) —
   flushes any *batched* CBs from the previous iteration that weren't consumed by `submitAndFlushQueue`.
2. `commandPoolManager->beginFrame()` (`:139`) — marks `pool[m_currentFrameIndex]` reset-pending for every
   pool (`CommandPool.cpp:198-208`).
3. layers `onUpdate` (`:141-143`).
4. `m_viewportManager->drawAll()` (`:156`) → `DeferredRenderer::drawFrame`:
   - `m_currentFrame = getFrameInFlightIndex()` (`DeferredRenderer.cpp:83`).
   - get `pool = getCommandPool(hash, m_currentFrame)` and `commandBuffer = pool->getPrimaryCommandBuffer()`
     (`:102-103`). `getPrimaryCommandBuffer` calls `resetIfNeeded()` (`CommandPool.cpp:49`).
   - `recordCommandBuffer(...)` (`:105`): begins the CB (`:287`); **records DDGI inline** via
     `populateProbesCompute(scene, m_currentFrame, commandBuffer)` (`:296`); shadows (`:312-355`); GBuffer,
     Lighting, Skybox, Instanced secondaries recorded on jobs and executed into the primary in order
     (`:439-463`); offscreen RT → shader-read transition (`:471-473`); ends CB (`:478`).
   - `m_graphicsQueue->addToBatch(commandBuffer)` (`:148`). **No submit here.**
5. overlays `onUpdate` (`:158-160`) → AmethystLayer → `RenderWindow::beginFrame()` +
   `RenderWindow::endFrame()`:
   - `beginFrame`: `acquireImage(m_currentFrame)` (`RenderWindow.cpp:78`) — this is a **separate** frame
     counter, `RenderWindow::m_currentFrame`, advanced independently at `:144`. Records the UI CB.
   - `endFrame`: `submitAndFlushQueue(uiCB, signal=renderFinished, wait=imageAvailable,
     waitStage=COLOR_ATTACHMENT_OUTPUT, fence=inFlightFence[m_currentFrame])` (`RenderWindow.cpp:110-118`),
     then `presentQueue(...)` (`:131`), then `m_currentFrame = (m_currentFrame+1) % imageCount` (`:144`).
6. `commandPoolManager->endFrame()` (`Application.cpp:183`) → `m_currentFrameIndex = (+1) % framesInFlight`
   (`CommandPool.cpp:210-213`).
7. `m_frameInFlightIndex = (m_frameInFlightIndex + 1) % imageCount` (`Application.cpp:185`).

### 2.1 The critical merge — `submitAndFlushQueue` with a non-empty batch

`VulkanQueue::submitAndFlushQueue` (`VulkanQueue.cpp:194-301`). When `commandBuffer=uiCB` and the batch holds
the scene CB, it builds **two** `VkSubmitInfo` and issues them in ONE `vkQueueSubmit`:

```
submits[0] = immediateSubmitInfo (uiCB):   waits imageAvailable @ COLOR_ATTACHMENT_OUTPUT,
                                           signals renderFinished + m_immediateTimeSema   (:241-253)
submits[1] = batchSubmitInfo (sceneCB):    NO waitSemaphores at all,
                                           signals m_timelineSemaphore @ value            (:262-268)
vkQueueSubmit(m_queue, 2, submits, fence=inFlightFence[rw.m_currentFrame])                (:274)
```

Key facts, verified:
- **The scene CB submit (`submits[1]`) has no wait semaphore.** Nothing gates it against the *previous
  frame's* scene CB. (Vulkan submission order gives no memory/execution dependency without a semaphore, and
  the scene CB's waitStage set is empty anyway.)
- The `imageAvailable` wait on `submits[0]` is at `COLOR_ATTACHMENT_OUTPUT`, which does **not** block compute
  or earlier stages, and in any case applies to the UI CB, not the scene CB.
- The fence is the swapchain `inFlightFence[rw.m_currentFrame]`; it signals when **both** submits finish.

### 2.2 GPU-queue order across frames

There is no per-frame present in the OFFSCREEN `drawFrame` (that path is SWAPCHAIN-only,
`DeferredRenderer.cpp:92-146`). Every frame the sequence on the one `VkQueue` is:

```
frame N   : vkQueueSubmit(2, [uiCB_N,  sceneCB_N ], inFlightFence[a])
frame N+1 : vkQueueSubmit(2, [uiCB_N+1, sceneCB_N+1], inFlightFence[b])
...
```

`sceneCB_N` and `sceneCB_N+1` are **different submits with no semaphore between them** → the GPU may (and on
a discrete GPU, will) execute them concurrently, bounded only by the host-side throttle in §3. The DDGI blend
in `sceneCB_N+1` and the lighting sample + DDGI blend in `sceneCB_N` touch the **same** atlas memory with no
ordering. This is the hazard.

---

## 3. Frames in flight (verified) — and the count is NOT 2

- `imageCount = min(minImageCount + 1, maxImageCount)` (`SwapChain.cpp:26-27`, recomputed `:87-88`).
- Present mode preference: MAILBOX, else FIFO (`SwapChain.cpp:350-359`). For both, `minImageCount` is
  typically 2-3, so **`imageCount` is 3 or 4 in practice, not 2.** (The previous audit hard-assumed 2; this
  is unverified and probably wrong. The user can confirm with one log line of `getImageCount()`.)
- Everything is sized to `imageCount`: `CommandPoolManager` (`Application.cpp:38`), the DDGI
  `m_framesInFlight` (`DeferredRenderer.cpp:31`), `Application::m_frameInFlightIndex % imageCount`
  (`Application.cpp:185`), the swapchain fences/semaphores (`SwapChain.cpp:196-214`).
- Two independent host-side throttles bound in-flight frames to `imageCount` (NOT to 1):
  1. **Command-pool reset wait.** `getPrimaryCommandBuffer → resetIfNeeded` waits on the pool's
     `m_pendingSignals` (the timeline values recorded by `addToBatch → markPendingSignal`,
     `VulkanQueue.cpp:113`) via `vkWaitSemaphores` (`CommandPool.cpp:95-113`). Pools cycle by
     `m_currentFrameIndex` over `framesInFlight` slots (`CommandPool.cpp:184-190`, `:210-213`). So recording
     `sceneCB` for frame F waits for the pool slot's *previous* user — frame **F − imageCount** — to
     complete. Bound: `imageCount` frames.
  2. **Acquire fence.** `RenderWindow::beginFrame → acquireImage(m_currentFrame)` does
     `vkWaitForFences(inFlightFences[semaphoreIndex])` (`SwapChain.cpp:280`), same-slot, cycling `imageCount`.
     Bound: `imageCount` frames.
- `getCommandPool(hash, frameIndex)` ignores its `frameIndex` arg (`CommandPool.cpp:174 (void)frameIndex`) and
  uses `m_currentFrameIndex`; harmless because they advance in lockstep, but the arg is dead.

**Net: the pipeline is `imageCount` (≈3-4) frames deep. `vkDeviceWaitIdle` is the only mechanism tried that
collapses it to 1.**

---

## 4. The atlas — single-buffered; every reader/writer

Single instances, created once, bindless index assigned once and never updated:
- `m_RadianceTexture` (`DynamicDiffuseGI.h:112`, created `.cpp:763`), bindless idx `.cpp:177-179`.
- `m_VisibilityTexture` (`.h:113`, created `.cpp:764`), bindless idx `.cpp:180-182`.
- Lighting reads them via push-const bindless handle: `pushConstants.probeIrradianceHandle =
  m_ddgi->getProbeIrradianceBindlessIndex()` (`LightingPass.cpp:191`), sampled in the fragment shader at
  `DeferredLighting.fs.glsl:468-469` → `IrradianceCommon.glsl:279` (distance `textureLod`) and `:325`
  (irradiance `textureLod`). **FRAGMENT stage.**

| # | Access | Where | R/W | Stage | Submit |
|---|--------|-------|-----|-------|--------|
| 1 | trace multibounce reads prev irradiance/distance (bindless) | `ProbeTrace.cs.glsl` (prevRadiance/prevVisibility handles, pushed `DynamicDiffuseGI.cpp:603-604`) | R | COMPUTE | sceneCB |
| 2 | blend reads prev EMA (storage image) | `ProbeBlending.cs.glsl` | R | COMPUTE | sceneCB |
| 3 | blend writes new EMA in place | `ProbeBlending.cs.glsl` (dispatches `DynamicDiffuseGI.cpp:672,691`) | W | COMPUTE | sceneCB |
| 4 | lighting samples atlas (bindless) | `IrradianceCommon.glsl:279,325` | R | **FRAGMENT** | sceneCB |

Within one frame these are correctly ordered by the DDGI barriers (§5) — trace-read → blend-RMW → post-blend
→ lighting-read. **Across frames they are not ordered at all** (§2.2).

Debug flatten passes are disabled (`DynamicDiffuseGI.cpp:769-776` commented). Not a factor.

---

## 5. Every barrier in the DDGI (verified against source, not comments)

All `vkCmdPipelineBarrier`s below are recorded into the **graphics** sceneCB (current path). Format:
`src→dst stage / oldLayout→newLayout / srcAccess→dstAccess`.

- **Pre-trace** (`castRays`, `:546-574`), 4 image barriers, `srcStage = firstFrame?TOP_OF_PIPE:COMPUTE`,
  `dstStage = COMPUTE`:
  - RayData: `{UNDEFINED|SHADER_READ_ONLY}→GENERAL / {0|SHADER_READ}→SHADER_WRITE`.
  - Radiance / Visibility / Classification: `{UNDEFINED|SHADER_READ_ONLY}→SHADER_READ_ONLY /
    {0|SHADER_READ}→SHADER_READ`. On non-first frames these are **no-ops** (same layout+access).
- **Post-trace** (`:621-625`): RayData `GENERAL→SHADER_READ_ONLY / SHADER_WRITE→SHADER_READ`, COMPUTE→COMPUTE.
- **Pre-relocate** (`:431-437`): ProbeOffset `UNDEFINED→GENERAL / 0→SHADER_WRITE`, COMPUTE→COMPUTE.
  (`UNDEFINED` oldLayout — offset contents discarded each frame; fine, relocation is deterministic.)
- **Post-relocate** (`:457-460`): ProbeOffset `GENERAL→SHADER_READ_ONLY / SHADER_WRITE→SHADER_READ`,
  COMPUTE→COMPUTE.
- **Pre-classify** (`:392-398`): Classification `UNDEFINED→GENERAL / 0→SHADER_WRITE`, COMPUTE→COMPUTE.
- **Post-classify** (`:419-422`): Classification `GENERAL→SHADER_READ_ONLY / SHADER_WRITE→SHADER_READ`,
  COMPUTE→COMPUTE.
- **Pre-blend** (`:638-653`), `srcStage = COMPUTE` (does **not** include FRAGMENT), `dstStage = COMPUTE`:
  - Radiance / Visibility: `SHADER_READ_ONLY→GENERAL / SHADER_READ→(SHADER_READ|SHADER_WRITE)`.
- **Post-blend** (`:698-712`), `srcStage = COMPUTE`, `dstStage = COMPUTE | FRAGMENT`:
  - Radiance / Visibility: `GENERAL→SHADER_READ_ONLY / SHADER_WRITE→SHADER_READ`.

The lighting pass adds **no** atlas barrier; it relies on the post-blend `dstStage=FRAGMENT` — correct
**within the one submit**.

### 5.1 Do pipeline barriers cross `vkQueueSubmit` boundaries? — settled: NO

A `vkCmdPipelineBarrier`'s two synchronization scopes cover only work in the same command buffer /
same submission batch (Vulkan spec, "submission order" gives ordering guarantees but a barrier's scopes do
not reach commands in a *different* `vkQueueSubmit`). Cross-submit ordering requires a semaphore, timeline
value, or fence. Therefore:
- the post-blend barrier correctly orders `blend_N (COMPUTE)` before `lighting_N (FRAGMENT)` — **same CB**;
- **no** DDGI barrier orders `sceneCB_N` against `sceneCB_N+1`. This is why attempt (e) (adding FRAGMENT to
  the pre-blend `srcStage`) did nothing: it targeted the right hazard (prior-frame lighting read vs this-frame
  blend write) with a mechanism structurally incapable of crossing the submit boundary.

---

## 6. The central question: what does `waitIdle` do that nothing else did?

`waitIdle` (`DeferredRenderer` experiment, top of `drawFrame`) drains **all** GPU work of frame N before frame
N+1 records/submits → exactly **1** frame in flight → the atlas is only ever touched by one frame at a time →
no concurrent read/write on single-buffered memory. That is the entire delta.

Every other attempt failed for a *structural* reason, not a tuning reason:
- **(a) probe-vs-probe CPU fence** (`m_ddgiFence`, `DynamicDiffuseGI.cpp:312-313,380`): only serializes the
  probe pass against itself; leaves `lighting_N` overlapping `blend_N+1`; and in the current Option-A path the
  fence code is **dead** (the `externalCB` branch returns at `:308` before reaching it).
- **(b) semaphore chaining probe submits**: same — orders probe vs probe only.
- **(c) synchronous in-order probe recording**: submission order ≠ execution dependency; barriers don't cross
  submits; `imageCount` frames still overlap.
- **(d) Option A (DDGI in graphics CB) + post-blend barrier**: fixes intra-frame only; the cross-frame hazard
  is between separate submits.
- **(e) FRAGMENT in pre-blend `srcStage`**: barriers don't cross submits (§5.1).
- **full-frame offscreen fence (2026-07-02 session)**: see §7.3 — it did **not** reduce to 1 frame in flight.
- **per-frame triple-buffered UBO (attempt 9)**: the UBO was never the atlas; irrelevant to the atlas race.

---

## 7. Reconciling the fence experiments (the crucial correction)

### 7.1 Why the acquire/inFlight fence permits overlap
`acquireImage` waits `inFlightFences[semaphoreIndex]` for the **same** slot it is about to reuse
(`SwapChain.cpp:280`), cycling `imageCount` slots. Same-slot wait = wait for frame **F − imageCount**. So
`imageCount` (≥2) frames are legitimately in flight and overlap.

### 7.2 Why the timeline/command-pool throttle permits overlap
Identical arithmetic: the pool reset waits on the slot's previous signal, i.e. frame **F − imageCount**
(`CommandPool.cpp:95-113`; slots cycle `framesInFlight = imageCount`). Bound = `imageCount`, not 1.

### 7.3 Why the "full-frame fence" experiment did nothing — and why the note's conclusion was WRONG
That experiment (INVESTIGATION_NOTES.md §"Session 2026-07-02") waited `offscreenFences[m_currentFrame %
imageCount]` at the top of `drawFrame` and submitted with the **same** slot's fence. Same-slot arithmetic ⇒
it waited for frame **F − imageCount**, i.e. it enforced the **exact same `imageCount`-deep pipeline that
already existed.** It added a redundant fence at the depth the command-pool/acquire throttles already
guaranteed. It never reduced concurrency below `imageCount`.

> The note claims this fence was *"exactly equivalent to `vkDeviceWaitIdle`'s effect on the GPU (at most N
> frames in flight)."* **This is false.** `waitIdle` ⇒ 1 frame in flight. The same-slot fence ⇒ `imageCount`
> (≈3-4) frames in flight. They are not equivalent, so the experiment did **not** test the hypothesis, and the
> subsequent deduction ("GPU execution overlap is ruled out; the fault must be host-side") does **not
> follow.** GPU overlap was never actually eliminated except by `waitIdle`.

To force 1 frame in flight with a fence you must wait for the **immediately previous** frame
(`fence[(F−1) mod imageCount]`, or a single shared fence), **before** recording — not the same slot you are
about to submit. No attempt in the log did this.

### 7.4 Why the previous version of THIS file's "Fix A" is also wrong
The prior `PIPELINE_AUDIT.md` recommended waiting `inFlightFence[m_currentFrame]` (same slot) before
recording. That is the identical same-slot arithmetic as the failed experiment → still `imageCount`-deep →
would **not** fix it. Discard that recommendation. Use §10.

### 7.5 Why rotation-freeze fixes it and hysteresis dampens it
Freeze rotation ⇒ after convergence every frame writes the *same* atlas values ⇒ concurrent frames read/write
identical data ⇒ the race is invisible. Higher hysteresis ⇒ per-frame atlas delta ≈ `(1−h)·(sampleₙ −
prev)` shrinks ⇒ a torn read blends nearly-identical states ⇒ smaller visible artifact. Both are consistent
only with a *cross-frame data race on the atlas*, not with a within-frame bug.

---

## 8. Root cause (ranked)

### RC-1 (STRONG — fits every datum): unsynchronized concurrent atlas access across in-flight frames
Up to `imageCount` (≈3-4) scene CBs execute concurrently on one queue (`§3`) with **no cross-submit
synchronization on the atlas** (`§2.1`, `§2.2`). Frame N+1's DDGI trace-read (#1) and blend-RMW (#2,#3) race
frame N's still-executing blend-write (#3) and, especially, lighting read (#4). Because the distance atlas is
read by the Chebyshev visibility test, a torn distance read makes occlusion move → "growing/shrinking
shadows"; a torn irradiance read → scene-wide shimmer. Rotation-driven, light-independent, scene-wide,
hysteresis-damped — all match.
- For: only `waitIdle` (1 in flight) fixes it; every fence left ≥2 in flight (§7); every barrier can't cross
  submits (§5.1); freeze/hysteresis behavior (§7.5).
- Against: none that survives §7. (The note's "host-side, GPU overlap ruled out" rests on the false
  equivalence corrected in §7.3.)

### RC-2 (AMPLIFIER, not independent): RaptureVK keeps MORE frames in flight than the reference, and has
### higher per-frame atlas variance
RTXGI is single-buffered too and *also* has an unsynchronized cross-frame atlas overlap (§9), yet is stable.
The difference is degree: (a) RTXGI is hard-capped at `MAX_FRAMES_IN_FLIGHT = 2`
(`RTXGI Graphics.h:18`), whereas RaptureVK runs `minImageCount+1` ≈ 3-4 (§3) — more concurrent atlas states to
tear between; (b) RTXGI's blend is ~0.8 ms vs ours ~1.6 ms — a 2× wider window where `blend_N+1` overlaps
`lighting_N`; (c) the sampling-side variance fixes in INVESTIGATION_NOTES (S1/S2/S5) reduced but did not zero
our per-frame atlas delta. This explains why the same structural race is invisible in RTXGI and visible here.

### RC-3 (RULED OUT): "host-side only, GPU sync is fine"
The note's fallback. Rests entirely on the §7.3 false equivalence. The UBO triple-buffer (attempt 9) already
showed the host UBO path is not it. No evidence remains for a purely host-side cause; `waitIdle`'s fix is
fully explained by the GPU-concurrency collapse to 1 frame.

---

## 9. Cross-check: how RTXGI's harness behaves (verified in `/home/Thomas/dev/RTXGI-DDGI`)

- `MAX_FRAMES_IN_FLIGHT = 2` (`samples/test-harness/include/Graphics.h:18`).
- One command buffer per frame, one submit: `SubmitCmdList` does a single `vkQueueSubmit(vk.queue, 1,
  &submitInfo, vk.fences[vk.frameIndex])` (`src/Vulkan.cpp` `SubmitCmdList`). Its only wait is
  `imageAcquiredSemaphore @ COLOR_ATTACHMENT_OUTPUT` — which, like ours, does **not** block the frame's DDGI
  compute.
- Main loop (`src/main.cpp:227-235`): `WaitForPrevGPUFrame` → `MoveToNextFrame` → record (DDGI update + probe
  blend + lighting all in the one CB) → submit → present. `WaitForPrevGPUFrame` =
  `vkWaitForFences(fences[vk.frameIndex])` (`src/Vulkan.cpp`), and `frameIndex` is advanced **after present**
  (`Vulkan.cpp:3410`). So it too waits the same slot ⇒ **2 frames in flight**, single-buffered atlas, DDGI
  compute of frame N+1 free to overlap lighting of frame N.

**Conclusion:** RTXGI does **not** structurally prevent this race — it merely runs at 2-in-flight with a
faster/lower-variance blend, so the tear is below the visible threshold. This *confirms* RC-1 + RC-2: the
hazard is real in both; visibility is a function of frames-in-flight × per-frame atlas delta. It also means
"copy RTXGI's structure" is not a fix — RTXGI has the same latent bug.

---

## 10. Recommended fix (correct whether or not compute and graphics are the same queue)

The blend is a temporal EMA: `atlasₙ₊₁ = mix(sampleₙ₊₁, atlasₙ, h)`. Frame N+1's blend has a **genuine data
dependency** on frame N's blend output, and frame N's lighting **reads** that same memory. This dependency is
inherently serial and single-buffering cannot be parallelized away. So the fix must create a real
**cross-submit GPU dependency** ordering each frame's atlas producer after the previous frame's atlas
consumer.

### Fix (primary) — serialize the atlas chain with the queue's existing timeline semaphore
Make `sceneCB_N`'s submit **wait** on the graphics queue's timeline semaphore at the value signaled by
`sceneCB_{N-1}`, at `dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT` (the first atlas access). The queue
already signals `m_timelineSemaphore` at a monotonic value on every batch submit
(`VulkanQueue.cpp:255,268`); record that value per frame and have the next frame's batch submit wait
`value_{N-1}`. Because a timeline wait at COMPUTE blocks frame N's DDGI until frame N-1's whole scene CB
(including `lighting_{N-1}`'s atlas read) has completed, this closes the WAR (#4 vs #3), the WAW/RAW (#3 vs
#3), and the trace-read hazard (#1) simultaneously.
- This is what `waitIdle` achieves for the atlas, but **on the GPU**: no host stall, and it does not drain the
  compute/transfer queues or block unrelated work. Throughput is ~1 scene-frame in flight (same as the
  `waitIdle` workaround), which is acceptable and correct.
- **Queue-agnostic:** semaphores synchronize across queues. If DDGI is later moved back onto the compute
  queue, have the compute submit wait the graphics timeline value from the previous frame's lighting and have
  lighting wait the compute submit — a cross-queue semaphore chain. (A `VkFence` would NOT be sufficient in
  the multi-queue case; it only orders host↔queue, not queue↔queue. Prefer the semaphore.)

Implementation note: the current submit is issued by the UI layer's `submitAndFlushQueue`
(`RenderWindow.cpp:117`) merging the batched scene CB with the UI CB. The batch `VkSubmitInfo`
(`VulkanQueue.cpp:262-268`) currently sets **no** wait semaphores; add a timeline wait there (or give
`addToBatch` a "wait on prior-frame timeline value" facility). Keep the UI submit's `imageAvailable` wait
untouched.

### Then re-measure, and only if you want the tear gone *and* pipelining back
1. First cheap experiment: cap frames-in-flight at 2 (match RTXGI) and observe — per RC-2 this alone may drop
   the dance to RTXGI's (invisible) level, and it is a one-line change to `imageCount`/`m_framesInFlight`
   sizing. This tells you how much is frame-count vs variance.
2. To keep raster pipelining while still serializing only the atlas: move DDGI back into its **own** compute
   submit, chained frame-to-frame by a timeline semaphore (producerₙ₊₁ waits producerₙ AND lighting_ₙ), and
   have the graphics lighting submit wait the DDGI producer of the same frame. This serializes only the
   ~1.6 ms atlas chain, letting the heavy G-buffer/lighting of frame N+1 still overlap frame N's tail. More
   code; do it only if the primary fix's throughput is insufficient.

Do **not** rely on: more barriers (can't cross submits, §5.1), a same-slot fence (still `imageCount`-deep,
§7.3-7.4), or multi-buffering alone (the EMA RAW edge still needs the cross-frame semaphore).

---

## Appendix: verified references

| Fact | File:Line |
|------|-----------|
| DDGI recorded inline into graphics CB | `DeferredRenderer.cpp:296`; branch `DynamicDiffuseGI.cpp:301-309` |
| scene CB `addToBatch`, no submit (OFFSCREEN) | `DeferredRenderer.cpp:148` |
| UI merges + submits scene CB, 2 VkSubmitInfo, 1 vkQueueSubmit | `RenderWindow.cpp:110-118`; `VulkanQueue.cpp:270-274` |
| scene (batch) submit has NO wait semaphore | `VulkanQueue.cpp:262-268` |
| imageCount = minImageCount + 1 | `SwapChain.cpp:26-27,87-88` |
| present mode MAILBOX else FIFO | `SwapChain.cpp:350-359` |
| in-flight bound = imageCount via acquire fence (same slot) | `SwapChain.cpp:280`; `RenderWindow.cpp:78,144` |
| in-flight bound = imageCount via pool reset timeline wait | `CommandPool.cpp:95-113,184-190,210-213`; `VulkanQueue.cpp:113` |
| frame index advance | `Application.cpp:185`; `CommandPool.cpp:212` |
| atlas single-buffered | `DynamicDiffuseGI.h:112-113`; `.cpp:763-764` |
| atlas bindless idx set once | `DynamicDiffuseGI.cpp:177-182` |
| lighting reads atlas (bindless, FRAGMENT) | `LightingPass.cpp:191`; `DeferredLighting.fs.glsl:468-469`; `IrradianceCommon.glsl:279,325` |
| blend RMW dispatch | `DynamicDiffuseGI.cpp:672,691` |
| post-blend barrier dst = COMPUTE\|FRAGMENT | `DynamicDiffuseGI.cpp:708-712` |
| pre-blend barrier src = COMPUTE only | `DynamicDiffuseGI.cpp:650-653` |
| dead probe-vs-probe fence in Option-A path | `DynamicDiffuseGI.cpp:301-313,380` |
| queues keyed by family; same family ⇒ same VkQueue | `VulkanContext.cpp:1151-1186,199-253` |
| per-frame DDGI NOT on compute queue (only clearTextures) | `DynamicDiffuseGI.cpp:240,296` |
| queue timeline semaphore signalled per batch submit | `VulkanQueue.cpp:255,268` |
| RTXGI MAX_FRAMES_IN_FLIGHT = 2 | `RTXGI-DDGI/samples/test-harness/include/Graphics.h:18` |
| RTXGI one submit/frame, waits imageAcquired only | `RTXGI-DDGI/samples/test-harness/src/Vulkan.cpp` SubmitCmdList |
| RTXGI same-slot WaitForPrevGPUFrame ⇒ 2 in flight | `RTXGI-DDGI/.../Vulkan.cpp` WaitForPrevGPUFrame + `:3410` |
</content>
</invoke>
