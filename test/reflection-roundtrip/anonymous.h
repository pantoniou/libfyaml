// SPDX-License-Identifier: MIT

enum {
	FIRST = 1,
	SECOND = 2
};

typedef struct {
	int count;
	union {
		int word;
		char bytes[4];
	} value;
} state_t, *state_ptr_t;

typedef state_t state_alias_t;

struct user {
	state_t state;
	state_alias_t other;
	state_ptr_t ptr;
	enum {
		MODE_A,
		MODE_B
	} mode;
	struct {
		struct {
			int x;
		} deep;
		int y;
	} nested;
};
