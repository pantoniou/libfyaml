struct ca_input_item {
	char *command;
	char *path;
};

// yaml: { flatten-field: input }
struct ca_input {
	// yaml: { remove-prefix: caif_ }
	enum {
		caif_string,
		caif_object,
	} input_form;
	// yaml: { field-auto-select: true }
	union {
		char *string;
		struct ca_input_item *object;
	} input;
};

struct foo {
	struct ca_input input;
};
