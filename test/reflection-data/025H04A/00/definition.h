enum e {
	e_one,
	e_two,
	e_final
};

struct bar {
	enum e *enums;	// yaml: { terminator: e_final }
};
