struct foo {
	char *key;
	int value;
};

typedef struct foo foo_struct;

// yaml: { key: key }
typedef struct foo *foo_struct_p;

typedef foo_struct_p foo_map;

struct baz {
	int count;
	foo_map foos;	// yaml: { counter: count }
	foo_map boos;
};
