// SPDX-License-Identifier: MIT

/* the parts of the declarations that depend on the target ABI */
typedef char t_char;
typedef signed char t_schar;
typedef unsigned char t_uchar;
typedef short t_short;
typedef unsigned short t_ushort;
typedef int t_int;
typedef unsigned int t_uint;
typedef long t_long;
typedef unsigned long t_ulong;
typedef long long t_llong;
typedef unsigned long long t_ullong;
typedef float t_float;
typedef double t_double;
typedef long double t_ldouble;
typedef _Bool t_bool;
typedef void *t_ptr;
typedef char *t_cptr;
typedef int (*t_fp)(int);
typedef __builtin_va_list t_va;

struct Members { char c; short s; int i; long l; long long ll; float f; double d; long double ld; void *p; };
struct AfterChar_d { char c; double d; };
struct AfterChar_ll { char c; long long ll; };
struct AfterChar_ld { char c; long double ld; };
struct AfterChar_a { char c; double a[2]; };
struct AfterChar_p { char c; void *p; };
struct AfterChar_l { char c; long l; };
union Union { char c; double d; long double ld; long long ll; };
struct Bits { char c; unsigned u : 3; long l : 5; char d : 7; int i : 20; long long ll : 40; };
struct BitsZero { char c; int a : 3; int : 0; int b : 3; };
struct PackedBits { char c; int i : 20 __attribute__((packed)); };
struct Packed { char c; double d; } __attribute__((packed));
struct Aligned { char c; } __attribute__((aligned(16)));
struct Flex { int n; double d[]; };
struct Vec { char c; int v __attribute__((vector_size(16))); };
enum E1 { E1_A, E1_B = 5 };
enum E2 { E2_NEG = -1, E2_POS = 3 };
enum E3 { E3_BIG = 0x80000000 };
struct Nested { char c; struct { long long a; char b; } in; short s; };
char array_size[sizeof(long) == 8 ? 1 : 2];
