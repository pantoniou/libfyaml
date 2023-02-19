#include <stdbool.h>

struct baz {
	unsigned int : 3;
	unsigned int a : 5;
#if 0
	_Bool b : 1;
#else
	bool b : 1;
#endif
	signed long c : 12;
};
