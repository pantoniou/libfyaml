"""PyYAML resolver module compatibility stubs."""


class BaseResolver:
    """Base YAML tag resolver."""
    yaml_implicit_resolvers = {}
    yaml_path_resolvers = {}

    @classmethod
    def add_implicit_resolver(cls, tag, regexp, first):
        """Add an implicit resolver."""
        pass

    @classmethod
    def add_path_resolver(cls, tag, path, kind=None):
        """Add a path resolver."""
        pass


class Resolver(BaseResolver):
    """Standard YAML resolver."""
    pass
