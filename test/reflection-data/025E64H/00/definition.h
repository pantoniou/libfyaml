// yaml: { flatten-field: b }
struct flatten {
	int b;
};

struct foo {
	struct flatten a;
};
