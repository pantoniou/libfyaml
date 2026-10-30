// SPDX-License-Identifier: MIT

/*
 * libclang gives a comment after the closing brace of a struct to the last
 * field. cparse gives it to nothing. Only the expected output tests this.
 */
struct B { int a; };   // trailing after struct B
