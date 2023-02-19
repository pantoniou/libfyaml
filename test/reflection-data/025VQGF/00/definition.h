struct foo {
	int values[5];
	int *valuesp[2];
	int xvalues[3];	/* yaml: { fill: 10 } */
	char str[10];
	char *strs[10];	/* yaml: { fill: '' } */
	char (*strs2)[10];	// a pointer to a char[10], yes it's confusing */
};
