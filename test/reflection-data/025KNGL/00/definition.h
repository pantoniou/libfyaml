// yaml: { flatten-field: b }
struct flatten {
	int b;
};

// yaml: { flatten-field: strings }
struct flatten2 {
	int count;
	char **strings;	// yaml: { counter: count }
};

struct foo {
	struct flatten a;
	struct flatten2 c;
};
