# Comment Handling in libfyaml

## Applicability

Comment handling is an **experimental, opt-in** feature. The behaviour
described in this document is only active when the relevant flags are set:

- **Parsing:** pass `FYPCF_PARSE_COMMENTS` in the parser configuration flags
  (`fy_parse_cfg.flags`). Without this flag comments are silently discarded
  during scanning, as the YAML specification requires.
- **Emitting:** pass `FYECF_OUTPUT_COMMENTS` in the emitter configuration
  flags (`fy_emitter_cfg.flags`). Without this flag attached comments are
  ignored during emission even if they were parsed.

Both flags must be set for a full round-trip. Setting only one is valid but
produces partial behaviour (e.g. parsing and storing comments without
re-emitting them, which is useful for comment-aware transformation tools).

Comment handling is not applicable in JSON input mode (`FYPCF_JSON_FORCE` or
`FYPCF_JSON_AUTO` with a `.json` input): JSON does not permit comments.

## YAML specification position

The YAML 1.2.2 specification defines comment syntax in §6.6 (production rules
[75]–[79]) and specifies that comments are valid as line separators in §6.7.
However, the spec explicitly excludes comments from the information model:

> "Comments are a presentation detail and must not have any effect on the
> serialization tree or representation graph."
> — YAML 1.2.2 §3.2.3.3

The specification gives **no guidance on comment preservation or placement
during a round-trip (parse → modify → emit) cycle**. This is an intentional
gap: the spec treats comments as belonging to the serialisation layer only.

libfyaml's comment handling is therefore an implementation extension above and
beyond the specification. The rules described in this document are libfyaml's
own conventions, not requirements of the YAML standard.

## Design goal

The original intent of comment support in libfyaml was simply "keep comments
around — they are useful". The current direction extends this toward
**round-trip fidelity**: after a parse → emit cycle the output should be
recognisable to the original author. Comments should appear at approximately
the same position relative to the content they annotate, and they should
survive operations such as key/sequence sorting.

This is a best-effort goal. Highly unusual comment placements (e.g. a comment
between a mapping key and its value indicator on the same line) may not
round-trip perfectly.

## Placement model

Every comment is stored relative to a specific token and assigned one of three
canonical placements:

| Placement | Meaning |
|-----------|---------|
| `fycp_top` | Comment on the line(s) immediately preceding the token |
| `fycp_right` | Comment on the same line as the token, after its content |
| `fycp_bottom` | Comment on the line(s) immediately following a block that the token closes |

These placements are stored and emitted independently. A token may carry
comments at more than one placement simultaneously (e.g. both a top and a
right comment).

The emitter reconstructs the original column from an `indent_delta` stored at
parse time. `indent_delta` is the distance between the comment's column and the
indent level of the block it belongs to. This decouples emission from the order
in which nodes are output, so that sort operations do not corrupt indentation.

## Parser comment buffer invariants

The parser holds at most two unattached comment atoms at any time, in fields
`last_comment` and `override_comment`:

- **`last_comment`** — the most recently scanned comment that has not yet been
  attached to a token.
- **`override_comment`** — a previously buffered comment that was displaced
  from `last_comment` when a second comment was scanned before the first was
  consumed. It is typically an older, more deeply nested comment.

Invariant: when `override_comment` is set, `last_comment` is also set.

Comments are consumed by `fy_attach_comments_if_any()`, which attaches them to
the next token that is produced. At most one comment ends up at `fycp_top` and
at most one at `fycp_right` per call; `override_comment` is always checked
(and consumed) before `last_comment`.

## Document-level comment rule

A comment separated from the first content token of a document by **at least
one blank line** is considered a document-level comment. It belongs to the
document as a whole, not to the first content token.

This is implemented as a promotion: the comment is moved from `last_comment`
into `override_comment` so that it is attached to the synthetic
`DOCUMENT_START` token rather than the first scalar/mapping/sequence token.

The blank-line threshold (≥ 2 line difference between the comment's end line
and the token's start line) is a heuristic. The YAML spec does not define the
concept of a document-level comment.

## Block scalar rule

For block scalars (`|` literal, `>` folded), the scanner advances past the
final content linebreak before the token's `end_mark` is recorded. This means
`end_mark.line` is one line beyond the actual content.

When testing whether a comment is on the same line as a token (and therefore
a right-hand comment rather than a top comment for the *next* token), block
scalars must use `start_mark.line` — the line of the `|` or `>` indicator —
rather than `end_mark.line`. Inline comments on block scalars can only appear
on the indicator line itself.

## Block-end attribution rule

When a block (mapping or sequence) is closed by `fy_parse_unroll_indent()`,
any buffered comment that is:

- more deeply indented than the target indent level, **and**
- at least as indented as the block being closed

is considered to belong to that block rather than to the next token at the
parent level. It is attached as a `fycp_bottom` comment on the `BLOCK_END`
token.

`override_comment` is checked first (it is the older, typically deeper
comment). `last_comment` is checked second. Comments that do not meet the
column threshold are left in the buffer to be attached to the next token.

## Sort-stability contract

A `fycp_top` comment must travel with the node it annotates through sort
operations (`fy_node_mapping_sort`, `fy_node_sequence_sort`). The token that
carries the comment is therefore chosen to match what the sort moves:

- **Standalone mapping key**: the `fycp_top` comment is left on the key scalar
  token. `fy_node_mapping_sort` moves key tokens, so the comment follows.
- **Sequence item that begins with a mapping**: the `fycp_top` comment is moved
  from the key scalar to the `BLOCK_MAPPING_START` token.
  `fy_node_sequence_sort` moves sequence items (whose representative token is
  `BLOCK_MAPPING_START`), so the comment follows.

The distinction between these two cases is signalled by the
`pending_seq_item_key` flag in the parser state, which is set when a block
sequence entry (`-`) is processed and cleared once the implicit mapping's value
indicator (`:`) has been seen.

## Emitter contract

The emitter reads `indent_delta` from each comment atom and reconstructs the
comment's column as `block_indent + indent_delta`. This means:

1. `indent_delta` must be computed at the time the comment is attached, using
   the indent level of the block the comment belongs to — not the parent.
2. The emitter must pass the correct `old_indent` (the block's own indent) when
   emitting `fycp_bottom` comments, so the column reconstruction is consistent
   with what was stored.

Violating this contract causes comments to shift left or right after a sort or
any other operation that changes the order of emitted nodes.