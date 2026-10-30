// SPDX-License-Identifier: MIT

/*
 * Cases that real headers hold: an empty record with a flexible array, and
 * enumerators with values of an unsigned type. The file edge.expected holds the
 * reflection on a 64 bit host.
 */
typedef unsigned int u32;
typedef unsigned long long u64;

/* the Linux flexible array in a union */
struct flex_holder {
	u32 count;
	union {
		u32 fixed[1];
		struct {
			struct { } empty;
			u32 items[];
		};
	};
};

enum callchain {
	CALLCHAIN_HV = (u64)-32,
	CALLCHAIN_KERNEL = (u64)-128,
	CALLCHAIN_USER = (u64)-512
};

enum mask {
	MASK_HIGH = (0xffffffffULL << 32),
	MASK_LOW = 1
};
