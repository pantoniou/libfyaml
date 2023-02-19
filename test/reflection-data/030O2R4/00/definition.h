// yaml: { skip-unknown: true }
struct input_item {
	char *command;
	char *path;
};

// yaml: { flatten-field: input }
struct input {
	// yaml: { remove-prefix: if_ }
	enum {
		if_string,
		if_object,
	} input_form;
	// yaml: { field-auto-select: true }
	union {
		char *string;
		struct input_item *object;
	} input;
};

struct foo {
	struct input *input;
};
