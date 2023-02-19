struct bar {
	int value;
};

struct foo {
	struct bar *bar;
	void (*func)(int bar);
};
