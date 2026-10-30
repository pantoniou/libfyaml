// SPDX-License-Identifier: MIT

/* the macros of a compiler, as `cc -dM -E` prints them; the text is dropped */
#define _MSC_VER 1940
#define MY_WIDTH 7
#define MY_NAME(x) my_##x
int leaked_declaration;
