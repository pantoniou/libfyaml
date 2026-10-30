// SPDX-License-Identifier: MIT

#include "inc1.h"
#include "inc1.h"
#include "inc2.h"
#include "inc2.h"
#define OBJ 1 + 2
#define FN(a, b) ((a) * (b) + OBJ)
int x = FN(3, OBJ);
#define STR(x) #x
#define XSTR(x) STR(x)
char *s = XSTR(FN(1, 2)), *t = STR(  a   +   "q\"w"  b );
#define CAT(a, b) a ## b
#define XCAT(a, b) CAT(a, b)
int CAT(foo, bar) = XCAT(OBJ, 1);
#define foo foo + 1
int y = foo;
#define a b
#define b a
int z = a + b;
#define EMPTY
#define LPAREN (
#define F(x) [x]
int w = F EMPTY (1);
#define VA(fmt, ...) printf(fmt, ## __VA_ARGS__)
VA("a") VA("a", 1, 2) VA("a",)
#define VA2(...) f(__VA_ARGS__) g(#__VA_ARGS__)
VA2() VA2(1) VA2(1, 2)
#define NAMED(args...) h(args)
NAMED() NAMED(1,2)
#define OPT(a, ...) a __VA_OPT__(, __VA_ARGS__ !) end
OPT(1) OPT(1, 2) OPT(1, 2, 3)
#if defined(OBJ) && (OBJ == 3) && !defined NOPE
int cond1;
#elif 1
int cond2;
#else
int cond3;
#endif
#if (1 ? 2 : (1/0)) == 2 && -1 < 0 && -1 > 0u && (1 << 3) == 8 && 'a' == 97
int cond4;
#endif
#if 0
#if garbage (
#else
#endif
int dead;
#else
int alive;
#endif
#ifdef __FILE__
char *f = __FILE__;
#endif
int c0 = __COUNTER__, c1 = __COUNTER__, l = __LINE__;
#pragma pack(push, 1)
int packed;
#pragma pack(pop)
#define EMPTYARG(x) <x>
EMPTYARG() EMPTYARG( )
#define SWAP(a,b) b a
SWAP(1, SWAP(2,3))
#define HASH_HASH # ## #
#define MKSTR(a) # a
char *u = MKSTR(HASH_HASH);
