struct foo {
	int value;
	struct foo *next_foo;	// yaml: { null-allowed: true }
};
