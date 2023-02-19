struct bar {
	int a;
	int b;
};

typedef struct string_list {
	int count;
	char **strings;		/* yaml: { counter: count } */
} string_list_t;

/* yaml: { flatten-field: strings } */
struct string_list_flat {
	int count;
	char **strings;		/* yaml: { counter: count } */
};

struct foo {
	int *values;		/* yaml: { counter: count } */
	int count;
	int *more_values;	/* yaml: { counter: more_count } */
	int more_count;
	char **strings;		/* yaml: { counter: strings_count } */
	int strings_count;
	struct bar *bar;
	int ***pointers;
	string_list_t *slist;
	struct string_list_flat sflist;
};
