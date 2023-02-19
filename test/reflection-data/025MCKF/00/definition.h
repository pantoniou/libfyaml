enum e {
	e_one,
	e_two,
	e_three,
	e_minus = 0xff00ff00ff00FFLL,
};

struct foo {
	enum e a;
	enum e b : 2;
};
