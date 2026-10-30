// SPDX-License-Identifier: MIT

typedef struct { int x, y; } Point;
struct Nest { struct { int a; int b; }; union { int u; float f; } un; };
typedef int arr_t[4];
typedef int (*fp_t)(int (*)[3], char *const *);
struct S2 { int a[3]; const char *names[2]; int (*pa)[4]; };
enum { ANON_A, ANON_B };
typedef enum { TE_A, TE_B } TE;
