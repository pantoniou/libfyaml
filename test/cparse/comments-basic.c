// SPDX-License-Identifier: MIT

/* leading block */
struct A {
	int x; /* trailing block */
	int y; // trailing line
	// leading line for z
	int z;
	/// doc for w
	int w; ///< trailing doc
};
// two
// line leading
struct B { int a; };
/** doc */ typedef int T; // t trailing
enum E {
	E1, // after E1
	E2 /* after E2 */,
	/* before E3 */ E3
};
struct C;  // fwd trailing
// blank line separated

struct D { int d; };
int x; // comment on a var
struct F { int f1; /* between */ int f2; };
#define M 1 // not a decl
struct G { // after brace
	int g;
};
/* detached */

/* attached to H */
struct H { int h; };
struct I {
	int i1;

	// separated by a blank line

	int i2;
	int i3; // t3
	// before i4
	int i4; // t4
};
