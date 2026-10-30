// SPDX-License-Identifier: MIT

/* a typedef with an alignment of its own, larger or smaller than its type */
struct desc {
	unsigned long long addr;
	unsigned int len;
	unsigned short flags;
	unsigned short next;
};

typedef struct desc __attribute__((aligned(16))) desc_t;
typedef int int_a8 __attribute__((aligned(8)));
typedef int int_a2 __attribute__((aligned(2)));
typedef struct desc desc_arr_t[2] __attribute__((aligned(32)));
typedef int_a8 int_again;

typedef struct {
	char c;
	int v;
} wide_t __attribute__((aligned(16)));

struct user {
	char c;
	desc_t d;
	char c2;
	int_a2 v;
	char c3;
	desc_arr_t arr;
	int_again again;
};
