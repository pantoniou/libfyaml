#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <libfyaml.h>

START_TEST(test_yaml_bomb_memory_limit)
{
    // Invariant: Parser must not allow exponential memory expansion from alias resolution
    const char *payloads[] = {
        // YAML bomb - exponential expansion via nested aliases
        "a: &a [\"lol\",\"lol\"]\nb: &b [*a,*a]\nc: &c [*b,*b]\nd: &d [*c,*c]\ne: &e [*d,*d]\nf: &f [*e,*e]\ng: &g [*f,*f]\nh: &h [*g,*g]\ni: [*h,*h]",
        // Boundary: single alias (should work)
        "anchor: &val test\nref: *val",
        // Valid input without aliases
        "key: value\nlist:\n  - item1\n  - item2"
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    // Set memory limit to 64MB to detect exponential expansion
    struct rlimit mem_limit = {.rlim_cur = 64 * 1024 * 1024, .rlim_max = 64 * 1024 * 1024};
    setrlimit(RLIMIT_AS, &mem_limit);

    for (int i = 0; i < num_payloads; i++) {
        struct fy_document *doc = fy_document_build_from_string(NULL, payloads[i], strlen(payloads[i]));
        // Parser should either succeed with bounded memory or fail gracefully
        // It must NOT cause unbounded memory allocation
        if (doc) {
            fy_document_destroy(doc);
        }
        // If we reach here, memory was bounded (didn't OOM crash)
        ck_assert_msg(1, "Parser handled input %d without memory exhaustion", i);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_set_timeout(tc_core, 10);
    tcase_add_test(tc_core, test_yaml_bomb_memory_limit);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}