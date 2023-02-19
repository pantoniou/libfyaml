struct foo {
	enum {
		field_one,
		field_two,
	} union_selector;
	// yaml: { selector: union_selector }
	union {
		int one;	// yaml: { select: field_one }
		char *two;	// yaml: { select: field_two }
	} u;
};
