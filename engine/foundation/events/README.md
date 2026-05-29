# cd::events

**Purpose**: type-safe event dispatcher. Subscribe / publish over std::type_index keys with bounded lifetime (Subscription RAII). Used by editor + UI + scripting layers for change notification without coupling caller to listener.

**Namespace**: `cd::events`.

**Headers**: `cd/events/{Dispatcher,Subscription}.hpp`.

**Primary types**:
- `cd::events::Dispatcher` -- registry + dispatcher. `subscribe<EventT>(fn) -> Subscription`, `publish(event)`.
- `cd::events::Subscription` -- RAII handle that unsubscribes on destruction.

**Test command**: `ctest --preset ninja-debug -R cd_test_events --output-on-failure`.

**Notes**:
- Single-threaded by design; cross-thread dispatch goes through cd::concurrency::Mpsc instead.
- Used by cd::editor for transform-change broadcasts (drag-end triggers history record).
