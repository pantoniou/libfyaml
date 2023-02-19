struct baz {
	int value;	/* yaml: { default: 5 } */
};

struct foo {
	struct {
		int value;
	};
	struct {
		int value;
	} bar;
	struct baz baz;
};
