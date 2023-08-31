/*
 * fy-b3sum.c - blake3 utility for testing within fyaml
 *
 * Copyright (c) 2023 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <alloca.h>
#include <stdbool.h>
#include <getopt.h>
#include <ctype.h>
#include <assert.h>

#include <stdatomic.h>
#include <blake3.h>

#define OPT_NO_MMAP		128
#define OPT_NO_MTHREAD		129
#define OPT_NUM_THREADS		130
#define OPT_BUFFER_SIZE		131
#define OPT_MT_DEGREE		132
#define OPT_LIST_BACKENDS	133
#define OPT_ENABLE_CPUSIMD	134
#define OPT_CPUSIMD_NUM_CPUS	135
#define OPT_CPUSIMD_MULT_FACT	136
#define OPT_DERIVE_KEY		137
#define OPT_NO_NAMES		138
#define OPT_RAW			139
#define OPT_QUIET		140
#define OPT_KEYED		141

static struct option lopts[] = {
	{"check",		no_argument,		0,	'c' },
	{"derive-key",		required_argument,	0,	OPT_DERIVE_KEY },
	{"no-names",		no_argument,		0,	OPT_NO_NAMES },
	{"raw",			no_argument,		0,	OPT_RAW },
	{"length",		required_argument,	0,	'l' },
	{"quiet",		no_argument,		0,	OPT_QUIET },
	{"keyed",		no_argument,		0,	OPT_KEYED },

	{"num-threads",		required_argument,	0,	OPT_NUM_THREADS },
	{"no-mmap",		no_argument,		0,	OPT_NO_MMAP },
	{"no-mthread",		no_argument,		0,	OPT_NO_MTHREAD },
	{"buffer-size",		required_argument,	0,	OPT_BUFFER_SIZE },
	{"mt-degree",		required_argument,	0,	OPT_MT_DEGREE },
	{"backend",		required_argument,	0,	'b' },
	{"enable-cpusimd",	no_argument,		0,	OPT_ENABLE_CPUSIMD },
	{"cpusimd-num-cpus",	required_argument,	0,	OPT_CPUSIMD_NUM_CPUS },
	{"cpusimd-mult-fact",	required_argument,	0,	OPT_CPUSIMD_MULT_FACT },

	{"list-backends",	no_argument,		0,	OPT_LIST_BACKENDS },
	{"help",		no_argument,		0,	'h' },
	{"debug",		no_argument,		0,	'd' },
	{0,			0,              	0,	 0  },
};

static void display_usage(FILE *fp, const char *progname)
{
	const char *s;

	s = strrchr(progname, '/');
	if (s != NULL)
		progname = s + 1;

	fprintf(fp, "Usage:\n\t%s [options] [args]\n", progname);
	fprintf(fp, "\noptions:\n");
	fprintf(fp, "\t--derive-key <context>    : Key derivation mode, with the given context string\n");
	fprintf(fp, "\t--no-names                : Omit filenames\n");
	fprintf(fp, "\t--raw                     : Output result in raw bytes (single input allowed)\n");
	fprintf(fp, "\t--length <n>, -l <n>      : Output only this amount of bytes per output (max %u)\n", BLAKE3_OUT_LEN);
	fprintf(fp, "\t--check, -c               : Read files with BLAKE3 checksums and check files\n");
	fprintf(fp, "\t--quiet                   : Do not print OK for checked files that are correct\n");
	fprintf(fp, "\t--keyed                   : Keyed mode with secret key read from <stdin> (32 raw bytes)\n");
	fprintf(fp, "\ntuning options:\n");
	fprintf(fp, "\t--num-threads <n>         : Number of threads to use (default: number of CPUs * 3 / 2)\n");
	fprintf(fp, "\t--no-mmap                 : Disable file mmap\n");
	fprintf(fp, "\t--no-mthread              : Disable multithreading\n");
	fprintf(fp, "\t--buffer-size <n>         : Buffer size for file I/O\n");
	fprintf(fp, "\t--mt-degree <n>           : Set the multi-thread degree (default 128)\n");
	fprintf(fp, "\t--backend <arg>, -b <arg> : Backend selection\n");
	fprintf(fp, "\t--enable-cpusimd          : Enable experimental CPUSIMD support\n");
	fprintf(fp, "\t--cpusimd-num-cpus        : Number of CPUs assigned to CPUSIMD\n");
	fprintf(fp, "\t--cpusimd-mult-fact       : Multiplication factor for CPUSIMD\n");
	fprintf(fp, "\ninformational options:\n");
	fprintf(fp, "\t--list-backends           : List available backends\n");
	fprintf(fp, "\t--debug                   : Enable debug messages\n");
	fprintf(fp, "\t--help, -h                : Display help message\n");
	fprintf(fp, "\n");
	fprintf(fp, "\nargs:\n");
	fprintf(fp, "\t<file>...  Files to hash or checkfiles to check.\n\tIf no file given (or file is '-' hash stdin\n");
}

static void list_backends(const char *name)
{
	const blake3_backend_info *bei;
	uint64_t backends;
	bool selected;
	unsigned int i;

	backends = blake3_get_selectable_backends() &
		   blake3_get_detected_backends();
	for (i = 0; backends && i < B3BID_COUNT; i++) {
		if (!(backends & ((uint64_t)1 << i)))
			continue;
		bei = blake3_get_backend_info(i);
		if (!bei)
			continue;
		backends &= ~((uint64_t)1 << i);

		if (!name)
			selected = backends == 0;
		else
			selected = !strcmp(name, bei->name);

		printf("%c %-12s\t%s\n",
				selected ? '*' : ' ', bei->name, bei->description);
	}
}

static int do_hash_file(struct blake3_hasher *hasher, const char *filename, bool no_names, bool raw, unsigned int length)
{
	static const char *hexb = "0123456789abcdef";
	uint8_t output[BLAKE3_OUT_LEN];
	size_t filename_sz, line_sz, outsz;
	ssize_t wrn;
	uint8_t v;
	void *outp;
	char *line, *s;
	unsigned int i;
	int rc;

	filename_sz = strlen(filename);

	rc = blake3_hash_file(hasher, filename, output);
	if (rc) {
		fprintf(stderr, "Failed to hash file: \"%s\", error: %s\n", filename, strerror(errno));
		return -1;
	}

	if (!raw) {
		/* output line (optimized) */
		line_sz = (length * 2);		/* the hex output */
		if (!no_names)
			line_sz += 2 + filename_sz;	/* 2 spaces + filename */
		line_sz++;	/* '\n' */
		line = alloca(line_sz + 1);
		s = line;
		for (i = 0; i < length; i++) {
			v = output[i];
			*s++ = hexb[v >> 4];
			*s++ = hexb[v & 15];
		}
		if (!no_names) {
			*s++ = ' ';
			*s++ = ' ';
			memcpy(s, filename, filename_sz);
			s += filename_sz;
		}
		*s++ = '\n';
		*s = '\0';
		outp = line;
		outsz = (size_t)(s - line);
	} else {
		outp = output;
		outsz = length;
	}

	wrn = fwrite(outp, 1, outsz, stdout);
	if ((size_t)wrn != outsz) {
		fprintf(stderr, "Unable to write to stdout! error: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static int do_check_file(struct blake3_hasher *hasher, const char *check_filename, bool quiet)
{
	char *hash, *filename;
	FILE *fp = NULL;
	char linebuf[8192];	/* maximum size for a line is 8K, should be enough (PATH_MAX is 4K at linux) */
	uint8_t read_hash[BLAKE3_OUT_LEN];
	uint8_t computed_hash[BLAKE3_OUT_LEN];
	uint8_t v;
	char *s;
	char c;
	unsigned int i, j, length;
	size_t linesz;
	int line, rc, exit_code;

	if (check_filename && strcmp(check_filename, "-")) {
		fp = fopen(check_filename, "ra");
		if (!fp) {
			fprintf(stderr, "Failed to open check file: \"%s\", error: %s\n", check_filename, strerror(errno));
			goto err_out;
		}
	} else {
		fp = stdin;
	}

	/* default error code if all is fine */
	exit_code = 0;

	line = 0;
	while (fgets(linebuf, sizeof(linebuf), fp)) {
		/* '\0' terminate always */
		linebuf[(sizeof(linebuf)/sizeof(linebuf[0]))-1] = '\0';

		linesz = strlen(linebuf);
		while (linesz > 0 && linebuf[linesz-1] == '\n')
			linesz--;

		if (!linesz) {
			fprintf(stderr, "Empty line found at file \"%s\" line #%d\n", check_filename, line);
			goto err_out;
		}
		linebuf[linesz] = '\0';

		length = 0;
		s = linebuf;
		while (isxdigit(*s))
			s++;

		length = s - linebuf;

		if (length == 0 || length > (BLAKE3_OUT_LEN * 2) || (length % 1) || !isspace(*s)) {
			fprintf(stderr, "Bad line found at file \"%s\" line #%d\n", check_filename, line);
			fprintf(stderr, "%s\n", linebuf);
			goto err_out;
		}

		*s++ = '\0';

		while (isspace(*s))
			s++;

		length >>= 1;	/* to bytes */
		hash = linebuf;
		filename = s;

		for (i = 0, s = hash; i < length; i++) {
			v = 0;
			for (j = 0; j < 2; j++) {
				v <<= 4;
				c = *s++;
				if (c >= '0' && c <= '9')
					v |= c - '0';
				else if (c >= 'a' && c <= 'f')
					v |= c - 'a' + 10;
				else if (c >= 'A' && c <= 'F')
					v |= c - 'A' + 10;
				else
					v = 0;
			}
			read_hash[i] = v;
		}

		rc = blake3_hash_file(hasher, filename, computed_hash);
		if (rc) {
			fprintf(stderr, "Failed to hash file: \"%s\", error: %s\n", filename, strerror(errno));
			goto err_out;
		}

		/* constant time comparison */
		v = 0;
		for (i = 0; i < length; i++)
			v |= (read_hash[i] ^ computed_hash[i]);

		if (v) {
			printf("%s: FAILED\n", filename);
			exit_code = -1;
		} else if (!quiet)
			printf("%s: OK\n", filename);
	}

out:
	if (fp && fp != stdin)
		fclose(fp);

	return exit_code;

err_out:
	exit_code = -1;
	goto out;
}

int main(int argc, char *argv[])
{
	blake3_host_config host_cfg;
	struct blake3_host_state *host_state = NULL;
	struct blake3_hasher *hasher = NULL;
	const char *filename;
	int i, opt, lidx, rc, num_inputs, num_ok;
	int exitcode = EXIT_FAILURE, opti;
	size_t buffer_size = 0;
	bool no_names = false, raw = false, quiet = false, keyed = false;
	bool no_mmap = false, no_mthread = false, debug = false, enable_cpusimd = false;
	bool do_list_backends = false, check = false, derive_key = false;
	unsigned int mt_degree = 0, num_threads = 0, cpusimd_num_cpus = 0, cpusimd_mult_fact = 0, length = BLAKE3_OUT_LEN;
	const char *backend = NULL, *context = NULL;
	uint8_t key[BLAKE3_OUT_LEN];
	ssize_t rdn;

	while ((opt = getopt_long_only(argc, argv, "cl:b:dh", lopts, &lidx)) != -1) {
		switch (opt) {

		case OPT_DERIVE_KEY:
			derive_key = true;
			context = optarg;
			break;
		case OPT_NO_NAMES:
			no_names = true;
			break;
		case OPT_RAW:
			raw = true;
			break;
		case 'c':
			check = true;
			break;
		case OPT_QUIET:
			quiet = true;
			break;
		case OPT_KEYED:
			keyed = true;
			break;

		case 'l':
			opti = atoi(optarg);;
			if (opti <= 0 || opti > BLAKE3_OUT_LEN) {
				fprintf(stderr, "Error: bad length=%d (must be > 0 and <= %u)\n\n", opti, BLAKE3_OUT_LEN);
				goto err_out_usage;
			}
			length = (unsigned int)opti;
			break;

		case OPT_NO_MMAP:
			no_mmap = true;
			break;
		case OPT_NO_MTHREAD:
			no_mthread = true;
			break;
		case OPT_NUM_THREADS:
			opti = atoi(optarg);
			if (opti < 0) {
				fprintf(stderr, "Error: bad num_threads=%d (must be >= 0)\n\n", opti);
				goto err_out_usage;
			}
			num_threads = (unsigned int)opti;
			break;
		case OPT_BUFFER_SIZE:
			opti = atoi(optarg);
			if (opti <= 0) {
				fprintf(stderr, "Error: bad buffer-size=%d (must be > 0)\n\n", opti);
				goto err_out_usage;
			}
			buffer_size = (unsigned int)opti;
			break;
		case OPT_MT_DEGREE:
			opti = atoi(optarg);
			if (opti < 0) {
				fprintf(stderr, "Error: bad mt_degree=%d (must be >= 0)\n\n", opti);
				goto err_out_usage;
			}
			mt_degree = (unsigned int)opti;
			break;

		case OPT_LIST_BACKENDS:
			do_list_backends = true;
			break;

		case OPT_ENABLE_CPUSIMD:
			enable_cpusimd = true;
			break;
		case OPT_CPUSIMD_NUM_CPUS:
			opti = atoi(optarg);
			if (opti < 0) {
				fprintf(stderr, "Error: bad cpusimd_num_cpus=%d (must be >= 0)\n\n", opti);
				goto err_out_usage;
			}
			cpusimd_num_cpus = (unsigned int)opti;
			break;
		case OPT_CPUSIMD_MULT_FACT:
			opti = atoi(optarg);
			if (opti < 0) {
				fprintf(stderr, "Error: bad cpusimd_mult_fact=%d (must be >= 0)\n\n", opti);
				goto err_out_usage;
			}
			cpusimd_mult_fact = (unsigned int)opti;
			break;

		case 'b':
			backend = optarg;
			break;

		case 'd':
			debug = true;
			break;
		case 'h' :
			display_usage(stdout, argv[0]);
			goto ok_out;
		default:
			goto err_out_usage;
		}
	}

	if (enable_cpusimd) {
		rc = blake3_backend_cpusimd_setup(cpusimd_num_cpus, cpusimd_mult_fact);
		if (rc) {
			fprintf(stderr, "Unable to enable CPUSIMD\n");
			goto err_out;
		}
	}

	if (do_list_backends) {
		list_backends(backend);
		goto ok_out;
	}

	if (quiet && !check) {
		fprintf(stderr, "Error: --quiet may only be used together with --check\n\n");
		goto err_out_usage;
	}

	if (keyed && derive_key) {
		fprintf(stderr, "Error: --keyed and --derive-key may not be used together\n\n");
		goto err_out_usage;
	}

	if (check && length != BLAKE3_OUT_LEN) {
		fprintf(stderr, "Error: --check and --length may not be used together\n\n");
		goto err_out_usage;
	}

	if (keyed) {
		rdn = fread(key, 1, BLAKE3_KEY_LEN, stdin);
		if (rdn != BLAKE3_KEY_LEN) {
			if (rdn >= 0 && rdn < BLAKE3_KEY_LEN)
				fprintf(stderr, "Error: could not read secret key from <stdin>: short key\n\n");
			else
				fprintf(stderr, "Error: could not read secret key from <stdin>: error %s\n\n", strerror(errno));
			goto err_out_usage;
		}
		rc = fgetc(stdin);
		if (rc != EOF) {
			fprintf(stderr, "Error: garbage trailing secret key from <stdin>\n\n");
			goto err_out_usage;
		}
	}

	memset(&host_cfg, 0, sizeof(host_cfg));
	host_cfg.debug = debug;
	host_cfg.no_mthread = no_mthread;
	host_cfg.no_mmap = no_mmap;
	host_cfg.num_threads = num_threads;
	host_cfg.backend = backend;
	host_cfg.mt_degree = mt_degree;
	host_cfg.file_io_bufsz = buffer_size;

	host_state = blake3_host_state_create(&host_cfg);
	if (!host_state) {
		fprintf(stderr, "unable to create blake3 host state\n");
		goto err_out;
	}

	hasher = blake3_hasher_create(host_state,
			keyed ? key : NULL,
			derive_key ? context : NULL, 0);
	if (!hasher) {
		fprintf(stderr, "unable to create blake3 hasher\n");
		goto err_out;
	}

	num_inputs = argc - optind;
	if (num_inputs <= 0)
		num_inputs = 1;	/* stdin mode */

	if (raw && num_inputs > 1) {
		fprintf(stderr, "Error: Raw output mode is only supported with a single input\n\n");
		goto err_out_usage;
	}

	if (!length)
		length = BLAKE3_OUT_LEN;

	/* we will get in the loop even when no arguments (we'll do stdin instead) */
	num_ok = 0;
	i = optind;
	do {
		/* if no arguments, use stdin */
		filename = i < argc ? argv[i] : "-";

		/* we can't handle '-' in keyed mode */
		if (keyed && !strcmp(filename, "-")) {
			fprintf(stderr, "Cannot use <stdin> in keyed mode\n");
			goto err_out_usage;
		}

		if (!check)
			rc = do_hash_file(hasher, filename, no_names, raw, length);
		else
			rc = do_check_file(hasher, filename, quiet);
		if (!rc)
			num_ok++;

	} while (++i < argc);

	if (num_inputs != num_ok)
		goto err_out;

ok_out:
	exitcode = EXIT_SUCCESS;

out:
	if (hasher)
		blake3_hasher_destroy(hasher);

	if (host_state)
		blake3_host_state_destroy(host_state);

	if (enable_cpusimd)
		blake3_backend_cpusimd_cleanup();

	return exitcode;

err_out_usage:
	display_usage(stderr, argv[0]);
err_out:
	exitcode = EXIT_FAILURE;
	goto out;
}
