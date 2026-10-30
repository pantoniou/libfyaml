// SPDX-License-Identifier: MIT

/* a struct */
struct S { int a; char b; long c; };
typedef struct S S_t;
struct P { char c; int i : 3; int j : 5; short s; };
union U { int i; char c[10]; double d; };
enum E { A, B = 5, C };
enum F { NEG = -1, POS = 3 };
typedef struct { int x, y; } Point;
struct __attribute__((packed)) Pk { char c; int i; };
struct Flex { int n; char data[]; };
typedef int (*cb_t)(int, const char *, ...);
typedef unsigned long arr_t[4];
struct Nest { struct { int a; int b; }; union { int u; float f; } un; Point pt; cb_t cb; };
int array[sizeof(struct S) + 1];
#pragma pack(push, 2)
struct Packed2 { char c; int i; };
#pragma pack(pop)
struct Packed2b { char c; int i; };
extern int fn(struct S *p) { return 0; }
