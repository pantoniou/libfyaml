// SPDX-License-Identifier: MIT

typedef unsigned char u8;
typedef unsigned int u32;

struct inner {
	int a;
	char b[4];
};

struct outer {
	struct inner in;
	int (*cb)(int x, char *y);
	const char *const names[3];
	unsigned int bits: 3;
	unsigned int more: 5;
	struct {
		int p;
		int q;
	} anon;
	union {
		int i;
		float f;
	} u;
	u8 flags[];
};

enum color {
	RED,
	GREEN = 5,
	BLUE
};

typedef struct outer outer_t;
typedef int (*fn_t)(void *, const volatile int *restrict);
typedef u32 (*table_t[2])(u32);
