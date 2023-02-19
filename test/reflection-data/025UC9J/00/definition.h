struct foo {
	int *value0;
	int *value;		/* yaml: { foo: bar } */
	int **pointers;
	int ***more_pointers;
	/*
	 * yaml:
	 *   foo: bar
	 */
	int *value_x;
};
