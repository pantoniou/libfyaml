// SPDX-License-Identifier: MIT

/* the bit-field rules differ between ABIs: unnamed and zero width fields, and wide types */
struct unnamed_gap { char a; int : 3; char b; };
struct unnamed_wide { char a; long long : 5; char b; };
struct zero_char { char a; int : 0; char b; };
struct zero_short { short a : 3; long long : 0; short b : 3; };
struct zero_first { int : 0; char a; };
struct llong_bits { char a; long long b : 40; char c; };
struct straddle { char a; unsigned short b : 12; unsigned short c : 12; };
struct wide_straddle { int a : 20; int b : 20; int c : 20; };
struct packed_bits { char a; int b : 12; char c; } __attribute__((packed));
struct align_bits { char a; int b : 4 __attribute__((aligned(8))); };
struct mixed { char a : 3; short b : 9; long long c : 33; char d; };
struct only_unnamed { int : 7; };
struct bool_bits { _Bool a : 1; _Bool b : 1; int c : 3; };
union bit_union { int a : 3; char b : 5; long long c : 40; };
struct long_double_after { char a : 2; long double b; };
