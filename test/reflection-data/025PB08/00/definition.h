// yaml: { key: key }
struct foo {
	struct {
		int one;
		char *two;
	} key;
	int value;
};

// yaml: { key: key }
typedef struct foo foo_struct;

// yaml: { key: key }
typedef struct foo *foo_struct_p;

typedef foo_struct_p foo_map;

struct baz {
	int count;
	foo_map foos;	// yaml: { counter: count }
	foo_map boos;
};
