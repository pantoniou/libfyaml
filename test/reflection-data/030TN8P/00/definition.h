struct foo {
	// yaml: { remove-prefix: field_ }
	enum {
		field_one,
		field_two,
	} union_selector;
	union {
		int one;
		char *two;
	};
};
