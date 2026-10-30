// SPDX-License-Identifier: MIT

/* what plain char is on the target decides these values */
typedef char t_char;

enum char_values {
	CV_NEG = (char)-1,
	CV_HIGH = (char)200,
	CV_SHIFT = (char)0x80 >> 1
};

enum char_cmp {
	CC_SIGNED = ((char)-1 < 0),
	CC_SIZE = sizeof(char)
};

enum wide_char_values {
	WV_CONV = (unsigned char)(char)-2
};
