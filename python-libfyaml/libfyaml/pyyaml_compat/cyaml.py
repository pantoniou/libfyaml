"""PyYAML cyaml module compatibility stubs.

Provides C-accelerated parser/emitter stubs.
"""

from libfyaml.pyyaml_compat.parser import Parser


class CParser(Parser):
    """C-accelerated YAML parser stub."""
    pass


class CEmitter:
    """C-accelerated YAML emitter stub."""
    pass


# Define C-accelerated Loader/Dumper classes directly to avoid circular import
# These mirror the classes in __init__.py

class CBaseLoader:
    """C-accelerated base loader."""
    yaml_constructors = {}
    yaml_multi_constructors = {}

    @classmethod
    def add_constructor(cls, tag, constructor_func):
        cls.yaml_constructors[tag] = constructor_func

    @classmethod
    def add_multi_constructor(cls, tag_prefix, multi_constructor):
        cls.yaml_multi_constructors[tag_prefix] = multi_constructor


class CSafeLoader(CBaseLoader):
    """C-accelerated safe loader."""
    yaml_constructors = {}
    yaml_multi_constructors = {}


class CLoader(CSafeLoader):
    """C-accelerated full loader."""
    yaml_constructors = {}
    yaml_multi_constructors = {}


class CBaseDumper:
    """C-accelerated base dumper."""
    yaml_representers = {}
    yaml_multi_representers = {}

    @classmethod
    def add_representer(cls, data_type, representer_func):
        cls.yaml_representers[data_type] = representer_func

    @classmethod
    def add_multi_representer(cls, data_type, representer_func):
        cls.yaml_multi_representers[data_type] = representer_func

    def represent_mapping(self, tag, mapping):
        return mapping

    def represent_sequence(self, tag, sequence):
        return sequence

    def represent_scalar(self, tag, value, style=None):
        return value

    @staticmethod
    def represent_dict(dumper, data):
        return dumper.represent_mapping('tag:yaml.org,2002:map', data.items())

    @staticmethod
    def represent_list(dumper, data):
        return dumper.represent_sequence('tag:yaml.org,2002:seq', data)

    @staticmethod
    def represent_str(dumper, data):
        return dumper.represent_scalar('tag:yaml.org,2002:str', data)


class CSafeDumper(CBaseDumper):
    """C-accelerated safe dumper."""
    yaml_representers = {}
    yaml_multi_representers = {}


class CDumper(CSafeDumper):
    """C-accelerated full dumper."""
    yaml_representers = {}
    yaml_multi_representers = {}
