// SPDX-License-Identifier: MIT

typedef struct loop_s loop_t;
typedef struct handle_s handle_t;
typedef void (*close_cb_t)(handle_t *);
typedef void (*walk_cb_t)(struct handle_s *, void *);

struct loop_s {
	handle_t *closing;
	union {
		void *unused;
		unsigned int count;
	} active;
};

struct handle_s {
	loop_t *loop;
	close_cb_t close_cb;
	walk_cb_t walk_cb;
	union {
		int fd;
		void *reserved[4];
	} u;
	handle_t *next;
};

struct node {
	struct node *next;
	struct node *children[2];
};
