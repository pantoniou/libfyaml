#ifndef LIBFYAML_EXAMPLE_REFLECTION_CONFIG_H
#define LIBFYAML_EXAMPLE_REFLECTION_CONFIG_H

struct service_port {
	char *name;
	int port;
};

struct service_config {
	char *listen;
	int count;
	struct service_port *ports;	/* yaml: { key: name, counter: count } */
};

#endif
