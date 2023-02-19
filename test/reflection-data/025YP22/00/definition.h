/* fwd declaration */
struct bar;
// struct bar { int a; };

struct foo {
	struct bar *b1;	// yaml: { name: bar1 }
	struct bar *b2;	// yaml: { name: bar2 }
};

struct bar { int a; };
