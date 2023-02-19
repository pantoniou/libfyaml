struct bar {
	char *text;
	int value;
};

struct pure {
	int a;
	double b;
};

struct impure {
	char *text;
	unsigned int a : 3;
};

struct filthy {
	char **data;
};

struct foo {
	int count;
	struct bar *bars;		// yaml: { counter: count }
	struct bar *bars_t;		// yaml: { terminator: { text: NULL, value: 0 } }
	struct pure *pures;		// yaml: { terminator: { a: 0, b: 0.0 } }
	struct impure *impures;		// yaml: { terminator: { text: this-is-the-end, a: 3 } }
	struct filthy *filthies;	// yaml: { terminator: { data: [ ] } }
};
