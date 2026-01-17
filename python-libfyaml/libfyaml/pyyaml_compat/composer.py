"""PyYAML composer module compatibility.

Provides Composer classes and ComposerError for API compatibility.
"""

from .error import MarkedYAMLError


class ComposerError(MarkedYAMLError):
    """YAML composer error (e.g., undefined alias)."""
    pass


class Composer:
    """YAML composer stub."""
    pass
