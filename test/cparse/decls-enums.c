// SPDX-License-Identifier: MIT

/* the underlying type of an enumeration without a fixed type */
typedef unsigned long long u64;
typedef unsigned int u32;

enum small { S_A, S_B = 7 };
enum negative { N_A = -1, N_B = 2 };
enum big_signed { BS_A = 1, BS_B = 0x100000000LL };
enum unsigned_cast { UC_A = (u64)-32, UC_B = (u64)-128 };
enum unsigned_shift { US_MASK = (0xffffffffULL << 32), US_LOW = 1 };
enum unsigned_max { UM_A = 0xffffffffffffffffULL };
enum unsigned_int { UI_A = 0x80000000u, UI_B = (u32)-1 };
enum implicit_after_unsigned { IU_A = (u64)-2, IU_B, IU_C };
