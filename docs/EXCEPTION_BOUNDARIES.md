# Exception boundaries

`ARCHITECTURE.md` §3.1 says **empty `catch(...) {}` is forbidden**.
A `catch(...)` block is only permitted if it *observably* records or
routes the failure (counter increment, log line, stat bump, error
flag) AND the file is enumerated below with a justification.

This doc is the registry. Any new `catch(...)` site MUST be added
here in the same commit that introduces it; CI runs a grep audit
that fails if a `catch (...)` exists in `engine/` that this doc
does not name.

---

## Registered boundaries (6)

### 1. `cd::concurrency::JobGraph` — detached graph nodes

- **File:** `engine/foundation/concurrency/include/cd/concurrency/JobGraph.hpp`
- **Observable action:** `failed_nodes_.fetch_add(1, memory_order_relaxed)`
- **Why a boundary is justified:** Each graph node runs on a
  detached pool task with no caller to receive an exception
  (return value is `void`; no future). Letting the throw escape
  the lambda invokes `std::terminate()` because the worker loop is
  `noexcept`. Nodes that need to surface errors must capture them
  inside the body via `std::promise` or `cd::core::Result<T>`.
- **How a downstream checks it:** read
  `JobGraph::failed_node_count()` after `wait()` returns.

### 2. `cd::concurrency::ThreadPool` — generic worker loop

- **File:** `engine/foundation/concurrency/include/cd/concurrency/ThreadPool.hpp`
- **Observable action:** `stats_.failures.fetch_add(1, memory_order_relaxed)`
- **Why a boundary is justified:** Same reason as JobGraph — the
  worker thread is `noexcept`, and the caller path
  (`submit_detached(...)`) has no return channel. The pool's
  `stats().failures` is the documented surface for caller-side
  reaction (a CI assert, a soak-test gate, a health-probe
  bumper).

### 3. `cd::concurrency::WorkStealingThreadPool` — detached jobs

- **File:** `engine/foundation/concurrency/include/cd/concurrency/WorkStealingThreadPool.hpp`
- **Observable action:** `stats_.detached_exceptions.fetch_add(1, memory_order_relaxed)`
- **Why a boundary is justified:** `packaged_task`-wrapped jobs
  carry their own `exception_ptr` and surface it through
  `std::future::get()`. Detached jobs have no future; the
  exception cannot propagate without crashing the worker. The
  counter is the documented diagnostic.

### 4. `cd::log::ILogger::log<Args>` — format-time failure

- **File:** `engine/foundation/log/include/cd/log/ILogger.hpp`
- **Observable action:**
  `log_impl(LogLevel::Error, &loc, "Log format error")`
- **Why a boundary is justified:** The format helper
  (`detail::format_braces`) is allowed to throw on a bad type
  match. A logger that itself throws is unusable from a
  `catch` handler or an exception-safety boundary, so the
  contract is "format errors become an `Error`-level log line at
  the same source location". The catch-all observable action is
  *the recovery log call itself* — there's no separate counter
  because the log is the observability.
- **Known gap (documented):** if `log_impl` itself throws (it is
  virtual, not declared `noexcept`), the recovery `log_impl`
  call can throw again and abort the program. A future change
  may add a noexcept escape hatch (per reviewer suggestion in
  REVIEW-independent-auditor-20260523.md).

### 5. `cd::profile::CsvSink::submit` — disk-write failure

- **File:** `engine/foundation/profile/include/cd/profile/CsvSink.hpp`
- **Observable action:**
  `failed_writes_.fetch_add(1, memory_order_relaxed)`,
  visible via `failed_writes()`.
- **Why a boundary is justified:** `ISink::submit` is contractually
  `noexcept` (the profiler must never crash the app). `ofstream`
  IO can still raise via `badbit` + `exceptions(failbit)` or
  `std::bad_alloc`. Counting-and-swallowing keeps the contract;
  the counter lets a consumer detect a dropped sample stream
  before claiming the CSV is complete.

### 6. `cd::profile::ChromeTraceSink::submit` — disk-write failure

- **File:** `engine/foundation/profile/include/cd/profile/ChromeTraceSink.hpp`
- **Observable action:**
  `failed_writes_.fetch_add(1, memory_order_relaxed)`,
  visible via `failed_writes()`.
- **Why a boundary is justified:** Same as CsvSink. The Chrome
  Trace JSON is consumed by `chrome://tracing` / `speedscope` /
  `perfetto.dev`, none of which tolerate a half-written array
  silently — non-zero `failed_writes()` tells the consumer to
  re-record before handing the .json to a viewer.

---

## CI audit (planned, Phase 13.B)

The audit script is one-liner:

```bash
grep -RIn "catch (\.\.\.)" engine/ \
  | grep -v "engine/foundation/core/include/cd/core/Definitions.hpp" \
  | awk -F: '{print $1}' \
  | sort -u
```

Every file in that list must appear in the table above. CI fails
otherwise. The `CD_CATCH_ALL` macro definition in
`Definitions.hpp` is exempt (it is the macro, not a use site).

Adding a new `catch(...)` requires:
1. Choose an observable action (counter / log / flag).
2. Append a new section to this doc with the file path,
   action, and justification.
3. Both changes go in the **same commit**.

---

## Why the rule reads this way

Original rule (pre-B5): *`catch(...) {}` is forbidden.* The
verbatim text described the *empty-body* construct; the intent
("never silently swallow an exception") got encoded too tightly.
The engine in practice has six sites where catch-ellipsis with an
*observable* payload is correct (detached worker loops, noexcept
sink boundaries) — they honor the intent but not the verbatim
rule. Rewriting the rule plus enumerating the sites gives the
codebase a single discoverable inventory of where exceptions
become counters, instead of trusting six different files to
explain themselves separately. Closes reviewer blocker B5 from
`docs/REVIEW-independent-auditor-20260523.md`.
