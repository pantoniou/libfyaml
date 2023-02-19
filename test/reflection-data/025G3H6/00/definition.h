struct bar {
	int *values;		/* yaml: { counter: count } */
	int count;
};

struct foo {
	int *values;		/* yaml: { counter: count } */
	int count;
	int *more_values;	/* yaml: { counter: more_count } */
	int more_count;
	struct bar bar;
};
