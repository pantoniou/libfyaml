struct baz {
	char *heh;
};

typedef char *str_t;

struct bar {
	char *string;			/* yaml: { a-string: false } */
	char *string2;			/* yaml: { a-string: true } */
	char *string3;
	str_t string4;
	long long *ptrlong;
	struct baz baz_in_place;
	struct baz *baz_out_of_place;
	int *table;			/* yaml: { counter: table_count } */
	int table_count;

	// yaml:
        //   omit-if-null: false
        //   required: true
	char *null_req;

	int const_int[3];		/* yaml: { fill: -1 } */
};
