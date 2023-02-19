struct foo {
	int key : 6;
	int value : 16;
};

struct baz {
	struct foo **foos;	// yaml: { key: key }
};
