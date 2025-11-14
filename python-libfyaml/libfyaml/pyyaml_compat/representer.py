"""PyYAML representer module compatibility stubs."""

from libfyaml.pyyaml_compat.error import YAMLError


class RepresenterError(YAMLError):
    pass


class BaseRepresenter:
    """Base class for YAML representers."""
    yaml_representers = {}
    yaml_multi_representers = {}

    @classmethod
    def add_representer(cls, data_type, representer):
        """Add a representer for a specific type."""
        cls.yaml_representers[data_type] = representer

    @classmethod
    def add_multi_representer(cls, data_type, representer):
        """Add a multi-representer for a type and its subclasses."""
        cls.yaml_multi_representers[data_type] = representer

    def represent_data(self, data):
        """Represent data as YAML."""
        return data

    @staticmethod
    def represent_dict(dumper, data):
        """Represent a dict."""
        return dumper.represent_mapping('tag:yaml.org,2002:map', data.items())

    @staticmethod
    def represent_list(dumper, data):
        """Represent a list."""
        return dumper.represent_sequence('tag:yaml.org,2002:seq', data)

    @staticmethod
    def represent_str(dumper, data):
        """Represent a string."""
        return dumper.represent_scalar('tag:yaml.org,2002:str', data)


class SafeRepresenter(BaseRepresenter):
    """Safe representer that doesn't allow arbitrary Python objects."""
    pass


class Representer(SafeRepresenter):
    """Full representer."""
    pass
