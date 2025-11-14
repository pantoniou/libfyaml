"""PyYAML scanner module compatibility.

Provides Scanner classes and ScannerError for API compatibility.
"""

from .error import MarkedYAMLError


class ScannerError(MarkedYAMLError):
    """YAML scanner error (tokenization phase)."""
    pass


class Scanner:
    """YAML scanner stub."""
    pass
