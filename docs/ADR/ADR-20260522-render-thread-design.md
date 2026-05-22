# ADR-20260522 — Render thread design

## Bağlam

Phase 1 sonu itibarıyla bütün render command-buffer recording'i main
loop'ta seri yapılıyor. Modern engine'lerde main thread (game logic +
event pump) ve render thread (cmd recording + GPU submit) ayrı koşar
— main loop GPU drain'i beklemez, render thread N-1'inci frame'i
sunarken main thread N. frame'in command'larını oluşturur (pipeline'd
double buffering).

Bu ADR Phase 1 sınırlarını korurken, küçük bir render-thread iskeleti
ekleyip ileri sprint'lerin üzerine inşa edebileceği API'yi
tanımlar. Hot-path concurrency burada yapılmaz — yalnızca command-
buffer recording'in main thread'den boşaltılması.

## Karar

### (a) Architecture: producer (main) → SPSC queue → consumer (render)

* Main thread:
  - Pump events, run ECS scheduler, animate scene, decide what to draw
  - Build a `FrameWork` struct: pointer to MaterialInstance + draw call
    arguments + push-constant bytes
  - Push `FrameWork` onto a single-producer-single-consumer queue
* Render thread:
  - Pop next FrameWork
  - Call renderer.begin_frame() → record commands → end_frame()
  - vkQueueSubmit → vkQueuePresentKHR
  - Notify main thread that the slot is free for the next frame

### (b) Synchronization

* SPSC bounded queue with `frames_in_flight` slots (matches Renderer's
  swapchain ring). Implementation: ring buffer + two atomic indices
  (consumer head, producer tail) — wait-free in steady state.
* Main thread blocks (condition_variable) when the queue is full;
  render thread blocks when empty. Both are *one-frame* events, not
  hot paths.
* Shutdown: main thread sets a `running = false` atomic, joins
  render thread. Render thread observes the flag at each pop and
  exits cleanly.

### (c) MVP scope (Phase 1.5)

v1 implementation:
* `cd::render::FrameRecorder` — header-only RAII wrapper around the
  SPSC queue + std::thread.
* User passes a `record_callback(FrameContext&)` lambda; the worker
  invokes it inside its own thread, then submits.
* Renderer + Vulkan resources are touched ONLY by the render thread.
  Main thread sends data via the queue (e.g., per-frame world matrices)
  and never directly touches Vk* handles.

v2 deferred:
* Per-frame command-buffer recording PARALLELISM (multiple worker
  threads recording subsets of the scene). Needs Vulkan secondary
  command buffers + careful sync.
* GPU timestamps + per-stage CPU/GPU graphs.

### (d) Not in v1

* True async compute queue (separate VkQueue) — Phase 2.
* GPU work stealing (multi-pass framegraph dispatcher) — Phase 3.
* RHI handle thread-affinity guards — implicit for now; v2 needs
  diagnostic asserts.

## Reddedilen alternatifler

* **Lockless triple-buffered command-buffer pool:** Cleaner steady-
  state but adds 3× VkCommandPool overhead and a much trickier
  fence-recycle path. SPSC bounded queue handles the same throughput
  with one VkCommandPool per swapchain image (Renderer already does this).
* **Push every per-frame command as a struct to render thread, render
  thread builds the actual VkCommandBuffer:** Cleaner separation but
  forces every draw API to be data-only. Current samples wire ICommandBuffer
  inline — a record_callback model is the minimum-change path that lets
  existing samples opt into threading later.
* **Coroutines (C++20 co_await):** Elegant for asynchronous frames but
  the engine has no `cd::scheduler` runtime yet, and pulling one in for
  just the render thread is overkill.

## Sonuçlar

* `cd::render::FrameRecorder` adds ~150 LOC + 3 unit tests; samples
  remain single-threaded by default.
* Tester sample `hello_render_thread` (Phase 1.5): spins
  `cd::ecs::World` + animation on main thread while the render thread
  renders the cube. Demonstrates the producer → consumer boundary.
* When v2 lands (parallel recording), the API stays the same — workers
  invoke the same record_callback under the hood.

## Açık sorular

* Should `cd::render::Renderer` itself thread-affine its handle access
  (assert on wrong thread)? Probably yes, gated by `CD_RHI_DEBUG_THREAD_CHECK`.
* OS thread priority: main thread NORMAL, render thread ABOVE_NORMAL?
  Console platforms often need explicit priority for sub-millisecond
  vsync alignment.
