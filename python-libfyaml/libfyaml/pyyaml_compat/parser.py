"""PyYAML parser module compatibility.

Provides Parser classes and ParserError for API compatibility.
"""

from .error import MarkedYAMLError


class ParserError(MarkedYAMLError):
    """YAML parser error (grammar phase)."""
    pass


class Parser:
    """YAML parser stub."""
    pass
