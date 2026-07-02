# DDGI vs RTXGI investigation

# ##################################################################################################
# >>> ROOT CAUSE FOUND (2026-07-02) + FIX IMPLEMENTED: per-frame repoint of a shared descriptor <<<
# ##################################################################################################
#
# The opus atlas-race audit (PIPELINE_AUDIT.md) was WRONG. Its FIX #1 (self-chain the graphics batch on
#   the queue timeline) was implemented and REVERTED: it changed nothing and cost no fps, i.e. it was a
#   no-op that correctly serializing the atlas on the GPU did nothing -> the dance is NOT a GPU atlas race.
#
# ACTUAL ROOT CAUSE: the probe-info UBO (ProbeVolume, holds the per-frame probeRayRotation) was bound
#   through ONE shared descriptor (set 0, binding 5) that updateProbeVolume repointed every frame via an
#   immediate vkUpdateDescriptorSets. The set is UPDATE_AFTER_BIND, so descriptors are read at SHADER
#   EXECUTION time -> the 3 "per-frame" UBOs were defeated: whichever in-flight frame's shader ran last read
#   whatever slot was last repointed. Worse, a frame N+1 repoint landing between frame N's trace and blend
#   made trace and blend read DIFFERENT rotations -> ray directions desync -> the dance. This fits every
#   clue: waitIdle fixes it (frame N done before N+1 repoints), freezing rotation fixes it (all slots equal),
#   the 3-UBO attempt did nothing (single descriptor still repointed), the atlas GPU-serialization did
#   nothing (CPU repoint races regardless).
#
# FIX (implemented): make binding 5 a descriptor ARRAY (count 3 in DescriptorManager). Bind all 3 elements
#   ONCE in initProbeInfoBuffer (never repointed); only per-slot buffer CONTENT is rewritten each frame.
#   Each frame's passes index their own element via a new volumeSlot push constant (= frameIndex, the global
#   frame-in-flight index from DeferredRenderer). Shaders declare u_probeInfo[] (runtime array, like the
#   other set-0 bindless arrays) and do `u_volume = u_probeInfo[pc.volumeSlot].volume` at main() top; lighting
#   indexes [0] (static fields only). Files: DescriptorManager.cpp, DynamicDiffuseGI.{h,cpp}, ProbeTrace/
#   ProbeBlending/ProbeRelocation/ProbeClassification.cs.glsl, DeferredLighting.fs.glsl.
#
# ##################################################################################################
# >>> ORIGINAL "START HERE" (kept for history): the mystery as it stood before the audit <<<
# ##################################################################################################
#
# THE MYSTERY (unsolved): the DDGI probe atlas "dances" (scene-wide shimmer). ONE thing fixes it perfectly:
#   `m_renderContext.vulkanContext->waitIdle()` at the TOP of DeferredRenderer::drawFrame (1 frame in flight).
# EVERY targeted attempt to reproduce that fix more cheaply did NOTHING:
#   - CPU fence serializing the probe pass vs itself (probe_N+1 waits probe_N GPU-complete)  -> no effect
#   - per-frame binary semaphore chaining probe submits                                       -> no effect (ran ~200fps)
#   - synchronous (main-thread, in-order) probe submission + existing barriers                -> no effect
#   - recording the probe pass INTO the frame graphics CB (Option A) + post-blend barrier      -> no effect
#   - adding FRAGMENT_SHADER to the pre-blend barrier srcStage (wait for prev lighting read)   -> no effect
#   - freezing the atlas after convergence                                                     -> trivially stable, proves nothing
# Meanwhile freezing the ray rotation also stops it, and higher hysteresis only dampens it.
#
# We concluded (maybe wrongly - RE-VERIFY) it's a cross-frame single-buffered-atlas race, probably frame N+1's
# blend WRITE vs frame N's lighting READ. But NONE of the targeted syncs reproduced waitIdle's fix, which does
# not fit that theory cleanly. Something about the actual pipeline is NOT what we assumed.
#
# NEXT SESSION PLAN: spawn agents to do a ZERO-ASSUMPTION full audit of the render pipeline. Verify against
# source (file:line), assume NOTHING about how any of this is wired:
#   1. Queues: on THIS machine compute/graphics/present/transfer are the SAME VkQueue (confirmed by user) -
#      m_queues is keyed by family index and they share one family. But do NOT assume it (verify in source, and
#      the fix must be correct whether or not they're the same queue). How is the DDGI compute submit ordered
#      vs the graphics submit? Who submits graphics in the EDITOR path (AmethystLayer addToBatch + its own
#      submit) and with what wait/signal semaphores + fences?
#   2. Frames in flight: exact count (swapchain image count). How is m_frameInFlightIndex advanced/gated? Is the
#      probe pass's per-frame command pool actually per-frame, and does it alias across frames?
#   3. The atlas: is m_RadianceTexture/m_VisibilityTexture truly single-buffered? Bindless index lifetime. Who
#      reads it (trace multibounce, blend RMW, lighting) and in which submit/queue/stage each.
#   4. EVERY barrier in the DDGI (pre-trace, rayData, relocate, classify, pre/post-blend) AND the lighting pass's
#      atlas access: exact srcStage/dstStage/srcAccess/dstAccess/oldLayout/newLayout. Do NOT trust the comments.
#   5. THE core question: what does waitIdle actually drain that fence/semaphore/barrier attempts do not? That
#      delta is the bug. Build the precise happens-before graph across two consecutive frames.
#   6. Cross-check against RTXGI's harness pipeline (single-buffered atlas too - how does IT avoid this?).
# CURRENT CODE STATE (all of this still DANCES - these are proven-ineffective attempts left IN PLACE on purpose
# so the audit agents don't re-recommend them; only the last FRAGMENT-barrier change was reverted):
#   - Probe pass runs SYNCHRONOUSLY on the main thread (DeferredRenderer::drawFrame calls populateProbesCompute
#     directly, NOT via the async JobSystem job). In-order submission. Still dances.
#   - A CPU VkFence (m_ddgiFence) serializes the probe pass vs itself (wait at top of populateProbesCompute,
#     signal on its submit). Ineffective, and it stalls fps - ignore the fps, it's diagnostic.
#   - Pre-blend barrier srcStage reverted to COMPUTE-only (the FRAGMENT addition did nothing).
#   - Debug flatten passes DISABLED (they read the atlas + used an UNDEFINED-oldLayout barrier + cost fps).
#   - Shader-side fixes from earlier this session are KEPT (real improvements): S1 black-probe hack removed,
#     S2 chebyshev floor 0.05, S5 surfaceBias normalBias=1.0. Those are NOT part of the dance race.
# The full attempt log is below ("CROSS-FRAME ATLAS RACE - SYNC ATTEMPT LOG").
# ##################################################################################################

Goal: ours "dances" (~95% stable at hysteresis 0.99); RTXGI is rock-stable at hysteresis 0.97 and blends in ~0.8ms vs our ~1.6ms. Same Sponza, no AO, no variability, 256 rays/probe.

Key user observations:
- RTXGI **visibility/distance texture is super stable**; ours dances. Distance depends only on static geometry + per-frame ray rotation, so identical blend math should be equally stable => suspect INPUT noise or ray-direction/rotation differences.
- RTXGI more stable at LOWER hysteresis (0.97) than ours at 0.99 => our input variance sigma^2 is HIGHER. Chase the trace/ray-gen, not the blend filter.
- Center-of-Sponza probes: filled in RTXGI irradiance atlas, empty in ours (likely classification convention/threshold). User says probably inactive anyway; low priority.

## Findings

### F1 (PERF, confirmed): no shared-memory cooperative load
`ProbeBlending.cs.glsl` recomputes `DDGIGetProbeRayDirection(rayIndex)` (spherical-fibonacci trig) and re-`texelFetch`es RayData for EVERY interior texel * EVERY ray.
- irradiance: 6x6=36 texels * 256 rays; distance: 14x14=196 texels * 256 rays per probe.
RTXGI (`ProbeBlendingCS.hlsl` + `RTXGI_DDGI_BLEND_SHARED_MEMORY`) cooperatively loads radiance/distance/direction into groupshared ONCE per group (each thread loads a few), syncs, then texels read LDS.
=> explains the ~2x blend cost. Fix: port the shared-memory path. Perf only, not the dance.

### F2 (correctness): hardcoded thresholds in irradiance blend
Ours: `probeIrradianceThreshold = 0.5` HARDCODED; `hysteresis = max(0, hysteresis - 0.5)`.
RTXGI: uses `volume.probeIrradianceThreshold` (UI shows 0.2) and `hysteresis - 0.75`.
Effect: our probes react differently to lighting-change detection. Not obviously the dance (ours triggers less often), but a real divergence from reference. Should use the volume field.

### Ruled out (match RTXGI exactly)
- Per-frame rotation: Arvo's method, identical matrix + quaternion. Uniform random, same distribution.
- DDGIGetProbeRayDirection / SphericalFibonacci: identical. probeStaticRayCount=32 == RTXGI_DDGI_NUM_FIXED_RAYS.
- RayData texture is rgba32f (full precision) - no fp16 quantization noise.
- Distance blend math: identical EMA `mix(cur, prev, hysteresis)`.
=> our per-frame blend INPUT is NOT more random than RTXGI. The atlas dance magnitude is likely similar
   (user confirms RTXGI's atlas also shows the shadow-grow at that spot). The disco is on the SAMPLING side.

## SAMPLING-SIDE divergences (IrradianceCommon.glsl vs RTXGI Irradiance.hlsl) -- prime disco suspects

### S1 (STRONG): non-RTXGI "black probe" weight hack -- IrradianceCommon.glsl:331-335
```
float probeLength = length(probeIrradiance);
if (probeLength < 0.001) { weight *= 0.1; }
```
Not in RTXGI. A probe whose sampled irradiance hovers around length 0.001 (exactly what happens in dark/
indirect-only areas) flips its weight 10x on/off frame-to-frame => discrete blend jump => DISCO in dark areas.
This is the clearest match to "disco on non-illuminated areas". ACTION: remove it.

### S2 (MODERATE): chebyshev visibility floor -- IrradianceCommon.glsl:298
Ours `weight *= max(0.01, chebyshevWeight)`; RTXGI `max(0.05, ...)`. Lower floor = higher occluded/visible
contrast = more sensitive to distance-atlas movement at shadow boundaries => more shadow-edge flicker.
ACTION: match 0.05.

### S3 (MINOR): extra octahedral inset clamp -- IrradianceCommon.glsl:270-272, 317-319
Ours clamps octantCoords inward by half a texel before DDGIGetProbeUV; RTXGI does not (relies on border
texels + bilinear). May be a workaround for a border-copy issue. Flag for border-pass review, not dance.

### S4: classification skip condition -- IrradianceCommon.glsl:240-241
Ours skips adjacent probe only if `probeClassificationEnabled>0 && state!=ACTIVE`; RTXGI skips whenever
`state==INACTIVE` unconditionally. Also relates to "center probes empty": our classification likely marks
center probes inactive (RTXGI keeps them active). If probes flip active/inactive per frame this is also a
discrete flicker source. Check ProbeClassification thresholds next.

## Priority
1. [DONE] S1 remove black-probe hack.
2. [DONE] S2 chebyshev floor 0.01->0.05.
3. F2 - DISMISSED by user (not relevant to this scene).
4. S4 - NOT A BUG per user: we clear atlas slots when a probe goes inactive; RTXGI may not. Center-empty is expected.
5. F1 shared-memory blend (perf only, later).

## FINAL SAMPLING LOGIC investigation

Combination (DeferredLighting.fs.glsl:454-477, 584-596): Lambertian `irradiance * albedo/PI * kD * ao`,
blendWeight fade, direction=shading normal. All reasonable. DDGIGetVolumeBlendWeight matches RTXGI.

### S5 (REVISED - NOT a bug; it's the newer paper's bias. Real question = parameter scale/wiring)
Ours is the newer self-shadow bias (Majercik follow-up / RTXGI "qualitative image improvements"):
`(n*0.2 + wo*0.8) * (0.75*D) * B`. The view dir IS included (0.8 term). So it is NOT ignoring view in
effect and is a legit, arguably better formula than RTXGI's simple `n*nb + view*vb`. Downgraded from "bug".

Two things still worth questioning (not correctness bugs):
- Scalar B = volume.probeNormalBias. So the "Probe View Bias" UI slider (0.3) is inert. Intended?
- SCALE: the new formula is already pre-scaled by 0.75*D (fraction of probe spacing), so B is meant to be
  ~O(1). User feeds B=0.1 (a value tuned for the OLD absolute-units formula) => bias ~0.034, ~10x smaller
  than RTXGI's ~0.3. Too-small bias keeps the Chebyshev sample near the self-occlusion boundary => more
  sensitivity to atlas movement. HYPOTHESIS (test, don't assume): raising probeNormalBias toward ~1.0
  reduces the dance/leak. This is a value experiment for the user, not a code change.

### (OLD overclaim, retracted): "DDGIGetSurfaceBias ignores probeViewBias / is THE dance driver"

### S5 RESULT: normalBias=1.0 CONFIRMED partial fix - the square spot on the curtain is GONE.
Bias scale was a real contributor. But scene still not fully stable => residual dance is a separate issue.
TODO: pick a sane default B for the new formula (~0.5-1.0) once we settle the rest.

## Residual dance - additional items RULED OUT (all match RTXGI)
- Fixed rays are NOT rotated (ProbeCommon.glsl:145 `if(isFixedRay) return normalize(direction);`) => relocation
  & classification get identical input every frame => offsets converge. Matches RTXGI exactly.
- Relocation algorithm: faithful port (move out of backfaces / away from near frontface / back toward 0, then
  ellipsoid clamp 0.45). Same as RTXGI.
- Config matches RTXGI reference: 256 rays, 32 fixed, 8/16 texels, grid 22^3, spacing(1.02,0.5,0.45),
  hysteresis 0.97, maxRayDist 10000. probeNormalBias now 1.0.
- Trace multi-bounce: samples prev-frame irradiance at hits (line 409), gain = albedo/pi*(2pi integ) < 0.9/bounce
  => damped, same as RTXGI. Radiance clamped to [0,1] (only REDUCES variance).
- Barriers exist between all passes (trace write->read @592, per-atlas transitions per pass). No raw sync gap.

## Where we are
Major amplifiers fixed: S1 (black-probe weight flip), S2 (chebyshev floor 0.05), S5 (bias scale, normalBias=1.0
killed the curtain square). Input pipeline verified byte-equivalent to RTXGI. No further clear code bug found by
static diffing. Residual "not stable" is either the inherent fixed-alpha noise floor or something only visible
empirically.

## LEADING HYPOTHESIS (fits ALL evidence): missing compute->graphics sync on the probe atlas
New evidence: dances with a SINGLE light (sky-only OR sun-only) => NOT input radiance variance; it's structural.
Recap of constraints any cause must satisfy: rotation-driven (freeze stops it), hysteresis-dampened,
light-source-independent, scene-wide, shaders/config identical to RTXGI.

Trace (DeferredRenderer.cpp drawFrame):
- DDGI blend runs as a fire-and-forget async COMPUTE job (line ~104, JobDeclaration counter = nullptr => nothing
  waits on it).
- Its submit signals NO semaphore: DynamicDiffuseGI.cpp:351 `m_computeQueue->submitQueue(cb, nullptr, nullptr)`.
- Graphics submit waits only on swapchain image-available (line ~129).
- getComputeQueue()/getGraphicsQueue() index m_queues by FAMILY index; on this HW compute==graphics family =>
  SAME VkQueue. So ordering is by submission order, but the async job (worker thread) races the main-thread
  graphics submit. No semaphore => no execution/memory dependency either way.
=> lighting samples this-frame vs last-frame atlas inconsistently per frame => scene-wide flicker of exactly the
   rotation noise-floor magnitude. Freeze rotation => consecutive atlases identical => no flicker. Higher
   hysteresis => atlases closer => less flicker. Matches everything.
Lighting samples the atlas DIRECTLY (probeIrradianceHandle = m_RadianceTexture bindless idx) - no copy/filter step.

CONFIRMED by test: synchronous populate + vulkanContext->waitIdle() before graphics => dance stops. But waitIdle
fixes BOTH candidate hazards at once, so it does not tell us which:
  (a) graphics samples the atlas without syncing to the compute that writes it (compute->graphics).
  (b) consecutive-frame DDGI compute passes race on the single-buffered atlas (write-after-read / write-after-write
      across frames) - "two DDGI passes bullying each other".

FIX IMPLEMENTED = Option A (clean, single linear chain, no async/semaphore):
- populateProbesCompute(Scene&, CommandBuffer*, uint32_t) now RECORDS into the frame's graphics command buffer
  (no own pool/begin/end/submit). Called on the MAIN thread from DeferredRenderer::recordCommandBuffer, at the
  front of the frame CB, before shadows/gbuffer/lighting. (Main thread avoids the 2-threads-one-CB race; a
  secondary CB in a job is the later optimization if recording cost matters.)
- The async DDGI job + the waitIdle test are removed.
- clearTextures() keeps its own one-time init submit on the compute queue (fine).
- Chain: [DDGI trace->relocate->classify->blend ->(existing post-blend GENERAL->SHADER_READ_ONLY,
  COMPUTE->FRAGMENT barrier)] -> shadows -> gbuffer -> lighting samples atlas. One queue, one CB.

ISOLATION (self-contained in Option A): the base fixes hazard (a) only; it does NOT protect (b) because the
pre-blend barrier srcStage is COMPUTE_SHADER only (misses the prior frame's FRAGMENT_SHADER read).
  - If the dance is GONE after building => cause was (a); (b) not broken; add nothing.
  - If it REMAINS => (b) is real; add VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT to the pre-blend barrier srcStage
    (DynamicDiffuseGI.cpp blendTextures, ~line 621) to order prior-frame lighting read before this-frame blend write.

## NEXT = empirical localization (use the Texture Viewer we built)
1. View [DDGI] Distance Flattened: is it stable now? 
   - stable  => residual is in IRRADIANCE/lighting (multibounce noise floor). 
   - dances  => residual is in the DISTANCE pipeline specifically.
2. Feedback test: zero the trace's indirect term (set `irradiance=0` before line 427) for one run.
   - dance stops => it's the multibounce feedback amplifying the per-frame rotation noise floor (=> want a
     bigger effective sample or temporal stabilization, i.e. RTXGI's probe variability / higher hysteresis).
   - dance remains => single-bounce direct noise; look at trace direct lighting determinism.
Perf (F1 shared-memory blend) still available, independent of the dance.
`ProbeCommon.glsl:257-271`:
```
float D = min(min(spacing.x, spacing.y), spacing.z);
biasVector = (surfaceNormal * 0.2 + samplePointToCamera * 0.8) * (0.75 * D) * volume.probeNormalBias;
```
RTXGI (`Irradiance.hlsl:28-31`):
```
return surfaceNormal * probeNormalBias + (-cameraDirection) * probeViewBias;
```
(-cameraDirection == our samplePointToCamera == V, surface->camera.)

Problems:
- probeViewBias is COMPLETELY IGNORED. User has it set to 0.3 in the UI; it does nothing.
- Magnitude is ~10x smaller: user spacing (1.828, 0.5, 0.45) => D=0.45, so bias = 0.75*0.45*0.1 = 0.034 * dir.
  RTXGI: normal*0.1 + view*0.3 ~= 0.3-magnitude.
surfaceBias offsets biasedWorldPosition used by the Chebyshev occlusion test. Too-small bias puts the sample
point right on the self-occlusion boundary => chebyshev is hypersensitive to distance-atlas movement =>
shadow grow/shrink dance + splotches; the missing VIEW bias is the standard DDGI leak/self-shadow fix.
This best explains why ours dances far more than RTXGI despite identical atlas inputs.
ACTION: replace with RTXGI's `surfaceNormal*probeNormalBias + samplePointToCamera*probeViewBias`.

# =====================================================================================================
# CROSS-FRAME ATLAS RACE — the real remaining dance. SYNC ATTEMPT LOG
# =====================================================================================================

## Established facts
- Full GPU serialization (`vulkanContext->waitIdle()`, either after the probe pass OR at the very top of
  drawFrame = 1 frame in flight) STOPS the dance. => the dance is frame N+1's probe pass overlapping frame N's
  on the SINGLE-BUFFERED irradiance/distance atlas. Freeze-rotation also stops it (same reason: identical
  atlas each frame). Higher hysteresis only dampens it.
- The atlas is single-buffered and updated by an in-place temporal EMA: frame N+1's blend READS the atlas
  frame N produced. That read-after-write dependency is inherent to the algorithm; NO amount of double/triple
  buffering removes it (N+1 still must read N's output). => the probe pass MUST be serialized frame-to-frame.
- CORRECTION: a pipeline barrier does NOT create a dependency across separate vkQueueSubmit calls, even in
  submission order. Its reach is within one submission. Cross-submit ordering needs a semaphore/fence (or put
  the work in one submit). This is why every barrier-only attempt failed.
- CORRECTION: "-40 fps" was 240->200 (a drop), NOT a hang/deadlock. The semaphore attempt RAN FINE; it just
  did not fix the dance. So there was never a submit-order-inversion deadlock; that theory was wrong.

## Attempts and results
1. BASELINE (original): probe pass in a fire-and-forget COMPUTE job, own CB + own submit, no cross-frame sync,
   single-buffered atlas. => DANCES.
2. waitIdle right AFTER the (synchronous) probe pass. => FIXES dance (full stall). First confirmation.
3. POPULATE_INTO_GRAPHICS_CB: populateProbesCompute(Scene&, uint32_t, CommandBuffer*) records all DDGI commands
   (castRays, relocate, classify, blend) into the CALLER's command buffer instead of creating its own. No own
   begin/end/submit/fence. The graphics CB's lifecycle (begin/end/addToBatch) is managed normally by DeferredRenderer.
   Called from recordCommandBuffer at the front of the frame CB, before shadows/gbuffer/lighting. The post-blend
   barrier (COMPUTE→COMPUTE|FRAGMENT, GENERAL→SHADER_READ_ONLY) and the lighting fragment reads are now in the SAME
   command buffer. Built and tested.
   => RESULT: ZERO change. Dance identical to baseline.
5. pre-trace barrier srcAccess SHADER_READ -> SHADER_WRITE. => NO CHANGE (RAW already covered by post-blend).
5. Per-frame binary semaphore chain (frame N signals sem[N], N+1 waits sem[N-1]) on the ASYNC job path.
   - First had a command-pool THREADING bug (worker job used the main-thread pool m_commandPoolHash) -> device
     lost / crash. Root cause: I dropped the original per-worker-thread pool (keyed by this_thread::get_id()).
   - After restoring the per-thread pool: RAN at ~200fps (down from 240), but dance NOT fixed. => the semaphore
     as written did not actually serialize the probe pass (or serialize-probe-vs-probe alone isn't the fix).
6. Synchronous probe pass on the MAIN thread (in-order submit) + existing barriers, no semaphore.
   => STILL DANCES. Confirms barriers don't serialize across submits regardless of submit order.
7. waitIdle at the TOP of drawFrame (1 frame in flight). => FIXES dance. Confirms it's cross-frame overlap.
8. FENCE: synchronous probe pass; a VkFence created signaled; at the top of populateProbesCompute
   `vkWaitForFences`+`vkResetFences`, and the probe submit signals it. CPU-blocks frame N+1's probe recording
   until frame N's probe pass fully completes on the GPU. Bulletproof probe-vs-probe serialization.
   => RESULT: DID NOT FIX. So probe-vs-probe overlap is NOT the dance.

9. PER-FRAME UBO (triple-buffered ProbeVolume): The PIPELINE_AUDIT.md (section 7) concluded the dance
   was a host-device race on the single-buffered `m_ProbeInfoBuffer` (HOST_VISIBLE|HOST_COHERENT UBO
   written every frame while GPU still reading it). Assumed: the UBO race corrupts both atlas sampling
   coordinates in the lighting pass and ray directions in the DDGI trace. Attempted: replaced
   `m_ProbeInfoBuffer` (singular) with `std::vector` of `m_framesInFlight` (3) UBOs; `updateProbeVolume`
   now writes to slot `frameIndex % m_framesInFlight` and calls `m_probeInfoBinding->update()` to
   repoint the descriptor set 0 binding 5 via `vkUpdateDescriptorSets` (safe with UPDATE_AFTER_BIND).
   The descriptor pool + layout use `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT` — verified.
   => RESULT: NO DIFFERENCE. Dance identical to fence/barrier attempts.
   => The UBO race is NOT the root cause. The ProbeVolume data reaches the shaders correctly; the
      corruption is elsewhere.

## KEY DEDUCTION from the fence result
- The probe pass is now strictly serialized frame-to-frame, yet it still dances.
- The ONLY thing waitIdle-at-frame-top does that the fence does NOT: also wait for the previous frame's
  GRAPHICS (the lighting pass). Fence waits only for the probe pass.
- The only atlas access in graphics is the LIGHTING pass READING the atlas.
=> The hazard is CROSS-FRAME WRITE-AFTER-READ: frame N+1's blend WRITES the single-buffered atlas while frame
   N's LIGHTING is still READING it. (Not probe-vs-probe.) waitIdle fixes it because it also drains lighting_N
   before probe_N+1; the fence doesn't.
- Note: the atlas *content* (probe-vs-probe EMA) should be clean under the fence; the visible dance would then
   be the lighting sampling a half-rewritten atlas (torn read), not the atlas oscillating.

## Session 2026-07-02: Full-frame fence (replaces addToBatch) -- DID NOT FIX

### What was tried
Previous attempts serialized only the PROBE pass (populateProbesCompute). The probe pass fence (m_ddgiFence) failed
to fix — the working theory was that the hazard was probe N+1 writing vs lighting N reading (cross-frame WAR),
not probe-vs-probe. Therefore, we needed to serialize the ENTIRE offscreen frame (probes + lighting together).

Built a per-frame VkFence system (`m_offscreenFences`) in DeferredRenderer:
1. Created `m_swapChain->getImageCount()` fences, pre-signaled (constructor)
2. At the TOP of drawFrame (OFFSCREEN only): `vkWaitForFences` + `vkResetFences` by `m_currentFrame % getImageCount()`
3. Replaced `addToBatch(commandBuffer)` with `submitQueue(commandBuffer, ..., fence)` — submits immediately,
   signals the fence when the entire CB (DDGI + gbuffer + lighting) completes on the GPU
4. Fence destroyed in destructor

### Result: ZERO change. Dance identical to baseline.
Despite the fence ensuring at most N (swapchain image count = 2 on this machine) frames GPU-in-flight — exactly
equivalent to `waitIdle`'s effect on the GPU — the dance persists. The only difference between this fence and
`vkDeviceWaitIdle`:

- The fence only gates the DeferredRenderer's offscreen submits. Other queues' submissions (UI, AmethystLayer,
  present) are NOT gated. On this machine they share the same VkQueue, so they're ordered by Vulkan queue
  submission ordering, but the fence doesn't explicitly wait for them.
- `vkDeviceWaitIdle` waits for ALL queue operations on ALL queue handles, plus host-side memory operations.
- `vkDeviceWaitIdle` drains command pool resets and host-coherent memory writes.

### Revised thinking
Since a GPU-only per-frame VkFence is equivalent to `vkDeviceWaitIdle` from the GPU perspective (at most N frames
in flight, full serialization of DeferredRenderer's submissions), and the fence does NOT fix the dance while
`waitIdle` DOES, the root cause is NOT a GPU synchronization gap.

The fault must be HOST-side — something only `vkDeviceWaitIdle` drains that a VkFence does not:
1. Host-to-device memory writes not yet visible (despite HOST_COHERENT on the buffer)
2. Command pool reset racing with GPU reads of stale descriptors/resource references
3. Some other pending host-side operation

BUT: the fence ensures the GPU is done with the old CB before the pool is reset (the fence wait happens BEFORE
`getPrimaryCommandBuffer()` → `resetIfNeeded()`), eliminating (2). And the UBO is HOST_COHERENT, eliminating (1).

Possibilities that remain:
- The fence IS working (GPU fully serialized) but the dance is NOT caused by GPU execution overlap. Instead,
  `waitIdle`'s ~16ms delay changes some CPU-timing-dependent behavior (e.g., how many probes complete per
  frame, or RNG state divergence). The dance would then be a convergence/sampling artifact, not a sync bug.
- OR there is a fence bug in this code that `waitIdle` doesn't have (e.g., the wrong fence is waited on,
  the fence is never signaled, etc.) — but the code was carefully reviewed.
- OR a subtle issue with the timeline semaphore interaction: `submitQueue` signals `m_immediateTimeSema`
  (a different timeline than `m_timelineSemaphore` used by `addToBatch`/`flush`). The `resetIfNeeded()` in the
  command pool waits on `m_pendingSignals` which records the `m_immediateTimeSema` value from the last
  submit. This should be fine — the fence already guarantees GPU completion of that submit.

### Conclusion
This attempt conclusively rules out "GPU execution overlap" as the root cause. The dance survives even with
full GPU frame serialization. The remaining possible causes are:
1. A CPU-timing-dependent sampling artifact (masked by waitIdle's stall)
2. A subtle bug in the DDGI shader state that resolves with time but not with frame-accurate sync
3. Something that only `vkDeviceWaitIdle`'s comprehensive drain catches (all queue handles, deferred
   operations, memory coalescing)

The next diagnostic should test the SWAPCHAIN path (present mode) which has its own proven synchronization
(swapchain semaphores + inFlightFence) with zero additional sync code to debug. If that also dances, the
bug is not synchronization at all.

## Separate issue (not the dance)
- fps drop this session = the 5 debug flatten passes re-enabled for the Texture Viewer, running every frame.
  Gate them behind the viewer being open, or disable. Unrelated to the dance.
