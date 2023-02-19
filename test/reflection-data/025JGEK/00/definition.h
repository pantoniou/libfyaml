typedef unsigned int uint;

typedef uint uint2;

typedef char *string;

struct foo {
	uint2 a;
	uint2 b : 4;
	_Bool c;
	string d;
};
