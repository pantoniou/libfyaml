enum e {
	e_one,
	e_two,
	e_three,
	e_invalid,
	e_default,
};

struct foo {
	int foo;	/* yaml: { default: 10 } */
	int bar;	/* yaml: { default: 100 } */
	enum e a;	/* yaml: { default: e_default } */
	enum e b;
	enum e *c;
	enum e d;
	enum e *f;	/* yaml: { terminator: e_invalid } */
};
