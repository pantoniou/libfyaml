// SPDX-License-Identifier: MIT

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;

struct __attribute__((packed)) wire {
	u8 tag;
	u32 length;
	u64 value;
};

struct __attribute__((aligned(16))) vec {
	float v[3];
};

struct stats {
	u64 first __attribute__((aligned(8)));
	u8 flag;
	u32 second __attribute__((aligned(8)));
	u8 tail;
};

union __attribute__((packed)) overlay {
	u8 bytes[5];
	u32 word;
};
