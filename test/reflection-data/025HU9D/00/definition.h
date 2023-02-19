struct foo {
	int a0;	// yaml: { default: 200 }
	const volatile int a;
	int b;	// yaml: { default: 101 }
	int bb;	// yaml: { default: 101 }
	short c : 8;	// yaml: { default: -1 }
};
