struct foo {
	char *key;
	int value;
};

struct baz {
	struct foo **foos;	// yaml: { key: key }
};
