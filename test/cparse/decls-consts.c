// SPDX-License-Identifier: MIT

enum E { A = 1 << 31, B = 0x80000000, C = -1, D = 3000000000, F = 2147483647 + 0, G = (1 << 30) * 2 };
struct V { int n; float v4 __attribute__((vector_size(16))); };
typedef double dv __attribute__((vector_size(4 * sizeof(double))));
int a1[sizeof(long) * 2 - 1u];
