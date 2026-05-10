# Brief: PL/SW dynamic memory architecture — chunk allocation + streaming

> ## ⚠️ SUPERSEDED — see authoritative document in the repo
>
> The canonical, in-flight version of this plan now lives at
> **[`sw-cor24-plsw/docs/shrink-lgo-size.md`](https://github.com/sw-embed/sw-cor24-plsw/blob/main/docs/shrink-lgo-size.md)**
> (relayed to main 2026-05-10, commit `10cd00f`). dcpls owns and updates that
> document as phases land. It carries the corrected sizing arithmetic
> (the char ABI is already 8-bit packed 3/word — no tc24r ABI change needed)
> and the measured Phase 0 baseline numbers.
>
> **Read that doc, not this one, for current planning.**
>
> This brief is preserved below for historical context only — it captures
> the original architecture discussion (2026-05-09) that prompted dcpls to
> draft the in-repo plan, including the early framing of the chunk
> allocator + streaming + 4 KB ceiling rule. Specific numbers and
> constraint #1 (char encoding) in the body below are partially incorrect
> per dcpls's correction; the high-level phase structure (0–6) was kept by
> dcpls in the authoritative doc.

---

**Owner:** dcpls
**Branch:** `pr/dynamic-memory-architecture` (rolling top-level; per-phase PRs may take their own slugs)
**Repo:** `sw-cor24-plsw`
**Drafted by:** mike (2026-05-09, after dcpls's static-buffer audit + size discussion).

This is a **multi-phase architecture brief**, not a single saga. It exists so the per-phase work is sequenced against a coherent target instead of being bolted on ad hoc. Each phase below ships as its own PR.

## Target budget

For the production `plsw.lgo` running on COR24 (1 MB SRAM total):

| Category | Target | Today (~) | Comment |
|---|---|---|---|
| Code + static data | **≤ 64 KB** | ~250–400 KB code (incl. tests) + ~513 KB statics per dcpls's audit | The "say, 64 KB" mark from the discussion — minicomputer-era footprint. Realistic once tests are gone (Phase 1) and pre-allocated buffers move to chunks (Phases 3–4). |
| Dynamic chunks | **≤ 12 × 64 KB = 768 KB peak** | n/a (no chunk allocator today) | Sized to fit a SNOBOL4-scale source compile with margin. |
| Total runtime SRAM (code + statics + chunks) | **≤ 832 KB** | exceeds 1 MB on large compiles | Leaves ~190 KB of SRAM for stack, I/O state, future loadable extensions. |

If a phase's measured cost makes one of these targets infeasible, that's a design signal — revisit the phase, don't just bump the target.

## Strategy in one paragraph

Replace every static buffer larger than ~1 KB with either (a) a streaming pipeline that doesn't buffer the data at all, or (b) a chunk-allocated pool that requests 64 KB chunks from a single low-level chunk allocator and returns them when the pool empties. The chunk allocator is the only static memory manager; everything above it (AST node pool, arena, symbol table backing, macro expansion buffer) becomes a chunk consumer. Static memory shrinks to: (i) the fixed-size small tables that genuinely never grow (DEF_MAX = 512 entries, SYM_SCOPE_MAX = 256, MACRO_MAX = 8, etc.), (ii) the chunk-allocator's own metadata (free-list of ~16 chunk descriptors), and (iii) the compiled code itself.

## Hard rule: 4 KB static-block ceiling

**No single static array, buffer, or zero-fill block may exceed 4096 bytes.** That includes `.byte`/`.word`/`.zero` declarations in any compiled output, and any C-source `int X[N]` / `char X[N]` declaration where the resulting allocation exceeds 4 KB.

Why 4 KB:
- Forces every "big block" to use the chunk allocator, by construction. There's no escape hatch where someone re-introduces a 256 KB buffer because the deadline is tight.
- Small enough that even worst-case static inflation can't make the binary unusably large (a 4 KB array is 4 KB, full stop — it can't accidentally grow with a `_MAX` bump).
- Big enough that legitimately small fixed tables (DEF_MAX × small-struct, symbol scope stack, etc.) still fit comfortably as static.
- Round number. Easy to remember, easy to grep for.

How it's enforced (two layers, both shipped as part of Phase 5):

1. **Source-level lint (cheap, near-term):** a check in `scripts/lint-static-blocks.sh` that fails CI if any `int|char X[N]` declaration in `src/*.h` or `src/*.c` resolves to N × wordsize > 4096 bytes. Runs as part of `just lint` / `just test`. Catches PL/SW source violations.

2. **Compiled-output lint (catches generated code):** a check that fails if `build/plsw.s` or `build/snobol4.s` (or any `.s` produced by tc24r or pl-sw) contains a `.zero N` directive with N > 4096, OR a contiguous run of `.byte`/`.word` directives summing to > 4096. Catches violations introduced by codegen (e.g. someone bumps `NODE_POOL_MAX` past the per-chunk limit and tc24r emits a giant array). Runs as part of the install verification step before mike copies any artifact to `work/lib/cor24/`.

**This rule is the load-bearing constraint of the whole plan.** Without it, the architecture is aspirational; with it, every future commit either passes the rule or has to confront why it's adding a giant static block. The 4 KB number is the design's answer to "what's the largest piece of state we'll ever name statically" — by fiat, not 4 KB + ε creeping forever upward.

Exemptions: the chunk allocator's own `chunk_storage` declaration is the *only* exception. It's the reservation pool from which everything else draws; it must be larger than 4 KB by design. It gets a `/* lint-exempt: chunk-pool */` comment marker that the lint script recognizes.

## Constraints we have to design around

1. **`char` is 8-bit, packed 3 per word on COR24.** Empirically verified: emit_buf[262144] occupies 87,382 words in plsw.s — exactly 3 chars per word. char buffers therefore cost ~1 byte per char (modulo at most 2 bytes tail-padding to a word boundary). No char-encoding waste. A 64 KB chunk holds ~65,535 chars, so any reasonable char buffer fits in a single chunk.

   This means streaming character pipelines (Phase 4) are chosen for *flexibility against unbounded inputs*, not to dodge a per-char SRAM penalty. Static char buffers are honestly priced; the problem is only that they're sized for worst-case input.

2. **tc24r is the long-tail code-size factor.** plsw.lgo is built by tc24r (TinyC→COR24). tc24r doesn't deduplicate, has limited inlining decisions, and its DCE (just shipped via `dcxtc-codegen-dce`) is function-level only. After Phase 1 (test split) the production code is plausibly small enough on its own — most of today's ~250–400 KB code is the 6800 lines of test functions. If Phase 1 + Phases 3–4 land and code is still well over 64 KB, the next step is **parallel work in `sw-cor24-x-tinyc`** (better register allocation, peephole, real DCE across compilation units). Treated as Phase 6's pivot point — only invoked if measurements demand it.

3. **Self-hosting bootstrap.** When rebuilding plsw.lgo against a chunk-using PL/SW source, tc24r has to be able to compile the chunk-based source. The first PR that actually depends on the chunk allocator must include a tested tc24r build path, or the bootstrap breaks.

4. **No MMU, no paging.** Chunks are real SRAM addresses. A "freed" chunk is reusable but never goes anywhere. Fragmentation is a real risk if chunks aren't returned cleanly at end-of-compilation.

5. **Test code is ~6800 / 7048 lines of `src/main.c`.** It dominates the code budget. Phase 1 splits this out. Without it the rest of the plan is moot.

## Phase 0 — measure peak usage

**Don't design against the static caps; design against measured peaks.** Today every `_MAX` constant is a guess. Before any architectural work, instrument every pool to record its high-water mark across a representative compile suite (lexer test, macro test, PL/SW self-compile, SNOBOL4 module compile). Output one report per pool.

Specific instrumentation in `src/main.c` (#ifdef MEASURE):
- `nd_alloc()`: track `peak_nd`, dump at end-of-compilation.
- `arena_alloc()`: track `peak_arena`.
- `emit_*` (post-streaming): track total bytes emitted.
- Macro tables: peak `macro_count`, peak `macro_gen_pos`, peak `mac_arg_*`.
- Lexer: peak `src_pos` / `inc_depth`.
- Symbol table: peak `sym_count` per scope, max scope depth.

**Deliverable:** a numbers table, one row per pool, three columns:
| Pool | Static cap (today) | Peak observed | Ratio |
|---|---|---|---|

across at least these inputs:
- `tests/inputs/hello.plsw`
- `examples/hello_macro.plsw` (macro-heavy)
- `src/main.c` self-compile (largest realistic)
- `sno_exec.plsw` (largest external consumer)

The ratio column tells us where the pre-allocation waste actually lives. If `peak_nd / NODE_POOL_MAX < 0.1`, that pool is 10× oversized. If `peak_nd > NODE_POOL_MAX`, we already overflow on real inputs (don't believe this is impossible — check first).

**Phase 0 ships a measurement PR. No production behavior change.** Pure instrumentation behind `#ifdef MEASURE`. Run it; capture numbers; commit them to a markdown report under `docs/memory-audit-2026-05-09.md` for future reference.

**Without Phase 0 numbers, every later phase is guessing.** No skipping.

## Phase 1 — split tests out of production

Test functions consume an estimated ~600 KB of the current ~1.2 MB code budget. Get them out before any other phase, because they distort every measurement.

Two steps, ship together:

**1a. Tag every `test_*` function with `#ifdef BUILD_TESTS`.** Default build excludes them. The interactive 0-34 prompt and `run_suite()` also wrap in the same gate. After this PR, default `just build-lgo` produces a `plsw.lgo` whose only entry point is `compile_program()`.

**1b. Add `just build-tests-lgo` recipe.** Produces `build/plsw-tests.lgo` for development/CI. The test build is what `just test` uses; it's not what mike installs to `work/lib/cor24/`.

**Deliverable:** two lgos out of the same source tree. plsw.lgo (production) shrinks ~600 KB; plsw-tests.lgo stays as today.

**Risks:** test code may share helpers with production code. Audit `grep '#ifdef BUILD_TESTS'` boundary thoroughly to avoid orphaning a helper.

**Phase 1 alone gets the production .lgo from ~1.7 MB → ~1.1 MB.** Doesn't hit any of the targets, but unblocks honest measurement of the rest.

## Phase 2 — chunk allocator

Single low-level module. Static metadata only; no pool semantics yet — those come in Phase 3+.

Design:

```c
/* chunk.h */
#define CHUNK_SIZE 65536           /* 64 KB per chunk, in CHARS (= 64KB SRAM) */
#define CHUNK_MAX 16               /* cap: 16 × 64 KB = 1 MB ceiling */

struct chunk_desc {
    int in_use;        /* 0 = free, 1 = allocated */
    char *base;        /* start address of this chunk's 64 KB region */
};

extern struct chunk_desc chunk_table[CHUNK_MAX];
extern char chunk_storage[CHUNK_MAX * CHUNK_SIZE];   /* the actual SRAM */

void chunk_init(void);              /* mark all free */
char *chunk_alloc(void);            /* return base of a free 64 KB chunk, or NULL */
void chunk_free(char *base);        /* return chunk to free pool */
int chunk_used(void);               /* count of in_use chunks (for budget reporting) */
```

Static cost: `chunk_table` (16 entries × 2 ints × 3 bytes = ~100 bytes) + the `chunk_storage` declaration. **Note `chunk_storage` is not "free SRAM" — it's the entire dynamic memory budget pre-reserved.** Total static cost = whatever `CHUNK_MAX * CHUNK_SIZE` works out to in COR24 words. We do this so allocation is trivial (no system call equivalent on COR24, no malloc); it's a reservation, not a heap.

**This is the architectural shift:** before Phase 2, we have ~513 KB of *named* static buffers. After Phase 2, we have a single 1 MB-or-fewer reservation that pools draw from. The total bytes don't shrink yet — they shift.

**Deliverable:** chunk allocator with unit tests. No pool migration in this phase.

## Phase 3 — migrate the AST pool to chunks

The AST node pool is **332 KB at NODE_POOL_MAX = 12288** with 9 parallel int arrays. Largest single waste once `EMIT_BUF_SIZE` drops out via streaming-emit.

Migration:

1. **Replace 9 parallel `int nd_*[NODE_POOL_MAX]` arrays with chunk-backed equivalents.** Each chunk holds `CHUNK_SIZE / 9` ≈ 7281 nodes (since 9 fields × 3 bytes = 27 bytes per node, and CHUNK_SIZE = 65536 bytes, that's actually 65536/27 ≈ 2427 nodes per chunk). First-cut implementation: one chunk = one array of `node_block { int kind[N]; int type[N]; ... }` where N = (CHUNK_SIZE / (9*3)) ≈ 2427.

2. **Indexing:** node id is now `chunk_idx * N + slot`. `nd_kind(id)` becomes `chunk_array[id/N]->kind[id % N]`. Add inline accessor macros so callsite count doesn't explode. Phase 3's PR is mechanical: replace every `nd_kind[i]` with `nd_kind(i)` etc.

3. **Allocation:** `nd_alloc()` bumps a slot counter in the current chunk; when it fills, request a new chunk from `chunk_alloc()`. Track current chunk index globally.

4. **Resetting between compiles:** `chunk_free()` every AST chunk at end-of-compile. Chunks return to free pool.

**Static cost after Phase 3:** AST static drops from 332 KB to **~50 bytes** (the chunk-list head + slot counter). Dynamic cost: 1-N chunks depending on input size, where N is determined by the Phase 0 peak measurement.

**For Phase 0's measured peak (assumed e.g. 4000 nodes for sno_exec):** 4000 / 2427 ≈ 2 chunks = 128 KB dynamic. **vs the current 332 KB static cost.** Net SRAM saving = 332 KB - 128 KB - 50 bytes ≈ 204 KB.

**Risks:**
- Indexing performance: every node access becomes `array[id/N][id%N]`. tc24r doesn't strength-reduce divisions; if N isn't a power of two this is expensive. **Pick N as a power of two** (e.g. 2048) even if it underutilizes the chunk slightly — the perf cost of non-power-of-two indexing is far worse.
- Chunk lookup is array indexing, which is fast. The current monolithic array index is just one indirection; chunk-array is two. Acceptable.

## Phase 4 — migrate buffer pools (streaming where possible)

`emit_buf` is already streamed (Phase B of `dcpls-streaming-emit`). Apply the same treatment to:

**4a. `src_buf` (currently 64 KB).** The lexer reads source via `src_buf[src_pos++]`. Convert lexer to pull-based: `lex_next_char()` calls `uart_getchar()` directly, with a tiny look-ahead buffer (one or two characters) for `peek()`. `%INCLUDE` becomes a stack of input streams, not a buffer concat.

**4b. `inc_buf` (currently 32 KB).** Same treatment. After 4a, this evaporates entirely — there's no buffer to stage into.

**4c. `mac_gen_buf` (currently 48 KB).** Macros generate text that ultimately emit to UART. Convert to streaming: macro expansion writes through to the same emit pipeline as the codegen. Mid-expansion state (the partial expansion of the current macro invocation) is small (a few hundred bytes per invocation, recursion-depth bounded by `MAC_EXPAND_MAX = 512`).

**4d. `arena_buf` (currently 24 KB).** This one is *not* easily streamable — it backs miscellaneous compile-time tables. Migrate to chunks: arena requests one chunk, bumps within it, requests another when full. Phase 0 measurement should tell us if peak arena usage is < 64 KB (one chunk) or much more.

**Static cost after Phase 4:** all four buffer caps drop to either zero (streaming) or ~50 bytes of chunk-list metadata. Net saving from Phase 4: ~168 KB static → near-zero static + maybe 1-2 chunks dynamic for arena.

**Risks:**
- `src_buf` removal touches every lexer callsite. Audit `grep src_buf` thoroughly. The PR for 4a will be the largest in line count of any phase here.
- `mac_gen_buf` streaming changes when macro expansion errors are detected (currently you can roll back the buffer pos; with streaming, you've already emitted). Need explicit error-detection-before-emit pass for macros, or accept that errored macros leave partial output and the test runner detects this via stderr.

## Phase 5 — small-pool audit + 4 KB ceiling lint

Two parts:

**5a. Audit each remaining `_MAX` constant against Phase 0 peak.** Per `src/*.h`:

| Constant | Value today | Static cost | Action |
|---|---|---|---|
| `DEF_MAX` | 512 | (entry-size-dependent) | Keep static if Phase 0 peak < 512 AND total size < 4 KB; else convert. |
| `SYM_SCOPE_MAX` | 256 | (small-struct × 256) | Keep static; almost certainly under 4 KB. |
| `SYM_DEPTH_MAX` | 8 | trivial | Keep static. |
| `MACRO_MAX` | 8 | (per-macro-struct × 8) | Verify total < 4 KB. |
| `MACRO_CLAUSE_MAX` | 8 | trivial | Keep static. |
| `INC_MAX_DEPTH` | 4 | trivial | Keep static. |
| `INC_MAX_FILES` | 16 | trivial | Keep static. |
| `MAC_ARG_MAX` | 8 | trivial | Keep static. |
| `TDESC_MAX` | 64 | (struct × 64) | Verify < 4 KB. |
| `TDESC_FIELD_MAX` | 16 | trivial | Keep static. |
| `CG_STR_MAX` | 128 | (entry × 128) | Verify < 4 KB. |
| `CG_MAX_ARGS` | 8 | trivial | Keep static. |

Any pool whose total static cost exceeds 4 KB **must** convert to chunk-backed regardless of peak usage — that's the rule from above, not a judgment call. Pools whose Phase 0 peak exceeds 50% of cap and total cost < 4 KB stay static (right-sized); pools whose peak is < 10% get shrunk to `peak × 2`.

**Total of small-pool static after audit:** by construction, under 4 KB × (count of remaining static pools) ≤ 50 KB on a generous estimate. In practice probably under 10 KB.

**5b. Implement the lint scripts** described in the "Hard rule" section above:

- `scripts/lint-static-blocks.sh` — greps `src/*.h` and `src/*.c` for `(int|char)\s+\w+\s*\[\s*\w+\s*\]\s*;` declarations and resolves the constant to a byte size. Fails if size > 4096 and the declaration lacks the `/* lint-exempt: chunk-pool */` marker.
- `scripts/lint-emitted-blocks.sh` — scans a `.s` file for `\.zero N` with N > 4096 and contiguous `\.word`/`\.byte` runs that sum to > 4096. Fails on violation.

Both wired into `just lint` / `just test`. Both run as part of the install pipeline so a violation blocks promotion to `work/lib/cor24/`.

## Phase 6 — verify final budget; pivot to tc24r if needed

After Phases 1-5 ship and install:

1. **Measure code size.** `wc -c build/plsw.lgo` (encoded) and the lgo's `.text` section size (decoded). If decoded code ≤ 64 KB, target met. Likely it isn't.
2. **If code > 64 KB**, the bottleneck is tc24r code generation, not PL/SW source. Options:
   - Accept a higher code budget (e.g. 256 KB code + 8 KB statics + 12 chunks = ~840 KB total) and document the deviation.
   - Spawn a parallel saga in `sw-cor24-x-tinyc` to improve tc24r: better register allocator, peephole pass, cross-function DCE, common-subexpression elimination, dead-store elimination, function inlining for hot small functions. This is a 6-month-scale tc24r effort, not a single saga.
   - Hand-optimize the hot functions in plsw source (manual inlining, manual CSE).

The Phase 6 deliverable is **the decision**, with measured numbers backing it. Either we hit the target, or we re-budget with reasons.

## What "mike installs" means at each phase

After every phase's PR is relayed and released:

```bash
cd /disk1/github/softwarewrighter/devgroup/work/relay/sw-cor24-plsw
just clean && just build-lgo
install -m 0640 build/plsw.lgo /disk1/github/softwarewrighter/devgroup/work/lib/cor24/plsw.lgo
```

Then mike pings dcsno (and any other consumer with regenerable artifacts) to rebuild against the new pl-sw if any output-shape changes occurred. **Phases 0, 1, 2, 5 don't affect output shape.** Phases 3, 4 might, in subtle ways (e.g. node-id encoding leaks into error messages?). Verify.

## Tests

Each phase ships its own test plan. Universal:

1. **Existing reg-rs goldens stay green.** Anything that fails is a bug introduced by the phase, not an intentional change.
2. **`build/snobol4.bin` (rebuilt against new pl-sw) is byte-identical to today's** through Phases 1, 2, 5. **May differ in benign ways** through Phases 3, 4 — same `.bin` semantically but with different node-id-to-emit ordering or similar. Verify via cor24-emu round-trip, not byte equality.
3. **Phase 0 numbers carry forward.** After Phase 4, re-run the Phase 0 instrumentation; the peak chunks-allocated count should be ≤ 12 for the measured inputs. If it isn't, something regressed.

## Out of scope

- **Char ABI changes.** Already 3-chars-per-word on COR24; no rework needed.
- **tc24r optimization.** Phase 6 may discover this is needed; it's a separate repo's saga.
- **Self-hosting via a new bootstrap path.** PL/SW could one day rewrite its own compiler in PL/SW (replacing the C source), getting access to the .zero N codegen and any future PL/SW improvements. Tracked in archived `snobol4-runtime-split` discussions; not this plan.

## Sequencing summary

```
Phase 0 (measure)               ──→  produces docs/memory-audit-2026-05-09.md
   ↓
Phase 1 (test split)            ──→  -600 KB code, plsw-tests.lgo separate build
   ↓
Phase 2 (chunk allocator)       ──→  static infrastructure, no pool migration
   ↓
Phase 3 (AST → chunks)          ──→  -204 KB static, +N×64 KB dynamic (N from Phase 0)
   ↓
Phase 4 (buffers → stream/chunks) ──→  -168 KB static, evaporates into stream + arena chunks
   ↓
Phase 5 (small-pool audit + 4 KB ceiling lint)  ──→  trim or keep per Phase 0; lint enforces 4 KB rule going forward
   ↓
Phase 6 (verify)                ──→  measure final budget, decide on tc24r pivot
```

Each `↓` is a separate PR cycle: dcpls implements + tests, mike relays + releases + reinstalls, downstream consumers (dcsno) optionally rebuild.

## When done

- plsw.lgo fits target (or has a documented reason for deviation).
- Every consumer of pl-sw can compile arbitrarily-large inputs without buffer cliffs.
- The 1 MB SRAM ceiling stops being a recurring blocker for *any* future PL/SW saga.
- The `_MAX` constants left in the source are right-sized to actual peaks, not aspirational ceilings.
