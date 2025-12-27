/*
 * test-short-names.c - Verify FY_CPP_SHORT_NAMES compatibility
 *
 * This test verifies that both long and short macro name implementations
 * produce identical results.
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "../src/lib/fy-parse.h"
#include "../src/generic/fy-generic.h"
#include "../src/generic/fy-generic-decoder.h"
#include "../src/generic/fy-generic-encoder.h"

int main(void) {
	fy_generic seq, map;
	struct fy_generic_builder *gb;
	char buf[4096];
	const char *s;

	printf("Testing FY_CPP_SHORT_NAMES mode compatibility\n");
#ifdef FY_CPP_SHORT_NAMES
	printf("Mode: SHORT_NAMES (enabled)\n");
#else
	printf("Mode: LONG_NAMES (default)\n");
#endif
	printf("\n");

	/* Test 1: Basic local sequence creation */
	seq = fy_local_sequence(1, 2, 3, 4, 5);
	assert(fy_generic_is_sequence(seq));
	assert(fy_len(seq) == 5);
	assert(fy_get(seq, 0, -1) == 1);
	assert(fy_get(seq, 4, -1) == 5);
	printf("✓ Test 1: Basic local sequence creation\n");

	/* Test 2: Basic local mapping creation */
	map = fy_local_mapping("foo", 100, "bar", 200, "baz", 300);
	assert(fy_generic_is_mapping(map));
	assert(fy_len(map) == 3);
	assert(fy_get(map, "foo", 0) == 100);
	assert(fy_get(map, "bar", 0) == 200);
	assert(fy_get(map, "baz", 0) == 300);
	printf("✓ Test 2: Basic local mapping creation\n");

	/* Test 3: Builder-based sequence */
	gb = fy_generic_builder_create_in_place(
		FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER,
		NULL, buf, sizeof(buf));
	assert(gb != NULL);

	seq = fy_gb_sequence(gb, 10, 20, 30, 40, 50);
	assert(fy_generic_is_sequence(seq));
	assert(fy_len(seq) == 5);
	assert(fy_get(seq, 0, -1) == 10);
	assert(fy_get(seq, 4, -1) == 50);
	printf("✓ Test 3: Builder-based sequence\n");

	/* Test 4: Builder-based mapping */
	map = fy_gb_mapping(gb, "key1", "val1", "key2", "val2");
	assert(fy_generic_is_mapping(map));
	assert(fy_len(map) == 2);
	s = fy_get(map, "key1", "");
	assert(strcmp(s, "val1") == 0);
	s = fy_get(map, "key2", "");
	assert(strcmp(s, "val2") == 0);
	printf("✓ Test 4: Builder-based mapping\n");

	/* Test 5: Nested local structures */
	seq = fy_local_sequence(
		fy_local_mapping("a", 1, "b", 2),
		fy_local_sequence(10, 20, 30),
		fy_local_string("test")
	);
	assert(fy_generic_is_sequence(seq));
	assert(fy_len(seq) == 3);
	assert(fy_generic_is_mapping(fy_get(seq, 0, fy_null)));
	assert(fy_generic_is_sequence(fy_get(seq, 1, fy_null)));
	assert(fy_generic_is_string(fy_get(seq, 2, fy_null)));
	printf("✓ Test 5: Nested local structures\n");

	/* Test 6: Large local sequence (stress test for expansion) */
	seq = fy_local_sequence(
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
		11, 12, 13, 14, 15, 16, 17, 18, 19, 20
	);
	assert(fy_generic_is_sequence(seq));
	assert(fy_len(seq) == 20);
	assert(fy_get(seq, 0, -1) == 1);
	assert(fy_get(seq, 19, -1) == 20);
	printf("✓ Test 6: Large local sequence (20 items)\n");

	/* Test 7: Large local mapping (stress test for expansion) */
	map = fy_local_mapping(
		"k1", 1, "k2", 2, "k3", 3, "k4", 4, "k5", 5,
		"k6", 6, "k7", 7, "k8", 8, "k9", 9, "k10", 10
	);
	assert(fy_generic_is_mapping(map));
	assert(fy_len(map) == 10);
	assert(fy_get(map, "k1", 0) == 1);
	assert(fy_get(map, "k10", 0) == 10);
	printf("✓ Test 7: Large local mapping (10 pairs)\n");

	/* Test 8: Large builder sequence */
	seq = fy_gb_sequence(gb,
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
		11, 12, 13, 14, 15, 16, 17, 18, 19, 20
	);
	assert(fy_generic_is_sequence(seq));
	assert(fy_len(seq) == 20);
	assert(fy_get(seq, 0, -1) == 1);
	assert(fy_get(seq, 19, -1) == 20);
	printf("✓ Test 8: Large builder sequence (20 items)\n");

	fy_generic_builder_destroy(gb);

	printf("\nAll tests passed!\n");
	return 0;
}
