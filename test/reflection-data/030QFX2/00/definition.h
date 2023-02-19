struct bar {
	int value;
	char *description;
};

struct foo {
	// yaml: { remove-prefix: field_ }
	enum {
		field_text,
		field_bar,
		field_bars
	} union_selector : 2;
	// yaml: { selector: union_selector, field-auto-select: true }
	union {
		char *text;
		struct bar bar;
		struct bar **bars;
	} common;
};
