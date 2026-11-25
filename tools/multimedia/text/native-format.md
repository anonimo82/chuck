# CKTXTDOC version 1

All integers are little-endian u32; signed RGB sentinel -1 is 0xffffffff. All real fields are IEEE-754 binary64 little-endian. Strings are u32 byte length followed by UTF-8 bytes, no terminator. Counts and indices are u32. No padding.

Header: 8 bytes ASCII CKTXTDOC, version=1, paragraph count, table/grid count, tree root count. Then paragraphs, table blocks, node blocks. Each block begins with paragraph boundary (number of preceding paragraphs) and canonical global insertion rank (0-based, dense across table/grid and node blocks).

Style: font string, size real, weight, italic, underline, strikethrough, script, rgb, background, spacing real, rise real, opacity real.

Paragraph: text string, baseBytes, style, run count, each run (text string, style), alignment, wrapMode, direction, lineSpacing real, indent real, marginLeft/Right/Top/Bottom real, paddingLeft/Right/Top/Bottom real, borderWidth real, borderColor, backgroundColor, box opacity real, marker string, list mode, number, level, levelIndent real, markerWidth real.

Table/grid: grid flag, rows, cols, columnGap, rowGap, cols real weights, rows*cols paragraphs (row-major).

Node: kind, paragraph record, gap real, columnGap, rowGap, rows, cols, weight count, real weights, child count, recursive child records. Kinds 0 group, 1 paragraph, 2 container, 3 table, 4 grid, 5 list. Existing in-memory limits apply; maximum file size 32 MiB. The file must end immediately after the last record. Version 1 is uncompressed and has no checksum. Page configurations and keyframe curves are external objects, not included.

## Reader invariants

Reader validates all ranges against the corresponding public API, checks UTF-8 boundaries and exact reconstruction of run text, validates separate paragraph/table/tree budgets and maximum depth, verifies unique dense insertion ranks and nondecreasing anchors, and rejects trailing bytes. Load is transactional. No checksum or authentication is provided.
