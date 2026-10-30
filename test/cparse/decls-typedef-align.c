// SPDX-License-Identifier: MIT

/* an aligned attribute on a typedef sets the alignment of its uses */
struct vd {
	unsigned long long addr;
	unsigned int len;
	unsigned short flags;
	unsigned short next;
};

/* between the type and the name, and after the name */
typedef struct vd __attribute__((aligned(16))) vd_t;
typedef struct vd vd2_t __attribute__((aligned(16)));
typedef int int_a8 __attribute__((aligned(8)));
typedef int int_a2 __attribute__((aligned(2)));
typedef char ch_a4[3] __attribute__((aligned(4)));
typedef struct vd vd_arr_t[2] __attribute__((aligned(32)));
typedef int int_a16 __attribute__((aligned(16)));
typedef int_a16 int_again;

struct u { vd_t a; char c; };
struct w { int_a8 x; char c; };
struct x { char c; int_a2 v; };
struct y { char c; vd_arr_t arr; };
struct z { char c; int_again v; };
struct v { char c; ch_a4 s; };
