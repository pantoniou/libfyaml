"""PyYAML error module compatibility.

Provides YAMLError, MarkedYAMLError, and Mark classes matching PyYAML's API.
"""


class Mark:
    """Represents a position in a YAML stream.

    Attributes:
        name: The name of the stream (e.g., filename or '<string>')
        index: Character index in the stream
        line: Line number (0-indexed)
        column: Column number (0-indexed)
        buffer: Optional buffer containing the source
        pointer: Optional pointer into the buffer
    """

    def __init__(self, name, index, line, column, buffer=None, pointer=None):
        self.name = name
        self.index = index
        self.line = line
        self.column = column
        self.buffer = buffer
        self.pointer = pointer

    def get_snippet(self, indent=4, max_length=75):
        """Return a snippet of the source at this mark."""
        if self.buffer is None:
            return None

        # Find the line containing the mark
        head = ''
        start = self.pointer
        while start > 0 and self.buffer[start - 1] not in '\0\r\n\x85\u2028\u2029':
            start -= 1
            if self.pointer - start > max_length / 2 - 1:
                head = ' ... '
                start += 5
                break

        tail = ''
        end = self.pointer
        while end < len(self.buffer) and self.buffer[end] not in '\0\r\n\x85\u2028\u2029':
            end += 1
            if end - self.pointer > max_length / 2 - 1:
                tail = ' ... '
                end -= 5
                break

        snippet = self.buffer[start:end]
        return ' ' * indent + head + snippet + tail + '\n' + \
               ' ' * (indent + self.pointer - start + len(head)) + '^'

    def __str__(self):
        snippet = self.get_snippet()
        where = "  in \"%s\", line %d, column %d" % (self.name, self.line + 1, self.column + 1)
        if snippet is not None:
            where += ":\n" + snippet
        return where


class YAMLError(Exception):
    """Base exception for YAML errors."""
    pass


class MarkedYAMLError(YAMLError):
    """YAML error with position marks.

    Attributes:
        context: Description of the parsing context
        context_mark: Mark pointing to the context
        problem: Description of the problem
        problem_mark: Mark pointing to the problem
        note: Additional note about the error
    """

    def __init__(self, context=None, context_mark=None,
                 problem=None, problem_mark=None, note=None):
        self.context = context
        self.context_mark = context_mark
        self.problem = problem
        self.problem_mark = problem_mark
        self.note = note

    def __str__(self):
        lines = []
        if self.context is not None:
            lines.append(self.context)
        if self.context_mark is not None \
                and (self.problem is None or self.problem_mark is None
                     or self.context_mark.name != self.problem_mark.name
                     or self.context_mark.line != self.problem_mark.line
                     or self.context_mark.column != self.problem_mark.column):
            lines.append(str(self.context_mark))
        if self.problem is not None:
            lines.append(self.problem)
        if self.problem_mark is not None:
            lines.append(str(self.problem_mark))
        if self.note is not None:
            lines.append(self.note)
        return '\n'.join(lines)
