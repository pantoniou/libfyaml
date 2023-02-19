struct foo {
	char *text;	// yaml: { default: default-text }
	int value;	// yaml: { default: 10, omit-if-default: true }
};
