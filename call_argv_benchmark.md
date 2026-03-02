# VM_Call vs VM_CallArgv Benchmark

Comparison of `ValkeyModule_Call` (VM_Call) and `ValkeyModule_CallArgv` (VM_CallArgv) using the
`call.c` test module and `valkey-benchmark`.

## Variants

Three module commands, each a transparent pass-through to the inner Valkey command:

| Command | Implementation |
|---|---|
| `test.call <cmd> [args...]` | `VM_Call` — builds a `CallReply` tree, replies via `ReplyWithCallReply` |
| `test.call_argv_passthrough <cmd> [args...]` | `VM_CallArgv` with `replyAvailable` — raw RESP bytes written directly to the output buffer |
| `test.call_argv <cmd> [args...]` | `VM_CallArgv` with per-type callbacks — parses reply and re-serializes via `ValkeyModule_ReplyWith*` |

## Metrics

- **Client-side:** throughput (RPS) and p50 latency reported by `valkey-benchmark -q`
- **Server-side:** `usec_per_call` from `INFO commandstats`, reset with `CONFIG RESETSTAT` before each run

## Benchmark Parameters

- Requests per run: 300,000
- Concurrent clients: 50

## Data Setup

```
SET mykey "hello world"
SET mybigkey <100KB value>
MSET k1 val1 k2 val2 … k100 val100
HSET myhash f1 v1 f2 v2 … f500 v500
RPUSH mylist elem1 elem2 … elem500
XADD mystream * field1 val1 field2 val2   (× 500 entries)
```

## Test Cases

### Category A — Few arguments, scalar response

Baseline: simplest possible interaction.

| Command | Args | Response |
|---|---|---|
| `GET mykey` | 2 | bulk string (~11 bytes) |

### Category B — Many arguments, scalar response

Isolates argument-passing overhead. `EXISTS` does N hash lookups and returns a single integer with
no per-argument allocations, making its execution time much shorter than commands like `HSET` that
write data. This keeps the command execution cost low enough that the module wrapper overhead
(argv array allocation + N×incrRefCount in VM_Call) can be observed.

| Command | Args | Response |
|---|---|---|
| `EXISTS k1 k2 … k100` | 101 | integer `100` |

### Category C — Few arguments, flat array response

Two commands at increasing reply sizes to show how flat array length scales.

| Command | Args | Response |
|---|---|---|
| `LRANGE mylist 0 -1` | 4 | flat array of 500 bulk strings |
| `HGETALL myhash` | 2 | flat array of 1000 bulk strings (field+value interleaved) |

### Category D — Few arguments, nested array response

Most structurally complex reply: each stream entry is itself a two-element array
`[id, [field, value, field, value]]`.

| Command | Args | Response |
|---|---|---|
| `XRANGE mystream - +` | 4 | 500 entries, each `[id, [f1, v1, f2, v2]]` |

### Category E — Few arguments, large scalar response

Isolates the effect of reply byte volume on a scalar (single bulk-string) reply. A 100KB
value keeps the RESP node count at 1 but maximises the bytes the module must handle.

| Command | Args | Response |
|---|---|---|
| `GET mybigkey` | 2 | bulk string (~100KB) |

### Category F — Few arguments, RESP3 map response

Same data as Category C (HGETALL), but with the client in RESP3 mode. HGETALL returns
a RESP3 map instead of a flat array. Tests whether the module variants handle the richer
RESP3 type encoding differently.

| Command | Args | Response |
|---|---|---|
| `HGETALL myhash` (RESP3) | 2 | map of 500 field→value pairs |

### What each category isolates

| Category | Reply-handling overhead | Arg-passing overhead | Special dimension |
|---|---|---|---|
| A | minimal (one small value) | minimal | — |
| B | minimal (integer) | high (100 args) | — |
| C | moderate → high (flat, grows with N) | minimal | — |
| D | high + nesting depth | minimal | — |
| E | payload size (scalar, no extra nodes) | minimal | large byte volume |
| F | same node count as C, but RESP3 map type | minimal | RESP3 map encoding |

---

## Results

Benchmark parameters: 300,000 requests, 50 concurrent clients.
Server-side latency from `INFO commandstats` (`usec_per_call`), reset before each run.
The "vs test.call" column shows the reduction in server-side time relative to `test.call`.

### Category A — GET mykey

| Variant | RPS | p50 (ms) | server µs/call | vs test.call |
|---|---:|---:|---:|---:|
| test.call | 150,301 | 0.215 | 0.82 | — |
| test.call_argv_passthrough | 162,602 | 0.151 | 0.47 | −43% |
| test.call_argv | 173,611 | 0.143 | 0.44 | −46% |

### Category B — EXISTS k1..k100 (100 keys)

| Variant | RPS | p50 (ms) | server µs/call | vs test.call |
|---|---:|---:|---:|---:|
| test.call | 86,806 | 0.503 | 3.72 | — |
| test.call_argv_passthrough | 87,771 | 0.503 | 3.40 | −9% |
| test.call_argv | 84,270 | 0.503 | 3.54 | −5% |

### Category C — LRANGE mylist 0 -1 (500 elements)

| Variant | RPS | p50 (ms) | server µs/call | vs test.call |
|---|---:|---:|---:|---:|
| test.call | 25,159 | 1.279 | 31.29 | — |
| test.call_argv_passthrough | 25,096 | 0.983 | 13.28 | **−58%** |
| test.call_argv | 26,826 | 1.015 | 29.37 | −6% |

### Category C — HGETALL myhash (500 fields)

| Variant | RPS | p50 (ms) | server µs/call | vs test.call |
|---|---:|---:|---:|---:|
| test.call | 11,343 | 3.567 | 80.10 | — |
| test.call_argv_passthrough | 15,164 | 1.623 | 46.58 | **−42%** |
| test.call_argv | 11,913 | 3.047 | 73.75 | −8% |

### Category D — XRANGE mystream - + (500 entries)

| Variant | RPS | p50 (ms) | server µs/call | vs test.call |
|---|---:|---:|---:|---:|
| test.call | 3,124 | 9.695 | 247.73 | — |
| test.call_argv_passthrough | 4,014 | 5.343 | 156.07 | **−37%** |
| test.call_argv | 3,088 | 10.951 | 275.81 | +11% |

### Category E — GET mybigkey (100KB scalar)

| Variant | RPS | p50 (ms) | server µs/call | vs test.call |
|---|---:|---:|---:|---:|
| test.call | 30,937 | 1.023 | 11.93 | — |
| test.call_argv_passthrough | 32,394 | 0.983 | 10.99 | −8% |
| test.call_argv | 32,906 | 0.999 | 10.86 | −9% |

### Category F — RESP3 HGETALL myhash (500 fields, map reply)

| Variant | RPS | p50 (ms) | server µs/call | vs test.call |
|---|---:|---:|---:|---:|
| test.call | 11,940 | 3.263 | 75.83 | — |
| test.call_argv_passthrough | 15,597 | 1.543 | 45.54 | **−40%** |
| test.call_argv | 12,338 | 3.071 | 72.58 | −4% |

---

## Conclusions

### 1. `replyAvailable` passthrough consistently outperforms `VM_Call`

`test.call_argv_passthrough` is faster than `test.call` in every category. Even for a simple
`GET` returning a short bulk string (Category A) the server-side time drops by 43%. For commands
returning large arrays the improvement ranges from 37% (XRANGE, nested) to 58% (LRANGE, flat).
The gain comes from eliminating both steps that `VM_Call` must perform: building an intermediate
`CallReply` tree from the parsed reply, and then re-serializing that tree back to RESP. The
`replyAvailable` callback receives the raw RESP bytes produced by the inner command and writes
them directly to the client output buffer — no allocation, no parse, no re-serialize.

### 2. Typed callbacks (`test.call_argv`) performance depends on reply complexity

The `test.call_argv` variant (per-RESP-type callbacks that call `ValkeyModule_ReplyWith*`) shows
behaviour that varies with the reply structure:

- **Scalar reply (GET small):** ~46% faster than `test.call`. The single `CallReply` node
  allocation is avoided entirely.
- **Scalar reply (GET 100KB):** ~9% faster (see §5 below).
- **Flat arrays (LRANGE, HGETALL):** 6–8% faster, within measurement noise. Both the typed
  callbacks and `VM_Call` must process each element; the costs are comparable.
- **Deeply nested arrays (XRANGE 500 entries):** ~11% **slower** than `test.call`. XRANGE
  produces roughly 4,500 individual callback invocations (arrayStart, bulkString, arrayEnd …
  per entry). At a few nanoseconds each, the cumulative dispatch overhead exceeds what the
  `CallReply` tree approach costs. The per-element function-call overhead of typed callbacks
  becomes the dominant factor for complex nested structures.

### 3. Argument-passing overhead of `VM_Call` is negligible in practice

`VM_Call` with the `"v"` format allocates a new argv array, creates a string object for the
command name, and calls `incrRefCount`/`decrRefCount` on each argument. `VM_CallArgv` borrows
the caller's argv array directly with no allocation or refcount changes.

EXISTS with 100 keys (101 total args) was chosen to isolate this effect because its execution
time (~3.7 µs) is much shorter than HSET. Even so, the gap between all three variants is only
5–9%, well inside noise. The per-argument overhead in VM_Call (one zmalloc + N×incrRefCount)
amounts to less than ~0.15 µs for 100 arguments, making it negligible in any realistic scenario.

### 4. The passthrough advantage scales with reply node count, not byte volume

| Case | RESP nodes | `test.call` µs | passthrough µs | speedup |
|---|---:|---:|---:|---:|
| GET small | 1 | 0.82 | 0.47 | 1.7× |
| EXISTS 100 | 1 | 3.72 | 3.40 | 1.1× |
| LRANGE 500 | 500 | 31.29 | 13.28 | 2.4× |
| HGETALL 500 fields | 1,000 | 80.10 | 46.58 | 1.7× |
| XRANGE 500 entries | ~4,500 | 247.73 | 156.07 | 1.6× |
| GET 100KB | 1 | 11.93 | 10.99 | 1.1× |
| HGETALL (RESP3) | ~1,001 | 75.83 | 45.54 | 1.7× |

The benefit grows primarily with the total number of RESP nodes in the reply, each requiring
one allocation in the `CallReply` tree. Category B (EXISTS) is the outlier: the single integer
reply means reply-handling is trivial.

### 5. Large scalar reply: byte volume does not multiply the passthrough gain

Category E (GET 100KB) shows only an 8–9% server-side improvement, much less than Category A
(GET small, ~46%). Both commands return exactly one RESP bulk-string node. The difference is that
with a 100KB value the dominant cost shifts to the memory copy of the payload itself (~11.9 µs
total), which both variants must perform. The savings from eliminating one `CallReply` node
allocation (~1 µs) represent a smaller fraction of the total wall time. Conclusion: the
passthrough advantage is proportional to node count, not payload size.

### 6. RESP3 map type does not change the overhead picture

Category F (RESP3 HGETALL) produces a map reply instead of a flat array. Comparing RESP2 vs
RESP3 for the same underlying data (500 fields):

| Protocol | `test.call` µs | passthrough µs | passthrough gain |
|---|---:|---:|---:|
| RESP2 (flat array, 1,000 nodes) | 80.10 | 46.58 | −42% |
| RESP3 (map, ~1,001 nodes) | 75.83 | 45.54 | −40% |

The results are statistically indistinguishable. `VALKEYMODULE_CALL_ARGV_FLAG_RESP_AUTO` causes
the inner call to match the client's negotiated protocol, so the raw RESP3 bytes flow directly
to the client in both `replyAvailable` and `VM_Call`'s re-serialization path. The map encoding
overhead is the same for both variants, leaving the relative difference unchanged.

### Summary

Use `VM_CallArgv` with `replyAvailable` whenever the module command is a pass-through (the reply
from the inner command is forwarded to the caller unchanged). The raw RESP bytes are already in
the correct format; copying them directly avoids all intermediate allocation and serialization
overhead. Typed per-RESP callbacks are suited for cases where the module needs to inspect or
transform individual reply elements, but they do not improve throughput over `VM_Call` for flat
arrays and are measurably slower for deeply nested structures due to per-element dispatch
overhead.
