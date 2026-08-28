# Neovim and RAD Debugger Text Architecture

A side-by-side of how two shipping programs store, address, edit and display text. Every statement below is from source at these commits:

- neovim `2fbc828` (2026-08-28), paths relative to `src/nvim/`
- vim `5ab969f` (2026-08-27), paths relative to `src/`, included where neovim diverged from it
- raddebugger `7a5a649` (2026-08-27), paths relative to `src/`

The two are not the same kind of program. Neovim is an editor: the buffer is mutable and every design choice serves that. RAD's text layer is a source viewer inside a debugger: the file is read-mostly, and its patch machinery exists to display speculative or modified content, not to serve interactive typing. Several rows below have an empty cell on one side for that reason.

## Document storage

**Neovim** — a B+tree over fixed 4096-byte blocks (`MEMFILE_PAGE_SIZE`, `memfile.c:69`), managed by `memfile.c` as a virtual-memory abstraction and structured by `memline.c`.

Pointer blocks are branches, holding `PointerEntry {blocknr_T pe_bnum; linenr_T pe_line_count; linenr_T pe_old_lnum; int pe_page_count;}` (`memline.c:112`). That is 24 bytes with padding, so `PB_COUNT_MAX` is `(4096 - 8) / 24` = 170 children per node.

Data blocks are leaves. Text is stored at the *end* of the block growing backwards, with a `unsigned db_index[]` array at the front growing forward, meeting in the middle at `db_txt_start` (`memline.c:141`). Per-line overhead inside the block is 4 bytes of index plus a NUL terminator. At 40-byte lines that is roughly 90 lines per block.

The topmost bit of each `db_index` entry is `DB_MARKED`, used by `:global` to mark a line, explicitly to avoid spending an extra byte per line (`memline.c:150`).

**RAD** — an immutable byte blob in a content-addressed store, keyed by a 128-bit hash. `fs_key_from_path_range(String8 path, Rng1U64 range, U64 endt_us)` keys a *byte range* of a path (`file_stream/file_stream.h`), and `file_stream.c:104` reads that range into an arena with the read itself split across lanes. There is no mmap. `C_KEY_HASH_HISTORY_COUNT` lets a lookup rewind to an earlier hash for the same key (`text/text.c:4279`).

## Line addressing

**Neovim** — there is no line table. Line numbers are derived by summing `pe_line_count` down the descent, so the counts live in the tree itself.

`ml_find_line()` (`memline.c`) resolves a line number in three tiers:

1. Locked block: if `ml_locked_low <= lnum <= ml_locked_high`, return immediately. Consecutive lines are in the same leaf, so a run of them costs one lookup.
2. Ancestor stack: on a miss, walk *up* `ml_stack` for a previously-descended node whose `[ip_low, ip_high]` still contains the target and restart the descent from there.
3. Root descent from block 1, `O(log n)`. With branching 170, three pointer levels address about 4.9M leaves.

**RAD** — a flat `Rng1U64 *lines_ranges`, one entry per line for the whole file, plus `lines_count` and `lines_max_size` (`text/text.h`, `TXT_TextInfo`). 16 bytes per line, allocated separately from the text.

It is built by a two-pass parallel scan: each lane counts its lines, `lane_sync_u64` publishes a prefix sum of per-lane bases, then each lane fills its slice of the array directly (`text/text.c:3597-3681`).

Line lookup is an array index.

When a file has been patched, `TXT_Patched` replaces the flat array with a `TXT_LineMap`: a linked list of `TXT_LineMapRangeNode {num_range, ranges*, delta}` (`text/text.h`). Unedited spans point back at the base `lines_ranges` with a byte delta applied, so a patch splits the map into pieces rather than rewriting it.

## Per-line cost

| | neovim | RAD |
|---|---|---|
| index bytes per line | 4 (+1 NUL), inside the block holding the text | 16, in a separate array |
| build cost | none; built as lines are inserted | one parallel scan of the whole file |
| random line access | `O(log n)` cold, `O(1)` within the locked block | `O(1)` |
| sequential line access | one block lookup per ~90 lines | pointer arithmetic |
| index after an edit | nothing to rebuild | piece-list with deltas, or rebuild |

## Edits

**Neovim** — `ml_replace()` / `ml_append_int()` operate on the data block directly (`memline.c:1998`). An insert that fits in the block's `db_free` writes text at `db_txt_start` and shifts the index array; a block that overflows splits.

Line-count fixups up the tree are deferred: an insert inside the locked block bumps `ml_locked_lineadd` and `ml_locked_high`, and `ml_lineadd()` propagates to the pointer blocks only when the block is released (`memline.c` in `ml_find_line`). Typing a character does not walk the tree.

A line too large for one page gets a block sized to hold it, spanning multiple pages (`memline.c:22`). A 100 MB single line is one 100 MB block, and `ml_get` returns a pointer into it.

**RAD** — `TXT_Patch {Rng1U64 range; String8 replace;}` (`text/text.h`): replace a byte range with a string. `mutable_text/mutable_text.h` carries the same shape as `MTX_Op`, pushed onto a per-thread ring buffer and applied by a dedicated mutation thread (`mtx_push_op`, `mtx_enqueue_op`).

## Memory under load

**Vim** calls `mf_release()` on every block allocation (`memfile.c:330,433`). It releases when `mf_used_count >= mf_used_count_max` (derived from `'maxmem'`) or `total_mem_used >> 10 >= p_mmt` (`'maxmemtot'`), walking `mf_used_last` backwards for the least-recently-used unlocked block and writing it to the swap file. Resident set is bounded by an option, independent of file size.

**Neovim** removed this. There is no `mf_release`, no `total_mem_used`, and no `'maxmem'`/`'maxmemtot'` in `options.lua` — only `mf_release_all()`, called on out-of-memory (`memfile.c:459`). Blocks stay resident; the swap file exists for crash recovery.

**RAD** holds the whole requested range resident in the hash store. Because the key includes a byte range, a windowed view of a large artifact is expressible, but the source-code path uses the whole-file key (`fs_key_from_path`).

## Undo

**Neovim** — `undo.c`. A tree, not a stack: `u_header` nodes chain through `uh_next`/`uh_prev` for the linear history and through `uh_alt_next`/`uh_alt_prev` for alternate branches, with `uh_seq` numbering them (`undo.c:5-50`).

Each `u_entry` records `ue_top`, `ue_bot`, `ue_lcount` and `ue_array` — a **copy of the full text of every line in the changed range** (`undo_defs.h:25-29`, filled at `undo.c:605`). Granularity is whole lines, and the cost is proportional to the lines touched, not the bytes changed.

**RAD** — none. The text and mutable_text layers contain no undo.

## Positions that survive edits

**Neovim** — `marktree.c`, a heavily modified kbtree, credited in its header as inspired by the marker tree of the Atom editor. Marks are stored at (row, col); `marktree_splice` updates every affected mark for a text change in one operation rather than touching them individually. Extmarks, and therefore highlights, diagnostics, virtual text and multicursor cursors, all ride on it.

**RAD** — no equivalent. Positions are byte offsets into an immutable blob; when the blob changes, its derived info is recomputed or remapped through the piece list.

## Tokens and syntax

**Neovim** — tree-sitter, via `lua/treesitter.c`. Parsing runs on the main thread with a deadline: `nts_parser_parse_buf(TSParser *p, const TSTree *old_tree, int bufnr, uint64_t timeout_ns)` calls `ts_parser_parse_with_options` with a timeout (`lua/treesitter.c:527-551`), so a long parse is time-sliced across frames rather than moved off-thread. Trees are incremental — `ts_tree_edit` on the previous tree.

**RAD** — hand-written lexers per language, `TXT_LangLexFunctionType(Arena *arena, U64 *bytes_processed_counter, String8 string)`. Output is a flat `TXT_TokenArray` of `TXT_Token {kind, Rng1U64 range}` for the whole file, non-incremental.

Tokens for a line range are found by binary search over that array (`text/text.c:3372+`), with `big_token_pts` as a coarse index. Progress is published through `bytes_processed`/`bytes_to_process` on `TXT_TextInfo`, so a partially-tokenized file is usable while the rest is still running.

## Concurrency

**Neovim** — the C core is single-threaded. The only `uv_thread_create` outside platform plumbing is in `os/pty_proc_win.c`, and there is no `uv_queue_work` in the core at all. Buffer operations, tree-sitter parsing and redraw all run on the event-loop thread.

**RAD** — a first-class lane API in `base/base_thread_context.h`: `lane_idx()`, `lane_count()`, `lane_range(count)`, `lane_sync()`, `lane_sync_u64(ptr, src)`, over `tctx_lane_barrier_wait`. Any task splits across lanes with a barrier and a broadcast. The file read, the line-range scan and tokenization all use it. Caches are filled asynchronously and read through an `Access` handle.

## Viewport

**Neovim** — only visible lines are drawn, each fetched by `ml_get`, which almost always hits the locked block.

**RAD** — the view asks for exactly the visible range: `txt_string_list_from_info_data_line_range` and `txt_line_tokens_slice_from_info_data_line_range` (`text/text.c:3358+`), both taking `Rng1S64 line_range` and clamping to `lines_count`.

## Multiple cursors and bulk edits

**Neovim** — two mechanisms, both replay-based.

The long-standing one has no cursors at all. Visual-block insert types the text once interactively on the first line; on `<Esc>`, `op_insert` extracts the `ins_len` inserted bytes from that line and `block_insert` replays that literal string into every remaining line in the block, one `ml_replace` per line (`ops.c:605-694`). `:global` with `:normal`, and macros, are the same idea at a different granularity. There is no per-cursor state to maintain, and no selection set to re-resolve.

Master also carries a core multicursor built on the same principle, documented at `runtime/doc/dev_arch.txt:539` (`*dev-multicursor*`), with the atom-capture layer in `input_cmdatom.c` and `Q` bound to adding a cursor (`runtime/doc/news.txt:136`). Its design:

- Cursors are extmarks in the `nvim.multicursor` namespace. The extmark is authoritative — it follows edits through `marktree_splice`, so there is no separate selection set to reconcile after a change.
- A user action is captured as a `CmdAtom`: the resolved, post-mapping keysequence, the same material dot-repeat replays. Not a macro of raw keys, but one semantic action.
- Queued atoms are replayed at every cursor on the "clock edge", the completion of a toplevel `normal_execute()`. Cursor-local state is swapped in per replay, so each cursor reads and writes its own registers.
- Undo is buffer-global and applied in bulk: one cascade produces exactly one undo state, and undo itself is never cascaded.
- The C core maintains only the model; display is driven off the extmark namespaces, and a GUI receives positions through the `win_extmark` UI event.

So the per-keystroke cost is N replays of one cheap local edit, plus one redraw of the visible lines and one undo entry.

**Zed**, as a contrasting data point on the same feature, maintains N real selections and pushes each keystroke through a layered display map. Profiling in `zed-industries/zed` issue #32051 at 1000 cursors reported roughly 24 ms per keystroke: ~11.8 ms post-edit display-map sync, ~3.7 ms CRDT `apply_local_edit`, ~3.5 ms resolving all N selections plus the per-cursor input loop, ~2.6 ms post-edit selection round-trip, ~2.4 ms `change_selections` and transaction machinery. The issue attributes it to the display sync re-running per-edit SumTree work through every layer even when no layer transforms anything, and to selections being re-resolved through the full display round-trip several times per keystroke.

**RAD** — not applicable; the text layer has no cursor model.

## Measured: flat index vs block tree

Both structures implemented standalone and run on generated source-like text, in `tools/text_document_bench.cpp`. Flat is one buffer plus a `uint64_t` line-start array; tree is a B+tree of 4096-byte blocks with per-child line counts, no global line table. Scan is 2000 viewports of 60 lines each, i.e. 120k line fetches; edit is one character inserted mid-document.

| | build ms | scan ms | rand ms | edit ms | index MB | text MB |
|---|---|---|---|---|---|---|
| **10 MB** flat | 7.7 | 0.11 | 0.00 | 0.27 | 4.2 | 10.0 |
| tree | 13.0 | 0.50 | 0.13 | 0.0001 | 1.7 | 11.3 |
| **100 MB** flat | 84.6 | 0.24 | 0.01 | 6.28 | 33.6 | 100.0 |
| tree | 145.2 | 0.82 | 0.30 | 0.0001 | 16.5 | 112.5 |
| **1 GB** flat | 739 | 0.25 | 0.01 | 69.0 | 536.9 | 1000.0 |
| tree | 1454 | 1.51 | 0.47 | 0.0001 | 165.0 | 1125.5 |

The edit column is the whole result. Flat is linear in file size, because inserting a byte moves the tail of the buffer and then shifts every line offset past the edit: 0.27ms at 10 MB, 6.3ms at 100 MB, 69ms at 1 GB. The tree is flat, because an insert touches one block and the line counts above it stay stale until a walk fixes them.

Index memory follows the per-line overhead exactly: 4 bytes inside the block that already holds the text, against 8 per line in a separate array that over-allocates as it grows, giving 3.3x at 1 GB.

The tree loses build time by 2x, and a viewport scan by 6x. One frame's 60 lines is 1/2000th of the scan column, so 0.75µs at 1 GB against 0.13µs. It also pays 12.5% more memory for the text itself, from partly-filled blocks.

Caveat on the tree's edit figure: line bytes grow backwards from the block end, so inserting at a line's start moves nothing at all. The rows above insert mid-block and do move bytes, bounded by the 4 KB block, which is where 0.0001ms comes from. A file consisting of one enormous line lands on the degenerate case and reports zero.

## Summary table

| | neovim | RAD |
|---|---|---|
| storage | B+tree of 4 KB blocks | immutable blob, content-addressed |
| line index | counts in tree nodes | flat 16 B/line array |
| line lookup | 3-tier cache, `O(log n)` worst | array index |
| edit | local, tree counts deferred | range-replace patch, queued to a thread |
| index after edit | nothing to rebuild | piece list with deltas |
| oversized line | block grows to fit | no special case |
| memory bound | vim: LRU to swap under `'maxmem'`; neovim: resident | resident, range-keyed loading available |
| undo | tree, whole-line copies | none |
| position tracking | marktree splice | none |
| syntax | tree-sitter, incremental, main thread with timeout | hand-written lexers, whole-file, lane-parallel |
| partial results | no | yes, `bytes_processed` progress |
| concurrency | single-threaded core | lane API across all cache fills |
| multiple cursors | extmarks plus semantic-action replay | none |
