// SPDX-License-Identifier: MIT

enum { A = 1L << 31 };
enum { B = sizeof(int) - 5 > 0 };
enum { C = 0x7fffffffL + 1 > 0 };
enum { D = -1L < 0u };
