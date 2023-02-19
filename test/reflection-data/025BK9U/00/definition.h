struct foo;
struct bar;
struct baz;

struct baz {
	struct bar *bar;	// yaml: { null-allowed: true }
};

struct bar {
	int bar_value;
	struct foo *foo;	// yaml: { null-allowed: true }
	struct baz *baz;	// yaml: { null-allowed: true, required: false }
};

struct foo {
	int foo_value;
	struct bar *bar;	// yaml: { null-allowed: true }
};
