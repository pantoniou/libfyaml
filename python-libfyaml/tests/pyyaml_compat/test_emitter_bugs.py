"""
Simplified test cases for C emitter bugs in libfyaml.

Each test isolates a specific failure mode in the C _emit() function.
Tests are organized by bug category and demonstrate the minimal
reproduction case.

All these bugs are in the C library's emitter, not in the Python layer.
"""

import pytest
import libfyaml.pyyaml_compat as yaml
from libfyaml.pyyaml_compat.events import (
    StreamStartEvent, StreamEndEvent,
    DocumentStartEvent, DocumentEndEvent,
    ScalarEvent,
    SequenceStartEvent, SequenceEndEvent,
    MappingStartEvent, MappingEndEvent,
)


# ── Helpers ──────────────────────────────────────────────────────────

def _emit_mapping_value(value, style, flow=False):
    """Emit a scalar as a mapping value, return (output, roundtripped_value)."""
    events = [
        StreamStartEvent(),
        DocumentStartEvent(),
        MappingStartEvent(anchor=None, tag=None, implicit=True, flow_style=flow),
        ScalarEvent(anchor=None, tag=None, implicit=(True, True), value='key'),
        ScalarEvent(anchor=None, tag=None, implicit=(True, True), value=value, style=style),
        MappingEndEvent(),
        DocumentEndEvent(),
        StreamEndEvent(),
    ]
    output = yaml.emit(events)
    new_events = list(yaml.parse(output))
    scalars = [e for e in new_events if isinstance(e, ScalarEvent)]
    return output, scalars[1].value if len(scalars) >= 2 else None


def _emit_mapping_key(value, style):
    """Emit a scalar as a mapping key, return (output, roundtripped_value)."""
    events = [
        StreamStartEvent(),
        DocumentStartEvent(),
        MappingStartEvent(anchor=None, tag=None, implicit=True, flow_style=False),
        ScalarEvent(anchor=None, tag=None, implicit=(True, True), value=value, style=style),
        ScalarEvent(anchor=None, tag=None, implicit=(True, True), value='val'),
        MappingEndEvent(),
        DocumentEndEvent(),
        StreamEndEvent(),
    ]
    output = yaml.emit(events)
    new_events = list(yaml.parse(output))
    scalars = [e for e in new_events if isinstance(e, ScalarEvent)]
    return output, scalars[0].value if scalars else None


def _emit_seq_item(value, style, flow=False):
    """Emit a scalar as a sequence item, return (output, roundtripped_value)."""
    events = [
        StreamStartEvent(),
        DocumentStartEvent(),
        SequenceStartEvent(anchor=None, tag=None, implicit=True, flow_style=flow),
        ScalarEvent(anchor=None, tag=None, implicit=(True, True), value=value, style=style),
        SequenceEndEvent(),
        DocumentEndEvent(),
        StreamEndEvent(),
    ]
    output = yaml.emit(events)
    new_events = list(yaml.parse(output))
    scalars = [e for e in new_events if isinstance(e, ScalarEvent)]
    return output, scalars[0].value if scalars else None


def _emit_roundtrip_events(events):
    """Emit events and parse back, return (output, new_events_or_error)."""
    try:
        output = yaml.emit(events)
    except Exception as e:
        return None, str(e)
    try:
        new_events = list(yaml.parse(output))
        return output, new_events
    except Exception as e:
        return output, str(e)


# ═══════════════════════════════════════════════════════════════════════
# Bug 1: Plain style drops trailing newline
#
# When style='' (plain), the emitter silently drops the trailing '\n'
# from scalar values. Plain scalars cannot represent trailing newlines,
# so the emitter should fall back to a quoted or block style, but
# instead it emits invalid plain output that loses the newline.
# ═══════════════════════════════════════════════════════════════════════

class TestPlainDropsTrailingNewline:

    def test_simple_trailing_newline(self):
        """'text\\n' emitted as plain loses the trailing newline."""
        output, got = _emit_mapping_value('text\n', '')
        assert got == 'text\n', f"expected 'text\\n', got {got!r}; output={output!r}"

    def test_multiline_trailing_newline(self):
        """'line1\\nline2\\n' emitted as plain loses trailing newline."""
        output, got = _emit_mapping_value('line1\nline2\n', '')
        assert got == 'line1\nline2\n', f"got {got!r}; output={output!r}"

    def test_block_scalar_content_trailing_newline(self):
        """Content that was originally a block scalar, re-emitted as plain."""
        output, got = _emit_mapping_value('literal\n', '')
        assert got == 'literal\n', f"got {got!r}; output={output!r}"


# ═══════════════════════════════════════════════════════════════════════
# Bug 2: Plain style drops leading whitespace
#
# When style='' (plain), leading spaces in the scalar value are lost.
# Plain scalars cannot start with spaces (they'd be interpreted as
# indentation), so the emitter should fall back to quoted style.
# ═══════════════════════════════════════════════════════════════════════

class TestPlainDropsLeadingSpace:

    def test_leading_space(self):
        """' leading' emitted as plain loses the leading space."""
        output, got = _emit_mapping_value(' leading', '')
        assert got == ' leading', f"got {got!r}; output={output!r}"

    def test_leading_spaces(self):
        """'  two spaces' emitted as plain."""
        output, got = _emit_mapping_value('  two spaces', '')
        assert got == '  two spaces', f"got {got!r}; output={output!r}"


# ═══════════════════════════════════════════════════════════════════════
# Bug 3: Plain style drops trailing whitespace
#
# When style='' (plain), trailing spaces are lost. Plain scalars can't
# preserve trailing spaces, so the emitter should use a quoted style.
# ═══════════════════════════════════════════════════════════════════════

class TestPlainDropsTrailingSpace:

    def test_trailing_space(self):
        """'trailing ' emitted as plain loses the trailing space."""
        output, got = _emit_mapping_value('trailing ', '')
        assert got == 'trailing ', f"got {got!r}; output={output!r}"

    def test_trailing_spaces(self):
        """'two  ' emitted as plain."""
        output, got = _emit_mapping_value('two  ', '')
        assert got == 'two  ', f"got {got!r}; output={output!r}"


# ═══════════════════════════════════════════════════════════════════════
# Bug 4: Plain style drops leading newlines
#
# Values starting with newlines lose them in plain style.
# ═══════════════════════════════════════════════════════════════════════

class TestPlainDropsLeadingNewlines:

    def test_leading_newlines(self):
        """'\\n\\ntext\\n' emitted as plain loses leading newlines."""
        output, got = _emit_mapping_value('\n\ntext\n', '')
        assert got == '\n\ntext\n', f"got {got!r}; output={output!r}"

    def test_single_leading_newline(self):
        """'\\ntext' emitted as plain."""
        output, got = _emit_mapping_value('\ntext', '')
        assert got == '\ntext', f"got {got!r}; output={output!r}"


# ═══════════════════════════════════════════════════════════════════════
# Bug 5: Plain style doesn't escape comment indicators
#
# Values starting with '#' or containing ' #' are emitted as plain,
# which the parser interprets as comments, losing the content.
# ═══════════════════════════════════════════════════════════════════════

class TestPlainCommentIndicators:

    def test_value_starting_with_hash(self):
        """'# comment-like' emitted as plain is parsed as empty."""
        output, got = _emit_mapping_value('# comment-like', '')
        assert got == '# comment-like', f"got {got!r}; output={output!r}"

    def test_value_with_inline_hash(self):
        """'text # rest' emitted as plain loses everything after ' #'."""
        output, got = _emit_mapping_value('text # rest', '')
        assert got == 'text # rest', f"got {got!r}; output={output!r}"


# ═══════════════════════════════════════════════════════════════════════
# Bug 6: Single-quoted style loses indentation in multiline scalars
#
# When multiline content with significant leading whitespace is emitted
# in single-quoted style, the indentation is lost on re-parsing.
# The single-quoted scalar folding normalizes whitespace.
# ═══════════════════════════════════════════════════════════════════════

class TestSingleQuotedLosesIndentation:

    def test_indented_line(self):
        """'line1\\n  indented\\n' single-quoted loses '  ' indent."""
        output, got = _emit_mapping_value('line1\n  indented\n', "'")
        assert got == 'line1\n  indented\n', f"got {got!r}; output={output!r}"

    def test_bullet_list_indentation(self):
        """Bullet list with indentation loses '  ' prefix."""
        value = 'text\n\n  * bullet\n  * list\n\nend\n'
        output, got = _emit_mapping_value(value, "'")
        assert got == value, f"got {got!r}; output={output!r}"

    def test_deeply_indented(self):
        """Multiple indentation levels."""
        value = 'line1\n  two\n    four\nline4\n'
        output, got = _emit_mapping_value(value, "'")
        assert got == value, f"got {got!r}; output={output!r}"


# ═══════════════════════════════════════════════════════════════════════
# Bug 7: Single-quoted style loses tabs
#
# Tabs within single-quoted multiline scalars are dropped during
# the fold/unfold round-trip.
# ═══════════════════════════════════════════════════════════════════════

class TestSingleQuotedLosesTabs:

    def test_tab_in_multiline(self):
        """'text\\n \\tlines\\n' single-quoted drops the tab."""
        output, got = _emit_mapping_value('text\n \tlines\n', "'")
        assert got == 'text\n \tlines\n', f"got {got!r}; output={output!r}"

    def test_leading_tab(self):
        """'\\t\\ndetected\\n' single-quoted drops leading tab."""
        output, got = _emit_mapping_value('\t\ndetected\n', "'")
        assert got == '\t\ndetected\n', f"got {got!r}; output={output!r}"


# ═══════════════════════════════════════════════════════════════════════
# Bug 8: Unicode line separators (U+2028/U+2029) handled incorrectly
#
# The C emitter treats U+2028 (LINE SEPARATOR) and U+2029 (PARAGRAPH
# SEPARATOR) as line breaks inside block and plain scalars, causing
# the parser to split the scalar at these characters and lose content.
#
# In literal/folded styles, the U+2028/U+2029 becomes a real line break,
# and the content after it is lost or misinterpreted.
# In plain style, the character causes a line break that splits content.
# Single-quoted preserves U+2029 but double-quoted handles both.
# ═══════════════════════════════════════════════════════════════════════

class TestUnicodeLineSeparators:

    @pytest.mark.xfail(reason="C lib treats U+2028 as line break; PyYAML parser does not")
    def test_u2028_in_literal(self):
        """U+2028 in literal block scalar truncates at the separator."""
        output, got = _emit_mapping_value('text\u2028more', '|')
        assert got == 'text\u2028more', f"got {got!r}; output={output!r}"

    @pytest.mark.xfail(reason="C lib treats U+2028 as line break; PyYAML parser does not")
    def test_u2028_in_folded(self):
        """U+2028 in folded block scalar truncates at the separator."""
        output, got = _emit_mapping_value('text\u2028more', '>')
        assert got == 'text\u2028more', f"got {got!r}; output={output!r}"

    @pytest.mark.xfail(reason="C lib treats U+2028 as line break; PyYAML parser does not")
    def test_u2028_in_plain(self):
        """U+2028 in plain scalar truncates at the separator."""
        output, got = _emit_mapping_value('text\u2028more', '')
        assert got == 'text\u2028more', f"got {got!r}; output={output!r}"

    @pytest.mark.xfail(reason="C lib treats U+2029 as line break; PyYAML parser does not")
    def test_u2029_in_folded(self):
        """U+2029 in folded scalar truncates at the separator."""
        output, got = _emit_mapping_value('text\u2029more', '>')
        assert got == 'text\u2029more', f"got {got!r}; output={output!r}"

    def test_u2028_in_double_quoted(self):
        """U+2028 in double-quoted scalar — should work (uses \\L escape)."""
        output, got = _emit_mapping_value('text\u2028more', '"')
        assert got == 'text\u2028more', f"got {got!r}; output={output!r}"

    def test_u2029_in_single_quoted(self):
        """U+2029 in single-quoted scalar — happens to work."""
        output, got = _emit_mapping_value('text\u2029more', "'")
        assert got == 'text\u2029more', f"got {got!r}; output={output!r}"

    @pytest.mark.xfail(reason="C lib treats U+2028 as line break; PyYAML parser does not")
    def test_u2028_with_trailing_newline(self):
        """Combining U+2028 with trailing content produces extra newlines."""
        # The emitter inserts an extra \n after \u2028 in folded style
        output, got = _emit_mapping_value('trimmed\nspecific\u2028\nnone', '>')
        assert got == 'trimmed\nspecific\u2028\nnone', f"got {got!r}; output={output!r}"


# ═══════════════════════════════════════════════════════════════════════
# Bug 9: NUL character (\x00) truncates block scalars
#
# The C emitter writes NUL bytes into block scalar output, which
# truncates the string at the NUL position when parsed back.
# Double-quoted style handles NUL correctly (emits \0 escape).
# ═══════════════════════════════════════════════════════════════════════

class TestNulCharacterTruncation:

    def test_nul_in_literal(self):
        """'text\\x00end' in literal truncates at NUL."""
        output, got = _emit_mapping_value('text\x00end', '|')
        assert got == 'text\x00end', f"got {got!r}; output={output!r}"

    def test_nul_in_folded(self):
        """'text\\x00end' in folded truncates at NUL."""
        output, got = _emit_mapping_value('text\x00end', '>')
        assert got == 'text\x00end', f"got {got!r}; output={output!r}"

    def test_nul_in_double_quoted(self):
        """'text\\x00end' in double-quoted — should work (emits \\0)."""
        output, got = _emit_mapping_value('text\x00end', '"')
        assert got == 'text\x00end', f"got {got!r}; output={output!r}"


# ═══════════════════════════════════════════════════════════════════════
# Bug 10: Block scalars (|/>) produce broken YAML structure
#
# In certain contexts, forcing block scalar style produces YAML that
# the parser reads as a different event structure (different number
# of events). This happens with:
# - Root scalars containing unicode line separators
# - Empty string scalars ('') in literal/folded style
# - Block scalars inside sequences that also contain mappings
# ═══════════════════════════════════════════════════════════════════════

class TestBlockScalarBrokenStructure:

    @pytest.mark.xfail(reason="C lib treats U+2028 as line break; PyYAML parser does not")
    def test_literal_root_scalar_with_u2028(self):
        """Root scalar with U+2028 in literal style breaks document structure.

        The emitter writes the U+2028 as a real line break in the block scalar,
        and the parser then misinterprets the continuation as a new document.
        Original: 5 events. Reparsed: 4 events.
        """
        events = [
            StreamStartEvent(),
            DocumentStartEvent(),
            ScalarEvent(anchor=None, tag=None, implicit=(True, True),
                        value='specific\u2028trimmed\n\n\nas space', style='|'),
            DocumentEndEvent(),
            StreamEndEvent(),
        ]
        output, result = _emit_roundtrip_events(events)
        assert not isinstance(result, str), f"parse error: {result}; output={output!r}"
        assert len(result) == len(events), \
            f"expected {len(events)} events, got {len(result)}; output={output!r}"

    def test_literal_empty_string_in_sequence(self):
        """Empty string '' as literal in a sequence with mappings.

        '|- ' followed by nothing confuses the parser about the
        subsequent mapping structure.
        Original: 13 events. Reparsed: 11 events.
        """
        events = [
            StreamStartEvent(),
            DocumentStartEvent(),
            SequenceStartEvent(anchor=None, tag=None, implicit=True, flow_style=False),
            ScalarEvent(anchor=None, tag=None, implicit=(True, True), value='', style='|'),
            MappingStartEvent(anchor=None, tag=None, implicit=True, flow_style=False),
            ScalarEvent(anchor=None, tag=None, implicit=(True, True), value='foo', style='|'),
            ScalarEvent(anchor=None, tag=None, implicit=(True, True), value='', style='|'),
            ScalarEvent(anchor=None, tag=None, implicit=(True, True), value='', style='|'),
            ScalarEvent(anchor=None, tag=None, implicit=(True, True), value='bar', style='|'),
            MappingEndEvent(),
            SequenceEndEvent(),
            DocumentEndEvent(),
            StreamEndEvent(),
        ]
        output, result = _emit_roundtrip_events(events)
        assert not isinstance(result, str), f"parse error: {result}; output={output!r}"
        assert len(result) == len(events), \
            f"expected {len(events)} events, got {len(result)}; output={output!r}"

    @pytest.mark.xfail(reason="C lib treats U+2028 as line break; PyYAML parser does not")
    def test_folded_root_scalar_with_u2028(self):
        """Root scalar with U+2028 in folded style — same issue as literal."""
        events = [
            StreamStartEvent(),
            DocumentStartEvent(),
            ScalarEvent(anchor=None, tag=None, implicit=(True, True),
                        value='specific\u2028trimmed\n\n\nas space', style='>'),
            DocumentEndEvent(),
            StreamEndEvent(),
        ]
        output, result = _emit_roundtrip_events(events)
        assert not isinstance(result, str), f"parse error: {result}; output={output!r}"
        assert len(result) == len(events), \
            f"expected {len(events)} events, got {len(result)}; output={output!r}"

    def test_literal_empty_string_as_mapping_key(self):
        """Empty string as literal mapping key breaks structure.

        '? |-' followed by nothing, then ': value' is misread by the parser.
        """
        events = [
            StreamStartEvent(),
            DocumentStartEvent(),
            MappingStartEvent(anchor=None, tag=None, implicit=True, flow_style=False),
            ScalarEvent(anchor=None, tag=None, implicit=(True, True), value='', style='|'),
            ScalarEvent(anchor=None, tag=None, implicit=(True, True), value='val', style='|'),
            MappingEndEvent(),
            DocumentEndEvent(),
            StreamEndEvent(),
        ]
        output, result = _emit_roundtrip_events(events)
        assert not isinstance(result, str), f"parse error: {result}; output={output!r}"
        new_scalars = [e for e in result if isinstance(e, ScalarEvent)]
        assert len(new_scalars) == 2, \
            f"expected 2 scalars, got {len(new_scalars)}; output={output!r}"
        assert new_scalars[0].value == '', f"key: got {new_scalars[0].value!r}"
        assert new_scalars[1].value == 'val', f"val: got {new_scalars[1].value!r}"


# ═══════════════════════════════════════════════════════════════════════
# Bug 11: Literal/folded style loses single-newline-only scalars
#
# A scalar whose value is just '\n' emitted in literal style produces
# output that parses back as '' (empty string).
# ═══════════════════════════════════════════════════════════════════════

class TestBlockScalarNewlineOnly:

    def test_single_newline_literal(self):
        """'\\n' in literal style parses back as empty string."""
        output, got = _emit_mapping_value('\n', '|')
        assert got == '\n', f"got {got!r}; output={output!r}"

    @pytest.mark.xfail(reason="C lib: newline-only folded round-trips to empty")
    def test_single_newline_folded(self):
        """'\\n' in folded style."""
        output, got = _emit_mapping_value('\n', '>')
        assert got == '\n', f"got {got!r}; output={output!r}"

    def test_single_newline_plain(self):
        """'\\n' in plain style."""
        output, got = _emit_mapping_value('\n', '')
        assert got == '\n', f"got {got!r}; output={output!r}"


# ═══════════════════════════════════════════════════════════════════════
# Bug 12: Carriage return (\r) in block scalars
#
# Carriage return is normalized to \n in block scalar styles, losing
# the original \r character. The emitter should either preserve it
# or fall back to double-quoted style which can use the \r escape.
# (Note: BEL \x07 and VT \x0b ARE preserved in literal style.)
# ═══════════════════════════════════════════════════════════════════════

class TestCarriageReturnInBlockScalar:

    def test_cr_in_literal(self):
        """Carriage return \\r in literal is normalized to \\n."""
        output, got = _emit_mapping_value('a \r b', '|')
        assert got == 'a \r b', f"got {got!r}; output={output!r}"

    def test_cr_in_folded(self):
        """Carriage return \\r in folded."""
        output, got = _emit_mapping_value('a \r b', '>')
        assert got == 'a \r b', f"got {got!r}; output={output!r}"

    def test_cr_in_plain(self):
        """Carriage return \\r in plain."""
        output, got = _emit_mapping_value('a \r b', '')
        assert got == 'a \r b', f"got {got!r}; output={output!r}"


# ═══════════════════════════════════════════════════════════════════════
# Bug 13: Plain style with multiline as mapping key loses content
#
# Multi-line plain scalars used as mapping keys can lose lines when
# the emitter doesn't properly handle the ? indicator context.
# ═══════════════════════════════════════════════════════════════════════

class TestPlainMultilineKey:

    def test_multiline_plain_key_hashbang(self):
        """'#!/usr/bin/perl\\nprint ...' as plain key loses first line."""
        output, got = _emit_mapping_key('#!/usr/bin/perl\nprint "hi";\n', '')
        assert got == '#!/usr/bin/perl\nprint "hi";\n', \
            f"got {got!r}; output={output!r}"

    def test_multiline_plain_key_simple(self):
        """Simple two-line plain mapping key."""
        output, got = _emit_mapping_key('line1\nline2', '')
        assert got == 'line1\nline2', f"got {got!r}; output={output!r}"
