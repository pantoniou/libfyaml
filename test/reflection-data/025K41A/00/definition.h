struct bar {
	long long v;
	const char *foo;
	int *iv;	/* yaml: { terminator: -1 } */
	char **argv;	/* yaml: { terminator: NULL } */
	char **argv2;	/* implicit terminator NULL */
};
