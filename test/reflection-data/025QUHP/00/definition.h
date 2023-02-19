struct foo {
	char *key;
	int value;
};

struct baz {
	int foos_count;
	struct foo foos[5];	// yaml: { key: key, counter: foos_count }
};
