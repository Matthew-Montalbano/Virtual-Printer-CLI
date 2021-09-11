#include <criterion/criterion.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define ERR_RETURN_SUCCESS "Program exited with %d instead of EXIT_SUCCESS"

static void stop_printers(void) {
    system("make stop_printers");
    system("killall util/printer");
}

static void create_dirs(void) {
    system("mkdir -p spool test_output");
}

static void setup_test(void) {
    stop_printers();
    create_dirs();
}

static void assert_correct_header(char *file, char **path, int n) {
    FILE *str = fopen(file, "r");
    if(str == NULL)
	cr_assert_fail("Can't open header file: %s", file);
    char buf[512];
    int pgid = 0;
    for(int i = 0; i < n-1; i++) {
	fgets(buf, sizeof(buf), str);
	char f[512], t[512];
	int p, g;
	int n;
	n = sscanf(buf, "convert %s %s %d %d", f, t, &p, &g);
	cr_assert_eq(n, 4, "Corrupted conversion header: %s", buf);
	cr_assert_eq(strcmp(f, path[i]), 0,
		     "Conversion header had wrong 'from' type (exp %s, was %s)", path[i], f);
	cr_assert_eq(strcmp(t, path[i+1]), 0,
		     "Conversion header had wrong 'to' type (exp %s, was %s)", path[i+1], t);
	fprintf(stdout, "pid: %d, pgid: %d\n", p, g);
	cr_assert_neq(g, p, "Conversion in pipeline was in its own process group (pgid: %d, pid: %d)", g, p);
	if(i > 0)
	    cr_assert_eq(g, pgid, "Conversions in pipeline weren't in same process group (%d != %d)", g, pgid);
	pgid = g;
    }
    fclose(str);
}

Test(basecode_suite, empty_script_test, .init = setup_test, .timeout=20) {
    char *cmd = "bin/imprimer -i /dev/null -o test_output/empty_script_test.out < /dev/null";
    int ret = system(cmd);
    cr_assert_eq(ret, 0, "Program did not exit normally (status 0x%x)", ret);
}

Test(basecode_suite, nonempty_script_test, .init = setup_test, .timeout=20) {
    char *cmd = "bin/imprimer -i test_scripts/help_test.imp -o test_output/nonempty_script_test.out < /dev/null";
    int ret = system(cmd);
    cr_assert_eq(ret, 0, "Program did not exit normally (status 0x%x)", ret);
    ret = system("[ -s test_output/nonempty_script_test.out ]");
    cr_assert_eq(ret, 0, "No output was produced", ret);
}

Test(basecode_suite, type_test, .init = setup_test, .timeout=20) {
    char *cmd = "bin/imprimer < test_scripts/type_test.imp -o test_output/type_test.out";
    int ret = system(cmd);
    cr_assert_eq(ret & 0xff00, 0, "Program failed/crashed (status 0x%x)", ret);
}

Test(basecode_suite, conversion_test, .init = setup_test, .timeout=20) {
    char *cmd = "bin/imprimer < test_scripts/conversion_test.imp -o test_output/conversion_test.out";
    int ret = system(cmd);
    cr_assert_eq(ret & 0xff00, 0, "Program failed/crashed (status 0x%x)", ret);
}

// One file, one printer for the same type.
Test(basecode_suite, print_test, .init = setup_test, .fini = stop_printers, .timeout=20) {
    char *cmd = "rm -f spool/Alice*; bin/imprimer < test_scripts/print_test.imp -o test_output/print_test.out";
    int ret = system(cmd);
    sleep(10); // Allow "printing" time to occur
    ret = system("ls spool | grep -q 'Alice_aaa_[0-9]*\\.[0-9]*'");
    cr_assert_eq(ret, 0, "There was no output from printer Alice");
    // Check that the correct data was "printed".
    ret = system("cmp -s spool/Alice_aaa_*.* test_scripts/testfile.aaa");
    cr_assert_eq(ret, 0, "The output from printer Alice was incorrect (cmp exit status %d)", ret);
}

// One printer, one file of a different type, one conversion.
Test(basecode_suite, convert_test, .init = setup_test, .fini = stop_printers, .timeout=30) {
    char *cmd = "rm -f spool/Bob*; bin/imprimer < test_scripts/convert_test.imp -o test_output/convert_test.out";
    int ret = system(cmd);
    sleep(10); // Allow "printing" time to occur
    ret = system("ls spool | grep -q 'Bob_aaa_[0-9]*\\.[0-9]*'");
    cr_assert_eq(ret, 0, "There was no output from printer Bob");
    // Check that the correct data was "printed".
    ret = system("tail -n +2 spool/Bob_aaa_*.* > test_output/convert_test.cnt");
    ret = system("cmp -s test_output/convert_test.cnt test_scripts/testfile.ccc");
    // Check that the correct conversion was performed, with sensible process groups.
    cr_assert_eq(ret, 0, "The output from printer Bob was incorrect (cmp exit status %d)", ret);
    ret = system("head -1 spool/Bob_aaa_*.* > test_output/convert_test.hdr");
    (void)ret;
    char *path[] = {"ccc", "aaa"};
    assert_correct_header("test_output/convert_test.hdr", path, sizeof(path)/sizeof(*path));
}
