#if 1
// yaml:
//   default:
//     val: -1
//     text: minus-one
#endif
struct foo {
#if 1
	int val;	// yaml: { default: 101 }
	char *text;	// yaml: { default: hundred-and-one }
#endif
#if 1
	// yaml: { default: { one: 1, two: 2 } }
#else
	// yaml: { default: { one: 1 } }
#endif
	struct {
		int one;	// yaml: { default: 11 }
#if 1
		int two;	// yaml: { default: 22 }
#endif
	} onetwo;
#if 1
	int bitfield : 3;	// yaml: { default: 3 }
#endif
};
