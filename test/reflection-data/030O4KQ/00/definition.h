struct ca_cache_control {
	// yaml: { remove-prefix: cacct_ }
	enum {
		cacct_ephemeral
	} type;
};

// yaml: { remove-prefix: cact_ }
enum ca_content_type {
	cact_text,
	cact_image,
	cact_tool_use,
	cact_tool_result,
	cact_document,
};

struct ca_content {
	enum ca_content_type type;
	char *text;
	struct ca_cache_control *cache_control;
};

// yaml: { remove-prefix: camr_ }
enum ca_role {
	camr_user,
	camr_assistant
};

struct ca_message {
	enum ca_role role;
	// yaml: { remove-prefix: camcf_ }
	enum {
		camcf_string,
		camcf_contents,
	} content_form;
	// yaml: { field-auto-select: true }
	union {
		char *string;
		struct ca_content **contents;
	} content;
};

struct foo {
	struct ca_message **messages;	/* required */
};
