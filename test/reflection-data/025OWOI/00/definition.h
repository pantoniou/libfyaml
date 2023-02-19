struct bar {
	// yaml: { remove-prefix: camr_ }
	enum {
		camr_user,
		camr_assistant
	} role;
	char *content;
};

struct foo {
	struct bar **messages;	/* required */
};
