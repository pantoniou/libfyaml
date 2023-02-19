struct foo {
	char *key;
	int value;
};

struct baz {
	int count;
	struct foo *foos;	// yaml: { key: key, counter: count }
};
