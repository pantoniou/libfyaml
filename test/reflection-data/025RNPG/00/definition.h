typedef unsigned int **uintpp;

struct bar {
	int b;
};

typedef struct bar *barp;

typedef const int cint;

typedef int inti;

struct foo {
	uintpp app;
	const barp barp;
	const int c;
	cint cc;
	const cint ccc;
	const inti cccc;
};
