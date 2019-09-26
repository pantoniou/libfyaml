/*
 * fy-parse.c - Internal parse interface
 *
 * Copyright (c) 2019 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <limits.h>

#include <libfyaml.h>

#include "fy-parse.h"

#include "fy-utils.h"

#define ATOM_SIZE_CHECK

const char *fy_library_version(void)
{
#ifndef VERSION
#warn No version defined
	return "UNKNOWN";
#else
	return VERSION;
#endif
}

int fy_parse_input_open(struct fy_parser *fyp, struct fy_input *fyi);
void fy_input_close(struct fy_input *fyi);

#define FY_DEFAULT_YAML_VERSION_MAJOR	1
#define FY_DEFAULT_YAML_VERSION_MINOR	1

const void *fy_ptr_slow_path(struct fy_parser *fyp, size_t *leftp)
{
	struct fy_input *fyi;
	const void *p;
	int left;

	if (fyp->current_ptr) {
		if (leftp)
			*leftp = fyp->current_left;
		return fyp->current_ptr;
	}

	fyi = fyp->current_input;
	if (!fyi)
		return NULL;

	/* tokens cannot cross boundaries */
	switch (fyi->cfg.type) {
	case fyit_file:
		if (fyi->file.addr) {
			left = fyi->file.length - fyp->current_input_pos;
			p = fyi->file.addr + fyp->current_input_pos;
			break;
		}

		/* fall-through */

	case fyit_stream:
		left = fyi->read - fyp->current_input_pos;
		p = fyi->buffer + fyp->current_input_pos;
		break;

	case fyit_memory:
		left = fyi->cfg.memory.size - fyp->current_input_pos;
		p = fyi->cfg.memory.data + fyp->current_input_pos;
		break;

	default:
		assert(0);	/* no streams */
		p = NULL;
		left = 0;
		break;
	}

	if (leftp)
		*leftp = left;

	fyp->current_ptr = p;
	fyp->current_left = left;
	fyp->current_c = fy_utf8_get(fyp->current_ptr, fyp->current_left, &fyp->current_w);

	return p;
}

bool fy_parse_have_more_inputs(struct fy_parser *fyp)
{
	return !fy_input_list_empty(&fyp->queued_inputs);
}

int fy_parse_get_next_input(struct fy_parser *fyp)
{
	struct fy_input *fyi;
	int rc;

	if (fyp->current_input) {
		fy_scan_debug(fyp, "get next input: already exists");
		return 1;
	}

	/* get next queued input */
	fyi = fy_input_list_pop(&fyp->queued_inputs);

	/* none left? we're done */
	if (!fyi) {
		fy_scan_debug(fyp, "get next input: all inputs exhausted");
		return 0;
	}
	fyi->on_list = NULL;

	rc = fy_parse_input_open(fyp, fyi);
	fy_error_check(fyp, !rc, err_out,
			"failed to open input");

	/* initialize start of input */
	fyp->current_input = fyi;
	fyp->current_input_pos = 0;
	fyp->current_c = -1;
	fyp->current_w = 0;
	fyp->line = 0;
	fyp->column = 0;

	fy_scan_debug(fyp, "get next input: new input");

	return 1;

err_out:
	return -1;
}

static const struct fy_tag * const fy_default_tags[] = {
	&(struct fy_tag) { .handle = "!", .prefix = "!", },
	&(struct fy_tag) { .handle = "!!", .prefix = "tag:yaml.org,2002:", },
	&(struct fy_tag) { .handle = "", .prefix = "", },
	NULL
};

bool fy_tag_handle_is_default(const char *handle, size_t handle_size)
{
	int i;
	const struct fy_tag *fytag;

	if (handle_size == (size_t)-1)
		handle_size = strlen(handle);

	for (i = 0; (fytag = fy_default_tags[i]) != NULL; i++) {

		if (handle_size == strlen(fytag->handle) &&
		    !memcmp(handle, fytag->handle, handle_size))
			return true;

	}
	return false;
}

bool fy_tag_is_default(const char *handle, size_t handle_size,
		       const char *prefix, size_t prefix_size)
{
	int i;
	const struct fy_tag *fytag;

	if (handle_size == (size_t)-1)
		handle_size = strlen(handle);

	if (prefix_size == (size_t)-1)
		prefix_size = strlen(prefix);

	for (i = 0; (fytag = fy_default_tags[i]) != NULL; i++) {

		if (handle_size == strlen(fytag->handle) &&
		    !memcmp(handle, fytag->handle, handle_size) &&
		    prefix_size == strlen(fytag->prefix) &&
		    !memcmp(prefix, fytag->prefix, prefix_size))
			return true;

	}
	return false;
}

bool fy_token_tag_directive_is_overridable(struct fy_token *fyt_td)
{
	const struct fy_tag *fytag;
	const char *handle, *prefix;
	size_t handle_size, prefix_size;
	int i;

	if (!fyt_td)
		return false;

	handle = fy_tag_directive_token_handle(fyt_td, &handle_size);
	prefix = fy_tag_directive_token_prefix(fyt_td, &prefix_size);
	if (!handle || !prefix)
		return false;

	for (i = 0; (fytag = fy_default_tags[i]) != NULL; i++) {

		if (handle_size == strlen(fytag->handle) &&
		    !memcmp(handle, fytag->handle, handle_size) &&
		    prefix_size == strlen(fytag->prefix) &&
		    !memcmp(prefix, fytag->prefix, prefix_size))
			return true;

	}
	return false;
}

int fy_append_tag_directive(struct fy_parser *fyp,
			    struct fy_document_state *fyds,
			    const char *handle, const char *prefix)
{
	struct fy_token *fyt = NULL;
	struct fy_input *fyi = NULL;
	char *data;
	size_t size, handle_size, prefix_size;
	struct fy_atom atom;

	size = strlen(handle) + 1 + strlen(prefix);
	data = fy_parse_alloc(fyp, size + 1);
	fy_error_check(fyp, data, err_out,
			"fy_parse_alloc() failed");

	snprintf(data, size + 1, "%s %s", handle, prefix);

	fyi = fy_parse_input_from_data(fyp, data, size, &atom, true);
	fy_error_check(fyp, fyi, err_out,
			"fy_parse_input_from_data() failed");

	handle_size = strlen(handle);
	prefix_size = strlen(prefix);

	fyt = fy_token_create(fyp, FYTT_TAG_DIRECTIVE, &atom,
			     handle_size, prefix_size);
	fy_error_check(fyp, fyt, err_out,
			"fy_token_create() failed");

	fy_token_list_add_tail(&fyds->fyt_td, fyt);

	if (!fy_tag_is_default(handle, handle_size, prefix, prefix_size))
		fyds->tags_explicit = true;

	return 0;

err_out:
	fy_token_unref(fyt);
	fy_input_unref(fyi);
	return -1;
}

int fy_fill_default_document_state(struct fy_parser *fyp,
				    struct fy_document_state *fyds,
				    int version_major, int version_minor,
				    const struct fy_tag * const *default_tags)
{
	const struct fy_tag *fytag;
	int i, rc;

	if (!default_tags)
		default_tags = fy_default_tags;

	fyds->version.major = version_major >= 0 ? version_major : FY_DEFAULT_YAML_VERSION_MAJOR;
	fyds->version.minor = version_minor >= 0 ? version_minor : FY_DEFAULT_YAML_VERSION_MINOR;

	fyds->version_explicit = false;
	fyds->tags_explicit = false;
	fyds->start_implicit = true;
	fyds->end_implicit = true;

	memset(&fyds->start_mark, 0, sizeof(fyds->start_mark));
	memset(&fyds->end_mark, 0, sizeof(fyds->end_mark));

	fyds->fyt_vd = NULL;
	fy_token_list_init(&fyds->fyt_td);

	for (i = 0; (fytag = default_tags[i]) != NULL; i++) {

		rc = fy_append_tag_directive(fyp, fyds, fytag->handle, fytag->prefix);
		fy_error_check(fyp, !rc, err_out,
				"fy_append_tag_directive() failed");
	}

	return 0;

err_out:
	return -1;
}

int fy_set_default_document_state(struct fy_parser *fyp,
				  int version_major, int version_minor,
				  const struct fy_tag * const *default_tags)
{
	struct fy_document_state *fyds;
	int rc;

	if (fyp->current_document_state) {
		fy_document_state_unref(fyp->current_document_state);
		fyp->current_document_state = NULL;
	}

	fyds = fy_parse_document_state_alloc(fyp);
	fy_error_check(fyp, fyds, err_out,
			"fy_parse_document_state_alloc() failed");
	fyp->current_document_state = fyds;

	rc = fy_fill_default_document_state(fyp, fyds, version_major, version_minor, default_tags);
	fy_error_check(fyp, !rc, err_out,
			"fy_fill_default_document_state() failed");

	return 0;
err_out:
	return -1;
}

int fy_reset_document_state(struct fy_parser *fyp)
{
	int rc;

	if (fyp->external_document_state) {
		fy_scan_debug(fyp, "not resetting document state");
		return 0;
	}
	fy_scan_debug(fyp, "resetting document state");
	rc = fy_set_default_document_state(fyp, -1, -1, NULL);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_set_default_document_state() failed");

	/* TODO check when cleaning flow lists */
	fyp->flow_level = 0;
	fyp->flow = FYFT_NONE;
	fy_parse_flow_list_recycle_all(fyp, &fyp->flow_stack);

	return 0;

err_out_rc:
	return rc;
}

int fy_check_document_version(struct fy_parser *fyp)
{
	int major, minor;

	major = fyp->current_document_state->version.major;
	minor = fyp->current_document_state->version.minor;

	/* we only support YAML version 1.x */
	if (major == 1) {
		/* 1.1 is supported without warnings */
		if (minor == 1)
			goto ok;

		if (minor == 2 || minor == 3)
			goto experimental;
	}

	return -1;

experimental:
	fy_scan_debug(fyp, "Experimental support for version %d.%d",
			major, minor);
ok:
	return 0;
}

int fy_parse_version_directive(struct fy_parser *fyp, struct fy_token *fyt)
{
	struct fy_document_state *fyds;
	struct fy_error_ctx ec;
	const char *vs;
	size_t vs_len;
	char *vs0;
	char *s, *e;
	long v;
	int rc;

	fy_error_check(fyp, fyt && fyt->type == FYTT_VERSION_DIRECTIVE, err_out,
			"illegal token (or missing) version directive token");

	fyds = fyp->current_document_state;
	fy_error_check(fyp, fyds, err_out,
			"no current document state error");

	FY_ERROR_CHECK(fyp, fyt, &ec, FYEM_PARSE,
			!fyds->fyt_vd,
			err_duplicate_version_directive);

	/* version directive of the form: MAJ.MIN */
	vs = fy_token_get_text(fyt, &vs_len);
	fy_error_check(fyp, vs, err_out,
			"fy_token_get_text() failed");
	vs0 = alloca(vs_len + 1);
	memcpy(vs0, vs, vs_len);
	vs0[vs_len] = '\0';

	/* parse version numbers */
	v = strtol(vs0, &e, 10);
	fy_error_check(fyp, e > vs0 && v >= 0 && v <= INT_MAX, err_out,
			"illegal major version number (%s)", vs0);
	fy_error_check(fyp, *e == '.', err_out,
			"illegal version separator");
	fyds->version.major = (int)v;

	s = e + 1;
	v = strtol(s, &e, 10);
	fy_error_check(fyp, e > s && v >= 0 && v <= INT_MAX, err_out,
			"illegal minor version number");
	fy_error_check(fyp, *e == '\0', err_out,
			"garbage after version number");
	fyds->version.minor = (int)v;

	fy_scan_debug(fyp, "document parsed YAML version: %d.%d",
			fyds->version.major,
			fyds->version.minor);

	rc = fy_check_document_version(fyp);
	fy_error_check(fyp, !rc, err_out_rc,
			"unsupport version number %d.%d",
			fyds->version.major,
			fyds->version.minor);

	fyds->version_explicit = true;
	fyds->fyt_vd = fyt;

	return 0;
err_out:
	rc = -1;
err_out_rc:
	fy_token_unref(fyt);
	return rc;
err_duplicate_version_directive:
	fy_error_report(fyp, &ec, "duplicate version directive");
	goto err_out;
}

int fy_parse_tag_directive(struct fy_parser *fyp, struct fy_token *fyt)
{
	struct fy_document_state *fyds;
	struct fy_token *fyt_td;
	const char *handle, *prefix;
	size_t handle_size, prefix_size;
	struct fy_error_ctx ec;
	bool can_override;

	fyds = fyp->current_document_state;
	fy_error_check(fyp, fyds, err_out,
			"no current document state error");

	handle = fy_tag_directive_token_handle(fyt, &handle_size);
	fy_error_check(fyp, handle, err_out,
			"bad tag directive token (handle)");

	prefix = fy_tag_directive_token_prefix(fyt, &prefix_size);
	fy_error_check(fyp, prefix, err_out,
			"bad tag directive token (prefix)");

	fyt_td = fy_document_state_lookup_tag_directive(fyds, handle, handle_size);

	can_override = fyt_td && fy_token_tag_directive_is_overridable(fyt_td);

	FY_ERROR_CHECK(fyp, fyt, &ec, FYEM_PARSE,
			!fyt_td || can_override,
			err_duplicate_tag_directive);

	if (fyt_td) {
		fy_notice(fyp, "overriding tag");
		fy_token_list_del(&fyds->fyt_td, fyt_td);
		fy_token_unref(fyt_td);
	}

	fy_token_list_add_tail(&fyds->fyt_td, fyt);

	fy_scan_debug(fyp, "document parsed tag directive with handle=%.*s",
			(int)handle_size, handle);

	if (!fy_tag_is_default(handle, handle_size, prefix, prefix_size))
		fyds->tags_explicit = true;

	return 0;
err_out:
	return -1;

err_duplicate_tag_directive:
	fy_error_report(fyp, &ec, "duplicate tag directive");
	goto err_out;
}

static const struct fy_parse_cfg default_parse_cfg = {
	.search_path = "",
	.flags = FYPCF_DEBUG_LEVEL_INFO | FYPCF_DEBUG_DIAG_TYPE | FYPCF_COLOR_AUTO | FYPCF_DEBUG_ALL,
};

int fy_parse_setup(struct fy_parser *fyp, const struct fy_parse_cfg *cfg)
{
	int rc;

	memset(fyp, 0, sizeof(*fyp));

	fyp->cfg = cfg ? *cfg : default_parse_cfg;

	fy_talloc_list_init(&fyp->tallocs);

	fy_indent_list_init(&fyp->indent_stack);
	fy_indent_list_init(&fyp->recycled_indent);
	fyp->indent = -2;
	fyp->generated_block_map = false;

	fy_simple_key_list_init(&fyp->simple_keys);
	fy_simple_key_list_init(&fyp->recycled_simple_key);

	fy_token_list_init(&fyp->queued_tokens);
	fy_token_list_init(&fyp->recycled_token);

	fy_input_list_init(&fyp->parsed_inputs);
	fy_input_list_init(&fyp->queued_inputs);
	fy_input_list_init(&fyp->recycled_input);

	fyp->state = FYPS_NONE;
	fy_parse_state_log_list_init(&fyp->state_stack);
	fy_parse_state_log_list_init(&fyp->recycled_parse_state_log);

	fy_eventp_list_init(&fyp->recycled_eventp);

	fy_flow_list_init(&fyp->flow_stack);
	fyp->flow = FYFT_NONE;
	fy_flow_list_init(&fyp->recycled_flow);

	fy_document_state_list_init(&fyp->recycled_document_state);

	fyp->pending_complex_key_column = -1;
	fyp->last_block_mapping_key_line = -1;

	fyp->suppress_recycling = !!(fyp->cfg.flags & FYPCF_DISABLE_RECYCLING) ||
		                  getenv("FY_VALGRIND");

	if (fyp->suppress_recycling)
		fy_notice(fyp, "Suppressing recycling");

	fyp->current_document_state = NULL;
	rc = fy_reset_document_state(fyp);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_reset_document_state() failed");

	return 0;

err_out_rc:
	return rc;
}

void fy_parse_cleanup(struct fy_parser *fyp)
{
	struct fy_input *fyi, *fyin;

	if (fyp->errfp)
		fclose(fyp->errfp);

	if (fyp->errbuf)
		free(fyp->errbuf);

	fy_parse_indent_list_recycle_all(fyp, &fyp->indent_stack);
	fy_parse_simple_key_list_recycle_all(fyp, &fyp->simple_keys);
	fy_token_list_unref_all(&fyp->queued_tokens);

	fy_parse_parse_state_log_list_recycle_all(fyp, &fyp->state_stack);
	fy_parse_flow_list_recycle_all(fyp, &fyp->flow_stack);

	fy_token_unref(fyp->stream_end_token);

	if (fyp->current_document_state)
		fy_document_state_unref(fyp->current_document_state);

	for (fyi = fy_input_list_head(&fyp->queued_inputs); fyi; fyi = fyin) {
		fyin = fy_input_next(&fyp->queued_inputs, fyi);
		fy_input_unref(fyi);
	}

	for (fyi = fy_input_list_head(&fyp->parsed_inputs); fyi; fyi = fyin) {
		fyin = fy_input_next(&fyp->parsed_inputs, fyi);
		fy_input_unref(fyi);
	}

	fy_input_unref(fyp->current_input);
	fyp->current_input = NULL;

	/* and vacuum (free everything) */
	fy_parse_indent_vacuum(fyp);
	fy_parse_simple_key_vacuum(fyp);
	fy_parse_token_vacuum(fyp);
	fy_parse_input_vacuum(fyp);
	fy_parse_parse_state_log_vacuum(fyp);
	fy_parse_eventp_vacuum(fyp);
	fy_parse_flow_vacuum(fyp);
	// fy_parse_document_state_vacuum(fyp);

	/* and release all the remaining tracked memory */
	fy_tfree_all(&fyp->tallocs);
}

/* open a file for reading respecting the search path */
static int fy_path_open(struct fy_parser *fyp, const char *name, char **fullpathp)
{
	char *sp, *s, *e, *t, *newp;
	size_t len, maxlen;
	int fd;

	if (!fyp || !name || name[0] == '\0')
		return -1;

	/* for a full path, or no search path, open directly */
	if (name[0] == '/' || !fyp->cfg.search_path || !fyp->cfg.search_path[0])
		return open(name, O_RDONLY);

	len = strlen(fyp->cfg.search_path);
	sp = alloca(len + 1);
	memcpy(sp, fyp->cfg.search_path, len + 1);

	/* allocate the maximum possible so that we don't deal with reallocations */
	maxlen = len + 1 + strlen(name);
	newp = fy_parser_alloc(fyp, maxlen + 1);
	if (!newp)
		return -1;

	s = sp;
	e = sp + strlen(s);
	while (s < e) {
		/* skip completely empty */
		if (*s == ':') {
			s++;
			continue;
		}

		t = strchr(s, ':');
		if (t)
			*t++ = '\0';
		else
			t = e;

		len = strlen(s) + 1 + strlen(name) + 1;
		snprintf(newp, maxlen, "%s/%s", s, name);

		/* try opening */
		fd = open(newp, O_RDONLY);
		if (fd != -1) {
			fy_scan_debug(fyp, "opened file %s at %s", name, newp);

			if (fullpathp)
				*fullpathp = newp;
			else
				fy_parser_free(fyp, newp);
			return fd;
		}

		s = t;
	}

	fy_parser_free(fyp, newp);
	return -1;
}

struct fy_input *fy_input_alloc(void)
{
	struct fy_input *fyi;

	fyi = malloc(sizeof(*fyi));
	if (!fyi)
		return NULL;
	memset(fyi, 0, sizeof(*fyi));

	fyi->state = FYIS_NONE;
	fyi->refs = 1;

	/* fy_notice(NULL, "%s: %p #%d", __func__, fyi, fyi->refs); */

	return fyi;
}

void fy_input_free(struct fy_input *fyi)
{
	if (!fyi)
		return;

	assert(fyi->refs == 1);

	/* fy_notice(NULL, "%s: %p #%d", __func__, fyi, fyi->refs); */
	if (fyi->on_list) {
		fy_input_list_del(fyi->on_list, fyi);
		fyi->on_list = NULL;
	}

	switch (fyi->state) {
	case FYIS_NONE:
	case FYIS_QUEUED:
		/* nothing to do */
		break;
	case FYIS_PARSE_IN_PROGRESS:
	case FYIS_PARSED:
		fy_input_close(fyi);
		break;
	}

	free(fyi);
}

struct fy_input *fy_input_ref(struct fy_input *fyi)
{
	if (!fyi)
		return NULL;


	assert(fyi->refs + 1 > 0);

	fyi->refs++;

	/* fy_notice(NULL, "%s: %p #%d", __func__, fyi, fyi->refs); */

	return fyi;
}

void fy_input_unref(struct fy_input *fyi)
{
	if (!fyi)
		return;

	assert(fyi->refs > 0);

	/* fy_notice(NULL, "%s: %p #%d", __func__, fyi, fyi->refs); */

	if (fyi->refs == 1)
		fy_input_free(fyi);
	else
		fyi->refs--;
}

struct fy_input *fy_parse_input_alloc(struct fy_parser *fyp)
{
	struct fy_input *fyi;

	if (!fyp)
		return NULL;

	fyi = fy_input_alloc();
	if (!fyi)
		return NULL;

	return fyi;
}

void fy_parse_input_recycle(struct fy_parser *fyp, struct fy_input *fyi)
{
	fy_input_unref(fyi);
}

int fy_parse_input_open(struct fy_parser *fyp, struct fy_input *fyi)
{
	struct stat sb;
	int rc;

	if (!fyi)
		return -1;

	assert(fyi->state == FYIS_QUEUED);

	/* reset common data */
	fyi->buffer = NULL;
	fyi->allocated = 0;
	fyi->read = 0;
	fyi->chunk = 0;
	fyi->fp = NULL;

	switch (fyi->cfg.type) {
	case fyit_file:
		fy_error_check(fyp, fyp, err_out,
				"parser association missing for file");
		memset(&fyi->file, 0, sizeof(fyi->file));
		fyi->file.fd = fy_path_open(fyp, fyi->cfg.file.filename, NULL);
		fy_error_check(fyp, fyi->file.fd != -1, err_out,
				"failed to open %s",  fyi->cfg.file.filename);

		rc = fstat(fyi->file.fd, &sb);
		fy_error_check(fyp, rc != -1, err_out,
				"failed to fstat %s", fyi->cfg.file.filename);

		fyi->file.length = sb.st_size;

		/* only map if not zero (and is not disabled) */
		if (sb.st_size > 0 && !(fyp->cfg.flags & FYPCF_DISABLE_MMAP_OPT)) {
			fyi->file.addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE,
					fyi->file.fd, 0);

			/* convert from MAP_FAILED to NULL */
			if (fyi->file.addr == MAP_FAILED)
				fyi->file.addr = NULL;
		}
		/* if we've managed to mmap, we' good */
		if (fyi->file.addr)
			break;

		fy_scan_debug(fyp, "direct mmap mode unavailable for file %s, switching to stream mode",
				fyi->cfg.file.filename);

		fyi->fp = fdopen(fyi->file.fd, "r");
		fy_error_check(fyp, rc != -1, err_out,
				"failed to fdopen %s", fyi->cfg.file.filename);

		/* fd ownership assigned to file */
		fyi->file.fd = -1;

		/* switch to stream mode */
		fyi->chunk = sysconf(_SC_PAGESIZE);
		fyi->buffer = malloc(fyi->chunk);
		fy_error_check(fyp, fyi->buffer, err_out,
				"fy_alloc() failed");
		fyi->allocated = fyi->chunk;
		break;

	case fyit_stream:
		memset(&fyi->stream, 0, sizeof(fyi->stream));
		fyi->chunk = fyi->cfg.stream.chunk;
		if (!fyi->chunk)
			fyi->chunk = sysconf(_SC_PAGESIZE);
		fyi->buffer = malloc(fyi->chunk);
		fy_error_check(fyp, fyi->buffer, err_out,
				"fy_alloc() failed");
		fyi->allocated = fyi->chunk;
		fyi->fp = fyi->cfg.stream.fp;
		break;

	case fyit_memory:
		/* nothing to do for memory */
		break;

	default:
		assert(0);
		break;
	}

	fyi->state = FYIS_PARSE_IN_PROGRESS;

	return 0;

err_out:
	fy_input_close(fyi);
	return -1;
}

void fy_input_close(struct fy_input *fyi)
{
	if (!fyi)
		return;

	switch (fyi->cfg.type) {
	case fyit_file:
		if (fyi->file.fd != -1) {
			close(fyi->file.fd);
			fyi->file.fd = -1;
		}
		if (fyi->file.addr && fyi->file.addr && fyi->file.addr != MAP_FAILED) {
			munmap(fyi->file.addr, fyi->file.length);
			fyi->file.addr = NULL;
		}
		if (fyi->buffer) {
			free(fyi->buffer);
			fyi->buffer = NULL;
		}
		if (fyi->fp) {
			fclose(fyi->fp);
			fyi->fp = NULL;
		}
		break;

	case fyit_stream:
		if (fyi->buffer) {
			free(fyi->buffer);
			fyi->buffer = NULL;
		}
		memset(&fyi->stream, 0, sizeof(fyi->stream));
		break;

	case fyit_memory:
		/* nothing */
		break;

	default:
		break;
	}
}

int fy_parse_input_done(struct fy_parser *fyp)
{
	struct fy_input *fyi;
	void *buf;

	if (!fyp)
		return -1;

	fyi = fyp->current_input;
	if (!fyi)
		return 0;

	switch (fyi->cfg.type) {
	case fyit_file:
		if (fyi->file.addr)
			break;

		/* fall-through */

	case fyit_stream:
		fy_error_check(fyp, fyp, err_out,
				"no parser associated with input");
		/* chop extra buffer */
		buf = realloc(fyi->buffer, fyp->current_input_pos);
		fy_error_check(fyp, buf || !fyp->current_input_pos, err_out,
				"realloc() failed");

		fyi->buffer = buf;
		fyi->allocated = fyp->current_input_pos;
		break;
	default:
		break;

	}

	fy_scan_debug(fyp, "moving current input to parsed inputs");

	fyi->state = FYIS_PARSED;
	fyi->on_list = &fyp->parsed_inputs;
	fy_input_list_add_tail(fyi->on_list, fyi);

	fyp->current_input = NULL;

	return 0;

err_out:
	return -1;
}

struct fy_input *fy_parse_input_from_data(struct fy_parser *fyp,
		const char *data, size_t size, struct fy_atom *handle,
		bool simple)
{
	struct fy_input *fyi;
	unsigned int aflags;

	if (data && size == (size_t)-1)
		size = strlen(data);

	fyi = fy_input_alloc();
	fy_error_check(fyp, fyi, err_out,
			"fy_input_alloc() failed");

	fyi->cfg.type = fyit_memory;
	fyi->cfg.userdata = NULL;
	fyi->cfg.memory.data = data;
	fyi->cfg.memory.size = size;

	fyi->buffer = NULL;
	fyi->allocated = 0;
	fyi->read = 0;
	fyi->chunk = 0;
	fyi->fp = NULL;

	if (size > 0)
		aflags = fy_analyze_scalar_content(data, size);
	else
		aflags = FYACF_EMPTY | FYACF_FLOW_PLAIN | FYACF_BLOCK_PLAIN;

	handle->start_mark.input_pos = 0;
	handle->start_mark.line = 0;
	handle->start_mark.column = 0;
	handle->end_mark.input_pos = size;
	handle->end_mark.line = 0;
	handle->end_mark.column = fy_utf8_count(data, size);
	/* if it's plain, all is good */
	if (simple || (aflags & FYACF_FLOW_PLAIN)) {
		handle->storage_hint = size;	/* maximum */
		handle->storage_hint_valid = false;
		handle->direct_output = true;
		handle->style = FYAS_PLAIN;
	} else {
		handle->storage_hint = 0;	/* just calculate */
		handle->storage_hint_valid = false;
		handle->direct_output = false;
		handle->style = FYAS_DOUBLE_QUOTED_MANUAL;
	}

	handle->chomp = FYAC_STRIP;
	handle->increment = 0;
	handle->fyi = fyi;

	fyi->state = FYIS_PARSED;
	fyi->on_list = &fyp->parsed_inputs;
	fy_input_list_add_tail(fyi->on_list, fyi);

	return fyi;

err_out:
	fy_input_unref(fyi);
	return NULL;
}

const void *fy_parse_input_try_pull(struct fy_parser *fyp, struct fy_input *fyi,
				    size_t pull, size_t *leftp)
{
	const void *p;
	size_t left, pos, size, nread, nreadreq, missing;
	size_t space __FY_DEBUG_UNUSED__;
	void *buf;

	if (!fyp || !fyi) {
		if (leftp)
			*leftp = 0;
		return NULL;
	}

	p = NULL;
	left = 0;
	pos = fyp->current_input_pos;

	switch (fyi->cfg.type) {
	case fyit_file:

		if (fyi->file.addr) {
			assert(fyi->file.length >= pos);

			left = fyi->file.length - pos;
			if (!left) {
				fy_scan_debug(fyp, "file input exhausted");
				break;
			}
			p = fyi->file.addr + pos;
			break;
		}

		/* fall-through */

	case fyit_stream:

		assert(fyi->read >= pos);

		left = fyi->read - pos;
		p = fyi->buffer + pos;

		/* enough to satisfy directly */
		if (left >= pull)
			break;

		/* no more */
		if (feof(fyi->fp) || ferror(fyi->fp)) {
			if (!left) {
				fy_scan_debug(fyp, "input exhausted (EOF)");
				p = NULL;
			}
			break;
		}

		space = fyi->allocated - pos;

		/* if we're missing more than the buffer space */
		missing = pull - left;

		fy_scan_debug(fyp, "input: space=%zu missing=%zu", space, missing);

		if (missing > 0) {

			/* align size to chunk */
			size = fyi->allocated + missing + fyi->chunk - 1;
			size = size - size % fyi->chunk;

			fy_scan_debug(fyp, "input buffer missing %zu bytes (pull=%zu)",
					missing, pull);
			buf = realloc(fyi->buffer, size);
			fy_error_check(fyp, buf, err_out,
					"realloc() failed");

			fy_scan_debug(fyp, "stream read allocated=%zu new-size=%zu",
					fyi->allocated, size);

			fyi->buffer = buf;
			fyi->allocated = size;

			space = fyi->allocated - pos;
			p = fyi->buffer + pos;
		}

		/* always try to read up to the allocated space */
		do {
			nreadreq = fyi->allocated - fyi->read;

			fy_scan_debug(fyp, "performing read request of %zu", nreadreq);

			nread = fread(fyi->buffer + fyi->read, 1, nreadreq, fyi->fp);

			fy_scan_debug(fyp, "read returned %zu", nread);

			if (!nread)
				break;

			fyi->read += nread;
			left = fyi->read - pos;
		} while (left < pull);

		/* no more, move it to parsed input chunk list */
		if (!left) {
			fy_scan_debug(fyp, "input exhausted (can't read enough)");
			p = NULL;
		}
		break;

	case fyit_memory:
		assert(fyi->cfg.memory.size >= pos);

		left = fyi->cfg.memory.size - pos;
		if (!left) {
			fy_scan_debug(fyp, "memory input exhausted");
			break;
		}
		p = fyi->cfg.memory.data + pos;
		break;

	default:
		assert(0);
		break;

	}

	if (leftp)
		*leftp = left;
	return p;

err_out:
	if (leftp)
		*leftp = 0;
	return NULL;
}

static const char *state_txt[] __FY_DEBUG_UNUSED__ = {
	[FYPS_NONE] = "NONE",
	[FYPS_STREAM_START] = "STREAM_START",
	[FYPS_IMPLICIT_DOCUMENT_START] = "IMPLICIT_DOCUMENT_START",
	[FYPS_DOCUMENT_START] = "DOCUMENT_START",
	[FYPS_DOCUMENT_CONTENT] = "DOCUMENT_CONTENT",
	[FYPS_DOCUMENT_END] = "DOCUMENT_END",
	[FYPS_BLOCK_NODE] = "BLOCK_NODE",
	[FYPS_BLOCK_NODE_OR_INDENTLESS_SEQUENCE] = "BLOCK_NODE_OR_INDENTLESS_SEQUENCE",
	[FYPS_FLOW_NODE] = "FLOW_NODE",
	[FYPS_BLOCK_SEQUENCE_FIRST_ENTRY] = "BLOCK_SEQUENCE_FIRST_ENTRY",
	[FYPS_BLOCK_SEQUENCE_ENTRY] = "BLOCK_SEQUENCE_ENTRY",
	[FYPS_INDENTLESS_SEQUENCE_ENTRY] = "INDENTLESS_SEQUENCE_ENTRY",
	[FYPS_BLOCK_MAPPING_FIRST_KEY] = "BLOCK_MAPPING_FIRST_KEY",
	[FYPS_BLOCK_MAPPING_KEY] = "BLOCK_MAPPING_KEY",
	[FYPS_BLOCK_MAPPING_VALUE] = "BLOCK_MAPPING_VALUE",
	[FYPS_FLOW_SEQUENCE_FIRST_ENTRY] = "FLOW_SEQUENCE_FIRST_ENTRY",
	[FYPS_FLOW_SEQUENCE_ENTRY] = "FLOW_SEQUENCE_ENTRY",
	[FYPS_FLOW_SEQUENCE_ENTRY_MAPPING_KEY] = "FLOW_SEQUENCE_ENTRY_MAPPING_KEY",
	[FYPS_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE] = "FLOW_SEQUENCE_ENTRY_MAPPING_VALUE",
	[FYPS_FLOW_SEQUENCE_ENTRY_MAPPING_END] = "FLOW_SEQUENCE_ENTRY_MAPPING_END",
	[FYPS_FLOW_MAPPING_FIRST_KEY] = "FLOW_MAPPING_FIRST_KEY",
	[FYPS_FLOW_MAPPING_KEY] = "FLOW_MAPPING_KEY",
	[FYPS_FLOW_MAPPING_VALUE] = "FLOW_MAPPING_VALUE",
	[FYPS_FLOW_MAPPING_EMPTY_VALUE] = "FLOW_MAPPING_EMPTY_VALUE",
	[FYPS_END] = "END"
};

int fy_parse_input_reset(struct fy_parser *fyp)
{
	struct fy_input *fyi, *fyin;

	/* must not be in the middle of something */
	if (fyp->state != FYPS_NONE && fyp->state != FYPS_END) {
		fy_scan_debug(fyp, "parser cannot be reset at state '%s'",
			state_txt[fyp->state]);
		return -1;
	}

	for (fyi = fy_input_list_head(&fyp->queued_inputs); fyi; fyi = fyin) {
		fyin = fy_input_next(&fyp->queued_inputs, fyi);
		fyi->on_list = NULL;
		fy_input_unref(fyi);
	}

	fy_parse_parse_state_log_list_recycle_all(fyp, &fyp->state_stack);

	fyp->stream_end_produced = false;
	fyp->stream_start_produced = false;
	fyp->state = FYPS_NONE;

	fyp->pending_complex_key_column = -1;
	fyp->last_block_mapping_key_line = -1;

	return 0;
}

int fy_parse_input_append(struct fy_parser *fyp, const struct fy_input_cfg *fyic)
{
	struct fy_input *fyi = NULL;

	fyi = fy_parse_input_alloc(fyp);
	fy_error_check(fyp, fyp != NULL, err_out,
			"fy_parse_input_alloc() failed!");

	fyi->cfg = *fyic;

	fyi->buffer = NULL;
	fyi->allocated = 0;
	fyi->read = 0;
	fyi->chunk = 0;
	fyi->fp = NULL;

	switch (fyi->cfg.type) {
	case fyit_file:
		memset(&fyi->file, 0, sizeof(fyi->file));
		fyi->file.fd = -1;
		fyi->file.addr = MAP_FAILED;
		break;

		/* nothing for those two */
	case fyit_stream:
		memset(&fyi->stream, 0, sizeof(fyi->stream));
		break;

	case fyit_memory:
		/* nothing to do for memory */
		break;

	default:
		assert(0);
		break;
	}

	fyi->state = FYIS_QUEUED;
	fyi->on_list = &fyp->queued_inputs;
	fy_input_list_add_tail(fyi->on_list, fyi);

	return 0;

err_out:
	fy_input_unref(fyi);
	return -1;
}

/* ensure that there are at least size octets available */
const void *fy_ensure_lookahead_slow_path(struct fy_parser *fyp, size_t size, size_t *leftp)
{
	const void *p;
	size_t left;

	if (!leftp)
		leftp = &left;

	p = fy_ptr(fyp, leftp);
	if (!p || *leftp < size) {
		fy_scan_debug(fyp, "ensure lookahead size=%zd left=%zd",
				size, *leftp);

		p = fy_parse_input_try_pull(fyp, fyp->current_input, size, leftp);
		if (!p || *leftp < size)
			return NULL;

		fyp->current_ptr = p;
		fyp->current_left = *leftp;
		fyp->current_c = fy_utf8_get(fyp->current_ptr, fyp->current_left, &fyp->current_w);
	}
	return p;
}

int fy_scan_comment(struct fy_parser *fyp, struct fy_atom *handle, bool single_line)
{
	int c, column, start_column, lines, scan_ahead;

	c = fy_parse_peek(fyp);
	if (c != '#')
		return -1;

	if (handle)
		fy_fill_atom_start(fyp, handle);

	lines = 0;
	start_column = fyp->column;
	column = fyp->column;
	scan_ahead = 0;

	/* continuation must be a # on the same column */
	while (c == '#' && column == start_column) {

		lines++;
		if (c == '#') {
			/* chomp until line break */
			fy_advance(fyp, c);
			while (!(fy_is_breakz(c = fy_parse_peek(fyp))))
				fy_advance(fyp, c);

			/* end of input break */
			if (fy_is_z(c))
				break;
		}

		if (!fy_is_lb(c))
			break;

		column = 0;

		scan_ahead = 1;	/* skipping over lb */
		while (fy_is_blank(c = fy_parse_peek_at(fyp, scan_ahead))) {
			scan_ahead++;
			column++;
		}

		if (fy_is_z(c) || single_line)
			break;

		if (c == '#' && column == start_column) {
			fy_advance_by(fyp, scan_ahead);
			c = fy_parse_peek(fyp);
		}
	}

	if (handle) {
		fy_fill_atom_end(fyp, handle);
		handle->style = FYAS_COMMENT;
		handle->direct_output = false;
		handle->storage_hint = 0;
		handle->storage_hint_valid = false;
	}

	return 0;
}

int fy_attach_comments_if_any(struct fy_parser *fyp, struct fy_token *fyt)
{
	int c, rc;

	if (!fyp || !fyt)
		return -1;

	/* if a last comment exists and is valid */
	if (fy_atom_is_set(&fyp->last_comment)) {
		memcpy(&fyt->comment[fycp_top], &fyp->last_comment, sizeof(fyp->last_comment));
		memset(&fyp->last_comment, 0, sizeof(fyp->last_comment));

		fy_notice(fyp, "token: %s attaching top comment:\n%s\n",
				fy_token_debug_text_a(fyt),
				fy_atom_get_text_a(&fyt->comment[fycp_top]));
	}

	/* right hand comment */

	/* skip white space */
	while (fy_is_ws(c = fy_parse_peek(fyp)))
		fy_advance(fyp, c);

	if (c == '#') {
		rc = fy_scan_comment(fyp, &fyt->comment[fycp_right], false);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_scan_comment() failed");

		fy_notice(fyp, "token: %s attaching right comment:\n%s\n",
				fy_token_debug_text_a(fyt),
				fy_atom_get_text_a(&fyt->comment[fycp_right]));
	}
	return 0;

err_out_rc:
	return rc;
}

int fy_scan_to_next_token(struct fy_parser *fyp)
{
	int c, c_after_ws, i, rc = 0;
	bool tabs_allowed;
	ssize_t offset;

	memset(&fyp->last_comment, 0, sizeof(fyp->last_comment));

	while ((c = fy_parse_peek(fyp)) >= 0) {

		/* is it BOM? skip over it */
		if (fyp->column == 0 && c == FY_UTF8_BOM)
			fy_advance(fyp, c);

		/* scan ahead until the next non-ws character */
		/* if it's a flow start one, then tabs are allowed */
		tabs_allowed = fyp->flow_level || !fyp->simple_key_allowed;
		if (!tabs_allowed && fy_is_ws(c = fy_parse_peek(fyp))) {
			i = 0;
			offset = -1;
			while (fy_is_ws(c_after_ws = fy_parse_peek_at_internal(fyp, i, &offset)))
				i++;
			/* flow start marker after spaces? allow tabs */
			if (c_after_ws == '{' || c_after_ws == '[')
				tabs_allowed = true;
		}

		/* skip white space, tabs are allowed in flow context */
		/* tabs also allowed in block context but not at start of line or after -?: */
		while ((c = fy_parse_peek(fyp)) == ' ' || (c == '\t' && tabs_allowed))
			fy_advance(fyp, c);

		if (c == '\t') {
			fy_scan_debug(fyp, "tab as token start (flow_level=%d simple_key_allowed=%s)",
					fyp->flow_level,
					fyp->simple_key_allowed ? "true" : "false");
		}

		/* comment? */
		if (c == '#') {
			rc = fy_scan_comment(fyp, &fyp->last_comment, false);
			fy_error_check(fyp, !rc, err_out_rc,
					"fy_scan_comment() failed");

			fy_notice(fyp, "unattached comment:\n%s\n",
					fy_atom_get_text_a(&fyp->last_comment));
		}

		c = fy_parse_peek(fyp);

		/* not linebreak? we're done */
		if (!fy_is_break(c)) {
			fy_scan_debug(fyp, "next token starts with c='%s'",
					fy_utf8_format_a(c, fyue_singlequote));
			break;
		}

		/* line break */
		fy_advance(fyp, c);

		/* may start simple key (in block ctx) */
		if (!fyp->flow_level) {
			fyp->simple_key_allowed = true;
			fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");
		}
	}

	fy_scan_debug(fyp, "no-next-token");

err_out_rc:
	return rc;
}

void fy_purge_required_simple_key_report(struct fy_parser *fyp,
		struct fy_error_ctx *ec,
		struct fy_token *fyt, enum fy_token_type next_type)
{
	bool is_anchor, is_tag;

	if (fyt) {
		ec->start_mark = *fy_token_start_mark(fyt);
		ec->end_mark = *fy_token_end_mark(fyt);
	}

	is_anchor = fyt && fyt->type == FYTT_ANCHOR;
	is_tag = fyt && fyt->type == FYTT_TAG;

	if (is_anchor || is_tag) {
		if ((fyp->state == FYPS_BLOCK_NODE_OR_INDENTLESS_SEQUENCE ||
			fyp->state == FYPS_BLOCK_MAPPING_VALUE ||
			fyp->state == FYPS_BLOCK_MAPPING_FIRST_KEY) &&
					next_type == FYTT_BLOCK_ENTRY) {

			fy_error_report(fyp, ec, "invalid %s indent for sequence",
					is_anchor ? "anchor" : "tag");
			return;
		}

		if (fyp->state == FYPS_BLOCK_MAPPING_VALUE && next_type == FYTT_SCALAR) {
			fy_error_report(fyp, ec, "invalid %s indent for mapping",
					is_anchor ? "anchor" : "tag");
			return;
		}
	}

	fy_error_report(fyp, ec, "could not find expected ':'");
}

static int fy_purge_stale_simple_keys(struct fy_parser *fyp, bool *did_purgep,
		enum fy_token_type next_type)
{
	struct fy_simple_key *fysk;
	struct fy_error_ctx ec;
	bool purge;
	int line;

	*did_purgep = false;
	while ((fysk = fy_simple_key_list_head(&fyp->simple_keys)) != NULL) {

		fy_scan_debug(fyp, "purge-check: flow_level=%d fysk->flow_level=%d fysk->mark.line=%d line=%d",
				fyp->flow_level, fysk->flow_level,
				fysk->mark.line, fyp->line);

		fy_debug_dump_simple_key(fyp, fysk, "purge-check: ");

		/* in non-flow context we purge keys that are on different line */
		/* in flow context we purge only those with higher flow level */
		if (!fyp->flow_level) {
			line = fysk->mark.line;
			purge = fyp->line > line;
		} else
			purge = fyp->flow_level < fysk->flow_level;

		if (!purge)
			break;

		FY_ERROR_CHECK(fyp, fysk->token, &ec, FYEM_SCAN,
				!fysk->required,
				err_remove_required);

		fy_debug_dump_simple_key(fyp, fysk, "purging: ");

		fy_simple_key_list_del(&fyp->simple_keys, fysk);
		fy_parse_simple_key_recycle(fyp, fysk);

		*did_purgep = true;
	}

	if (*did_purgep && fy_simple_key_list_empty(&fyp->simple_keys))
		fy_scan_debug(fyp, "(purge) simple key list is now empty!");

	return 0;

err_out:
	return -1;

err_remove_required:
	fy_purge_required_simple_key_report(fyp, &ec, fysk->token, next_type);
	goto err_out;
}

int fy_push_indent(struct fy_parser *fyp, int indent, bool generated_block_map)
{
	struct fy_indent *fyit;

	fyit = fy_parse_indent_alloc(fyp);
	fy_error_check(fyp, fyit != NULL, err_out,
		"fy_indent_alloc() failed");

	fyit->indent = fyp->indent;
	fyit->generated_block_map = fyp->generated_block_map;

	/* push */
	fy_indent_list_push(&fyp->indent_stack, fyit);

	/* update current state */
	fyp->parent_indent = fyp->indent;
	fyp->indent = indent;
	fyp->generated_block_map = generated_block_map;

	fy_scan_debug(fyp, "push_indent %d -> %d - generated_block_map=%s\n",
			fyp->parent_indent, fyp->indent,
			fyp->generated_block_map ? "true" : "false");

	return 0;

err_out:
	return -1;
}

int fy_parse_unroll_indent(struct fy_parser *fyp, int column)
{
	struct fy_indent *fyi;
	int prev_indent __FY_DEBUG_UNUSED__;
	struct fy_token *fyt;

	/* do nothing in flow context */
	if (fyp->flow_level)
		return 0;

	/* pop while indentation level greater than argument */
	while (fyp->indent > column) {

		fy_scan_debug(fyp, "unrolling: %d/%d", fyp->indent, column);

		/* create a block end token */
		fyt = fy_token_queue(fyp, FYTT_BLOCK_END, fy_fill_atom_a(fyp, 0));
		fy_error_check(fyp, fyt, err_out,
				"fy_token_queue() failed");

		fyi = fy_indent_list_pop(&fyp->indent_stack);
		fy_error_check(fyp, fyi, err_out,
				"no indent on stack popped");

		prev_indent = fyp->indent;

		/* pop the indent and update */
		fyp->indent = fyi->indent;
		fyp->generated_block_map = fyi->generated_block_map;

		/* pop and recycle */
		fy_parse_indent_recycle(fyp, fyi);

		/* update the parent indent */
		fyi = fy_indent_list_head(&fyp->indent_stack);
		fyp->parent_indent = fyi ? fyi->indent : -2;

		fy_scan_debug(fyp, "pop indent %d -> %d (parent %d) - generated_block_map=%s\n",
				prev_indent, fyp->indent, fyp->parent_indent,
				fyp->generated_block_map ? "true" : "false");

	}
	return 0;
err_out:
	return -1;
}

void fy_remove_all_simple_keys(struct fy_parser *fyp)
{
	struct fy_simple_key *fysk;

	fy_scan_debug(fyp, "SK: removing all");

	while ((fysk = fy_simple_key_list_pop(&fyp->simple_keys)) != NULL)
		fy_parse_simple_key_recycle(fyp, fysk);

	fyp->simple_key_allowed = true;
	fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");
}

struct fy_simple_key *fy_would_remove_required_simple_key(struct fy_parser *fyp)
{
	struct fy_simple_key *fysk;

	/* no simple key? */
	for (fysk = fy_simple_key_list_head(&fyp->simple_keys);
			fysk && fysk->flow_level >= fyp->flow_level;
			fysk = fy_simple_key_next(&fyp->simple_keys, fysk)) {
		if (fysk->required)
			return fysk;
	}

	return NULL;
}

int fy_remove_simple_key(struct fy_parser *fyp, enum fy_token_type next_type)
{
	struct fy_simple_key *fysk;
	struct fy_error_ctx ec;

	/* no simple key? */
	while ((fysk = fy_simple_key_list_first(&fyp->simple_keys)) != NULL &&
		fysk->flow_level >= fyp->flow_level) {

		fy_debug_dump_simple_key(fyp, fysk, "removing: ");

		/* remove it from the list */
		fy_simple_key_list_del(&fyp->simple_keys, fysk);

		FY_ERROR_CHECK(fyp, fysk->token, &ec, FYEM_SCAN,
				!fysk->required,
				err_remove_required);

		fy_parse_simple_key_recycle(fyp, fysk);
	}

	return 0;

err_out:
	fy_parse_simple_key_recycle(fyp, fysk);
	return -1;

err_remove_required:
	fy_purge_required_simple_key_report(fyp, &ec, fysk->token, next_type);
	goto err_out;
}

struct fy_simple_key *fy_simple_key_find(struct fy_parser *fyp, const struct fy_token *fyt)
{
	struct fy_simple_key *fysk;

	if (!fyt)
		return NULL;

	/* no simple key? */
	for (fysk = fy_simple_key_list_head(&fyp->simple_keys); fysk;
			fysk = fy_simple_key_next(&fyp->simple_keys, fysk))
		if (fysk->token == fyt)
			return fysk;

	return NULL;
}

int fy_save_simple_key(struct fy_parser *fyp, struct fy_mark *mark, struct fy_mark *end_mark,
		struct fy_token *fyt, bool required, int flow_level,
		enum fy_token_type next_type)
{
	struct fy_simple_key *fysk;
	bool did_purge;
	int rc;

	fy_error_check(fyp, fyt && mark && end_mark, err_out,
			"illegal arguments to fy_save_simple_key");

	rc = fy_purge_stale_simple_keys(fyp, &did_purge, next_type);
	fy_error_check(fyp, !rc, err_out_rc,
		"fy_purge_stale_simple_keys() failed");

	/* if no simple key is allowed, don't save */
	if (!fyp->simple_key_allowed) {
		fy_scan_debug(fyp, "not saving simple key; not allowed");
		return 0;
	}

	/* remove pending complex key mark if in non flow context and a new line */
	if (!fyp->flow_level && fyp->pending_complex_key_column >= 0 &&
	mark->line > fyp->pending_complex_key_mark.line &&
	mark->column <= fyp->pending_complex_key_mark.column ) {

		fy_scan_debug(fyp, "resetting pending_complex_key mark->line=%d line=%d\n",
				mark->line, fyp->pending_complex_key_mark.line);

		fyp->pending_complex_key_column = -1;
		fy_scan_debug(fyp, "pending_complex_key_column -> %d",
				fyp->pending_complex_key_column);
	}

	fysk = fy_simple_key_list_head(&fyp->simple_keys);

	/* create new simple key if it does not exist or if has flow level less */
	if (!fysk || fysk->flow_level < fyp->flow_level) {

		fysk = fy_parse_simple_key_alloc(fyp);
		fy_error_check(fyp, fysk != NULL, err_out,
			"fy_simple_key_alloc()");

		fy_scan_debug(fyp, "new simple key");

		fy_simple_key_list_push(&fyp->simple_keys, fysk);

	} else {
		fy_error_check(fyp, !fysk->possible || !fysk->required, err_out,
				"cannot save simple key, top is required");

		if (fysk == fy_simple_key_list_tail(&fyp->simple_keys))
			fy_scan_debug(fyp, "(reuse) simple key list is now empty!");

		fy_scan_debug(fyp, "reusing simple key");

	}

	fysk->mark = *mark;
	fysk->end_mark = *end_mark;

	fysk->possible = true;
	fysk->required = required;
	fysk->token = fyt;
	fysk->flow_level = flow_level;

	fy_debug_dump_simple_key_list(fyp, &fyp->simple_keys, fysk, "fyp->simple_keys (saved): ");

	return 0;

err_out:
	rc = -1;
err_out_rc:
	return rc;
}

struct fy_simple_key_mark {
	struct fy_mark mark;
	bool required;
	int flow_level;
};

void fy_get_simple_key_mark(struct fy_parser *fyp, struct fy_simple_key_mark *fyskm)
{
	fy_get_mark(fyp, &fyskm->mark);
	fyskm->flow_level = fyp->flow_level;
	fyskm->required = !fyp->flow_level && fyp->indent == fyp->column;
}

int fy_save_simple_key_mark(struct fy_parser *fyp,
			struct fy_simple_key_mark *fyskm,
			enum fy_token_type next_type,
			struct fy_mark *end_markp)
{
	struct fy_mark end_mark;

	if (!end_markp) {
		fy_get_mark(fyp, &end_mark);
		end_markp = &end_mark;
	}

	return fy_save_simple_key(fyp, &fyskm->mark, end_markp,
			fy_token_list_last(&fyp->queued_tokens),
			fyskm->required, fyskm->flow_level,
			next_type);
}

int fy_parse_flow_push(struct fy_parser *fyp)
{
	struct fy_flow *fyf;

	fyf = fy_parse_flow_alloc(fyp);
	fy_error_check(fyp, fyf != NULL, err_out,
			"fy_flow_alloc() failed!");
	fyf->flow = fyp->flow;

	fyf->pending_complex_key_column = fyp->pending_complex_key_column;
	fyf->pending_complex_key_mark = fyp->pending_complex_key_mark;

	fy_scan_debug(fyp, "flow_push: flow=%d pending_complex_key_column=%d",
			(int)fyf->flow,
			fyf->pending_complex_key_column);

	fy_flow_list_push(&fyp->flow_stack, fyf);

	if (fyp->pending_complex_key_column >= 0) {
		fyp->pending_complex_key_column = -1;
		fy_scan_debug(fyp, "pending_complex_key_column -> %d",
				fyp->pending_complex_key_column);
	}

	return 0;
err_out:
	return -1;
}

int fy_parse_flow_pop(struct fy_parser *fyp)
{
	struct fy_flow *fyf;

	fyf = fy_flow_list_pop(&fyp->flow_stack);
	fy_error_check(fyp, fyf, err_out,
			"no flow to pop");

	fyp->flow = fyf->flow;
	fyp->pending_complex_key_column = fyf->pending_complex_key_column;
	fyp->pending_complex_key_mark = fyf->pending_complex_key_mark;

	fy_parse_flow_recycle(fyp, fyf);

	fy_scan_debug(fyp, "flow_pop: flow=%d pending_complex_key_column=%d",
			(int)fyp->flow,
			fyp->pending_complex_key_column);

	return 0;

err_out:
	return -1;
}


int fy_fetch_stream_start(struct fy_parser *fyp)
{
	struct fy_token *fyt;

	/* simple key is allowed */
	fyp->simple_key_allowed = true;
	fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");

	fyt = fy_token_queue(fyp, FYTT_STREAM_START, fy_fill_atom_a(fyp, 0));
	fy_error_check(fyp, fyt, err_out,
			"fy_token_queue() failed");
	return 0;

err_out:
	return -1;
}

int fy_fetch_stream_end(struct fy_parser *fyp)
{
	struct fy_token *fyt;
	int rc;

	/* force new line */
	if (fyp->column) {
		fyp->column = 0;
		fyp->line++;
	}

	fy_remove_all_simple_keys(fyp);

	rc = fy_parse_unroll_indent(fyp, -1);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_parse_unroll_indent() failed");

	fyt = fy_token_queue(fyp, FYTT_STREAM_END, fy_fill_atom_a(fyp, 0));
	fy_error_check(fyp, fyt, err_out_rc,
			"fy_token_queue() failed");

	return 0;

err_out_rc:
	return rc;
}

int fy_scan_tag_uri_length(struct fy_parser *fyp, int start)
{
	int c, cn, length;
	ssize_t offset, offset1;

	/* first find the utf8 length of the uri */
	length = 0;
	offset = -1;
	while (fy_is_uri(c = fy_parse_peek_at_internal(fyp, start + length, &offset))) {

		offset1 = offset;
		cn = fy_parse_peek_at_internal(fyp, start + length + 1, &offset1);

		/* special handling for detecting URIs ending in ,}] */
		if (fy_is_blankz(cn) && fy_utf8_strchr(",}]", c))
			break;

		length++;
	}

	return length;
}

bool fy_scan_tag_uri_is_valid(struct fy_parser *fyp, int start, int length)
{
	int i, j, k, width, c, w;
	uint8_t octet, esc_octets[4];
	ssize_t offset;
	struct fy_error_ctx ec;

	offset = -1;
	for (i = 0; i < length; i++) {
		c = fy_parse_peek_at_internal(fyp, start + i, &offset);
		if (c != '%')
			continue;
		/* reset cursor */
		offset = -1;

		width = 0;
		k = 0;
		do {
			/* % escape */
			FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
					(length - i) >= 3,
					err_short_uri_escape);

			if (width > 0) {
				c = fy_parse_peek_at(fyp, start + i);

				FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
						c == '%',
						err_missing_uri_escape);
			}

			octet = 0;

			for (j = 0; j < 2; j++) {
				c = fy_parse_peek_at(fyp, start + i + 1 + j);

				FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
						fy_is_hex(c),
						err_non_hex_uri_escape);
				octet <<= 4;
				if (c >= '0' && c <= '9')
					octet |= c - '0';
				else if (c >= 'a' && c <= 'f')
					octet |= 10 + c - 'a';
				else
					octet |= 10 + c - 'A';
			}
			if (!width) {
				width = fy_utf8_width_by_first_octet(octet);

				FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
						width >= 1 && width <= 4,
						err_bad_width_uri_escape);
				k = 0;
			}
			esc_octets[k++] = octet;

			/* skip over the 3 character escape */
			i += 3;

		} while (--width > 0);

		/* now convert to utf8 */
		c = fy_utf8_get(esc_octets, k, &w);

		FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
				c >= 0,
				err_bad_utf8_uri_escape);
	}
	return true;
err_out:
	return false;

err_short_uri_escape:
	fy_advance_octets(fyp, start + i);
	fy_get_mark(fyp, &ec.start_mark);
	ec.end_mark = ec.start_mark;
	fy_error_report(fyp, &ec, "short URI escape");
	goto err_out;

err_missing_uri_escape:
	fy_advance_octets(fyp, start + i);
	fy_get_mark(fyp, &ec.start_mark);
	ec.end_mark = ec.start_mark;
	fy_error_report(fyp, &ec, "missing URI escape");
	goto err_out;

err_non_hex_uri_escape:
	fy_advance_octets(fyp, start + i + 1 + j);
	fy_get_mark(fyp, &ec.start_mark);
	ec.end_mark = ec.start_mark;
	fy_error_report(fyp, &ec, "non hex URI escape");
	goto err_out;

err_bad_width_uri_escape:
	fy_advance_octets(fyp, start + i + 1 + j);
	fy_get_mark(fyp, &ec.start_mark);
	ec.end_mark = ec.start_mark;
	fy_error_report(fyp, &ec, "bad width for hex URI escape");
	goto err_out;

err_bad_utf8_uri_escape:
	fy_advance_octets(fyp, start + i + 1 + j);
	fy_get_mark(fyp, &ec.start_mark);
	ec.end_mark = ec.start_mark;
	fy_error_report(fyp, &ec, "bad utf8 URI escape 0x%x", c);
	goto err_out;
}

int fy_scan_tag_handle_length(struct fy_parser *fyp, int start)
{
	int c, length;
	ssize_t offset;
	struct fy_error_ctx ec;

	length = 0;

	offset = -1;
	c = fy_parse_peek_at_internal(fyp, start + length, &offset);

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			c == '!',
			err_bad_handle_start);

	length++;

	/* get first character of the tag */
	c = fy_parse_peek_at_internal(fyp, start + length, &offset);
	if (fy_is_ws(c))
		return length;

	/* if first character is !, empty handle */
	if (c == '!') {
		length++;
		return length;
	}

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			fy_is_first_alpha(c),
			err_non_alpha_handle);
	length++;

	/* now loop while it's alphanumeric */
	while (fy_is_alnum(c = fy_parse_peek_at_internal(fyp, start + length, &offset)))
		length++;

	/* if last character is !, copy it */
	if (c == '!')
		length++;

	return length;

err_out:
	return -1;

err_bad_handle_start:
	fy_advance_octets(fyp, start + length);
	fy_get_mark(fyp, &ec.start_mark);
	ec.end_mark = ec.start_mark;
	fy_error_report(fyp, &ec, "invalid tag handle start");
	goto err_out;

err_non_alpha_handle:
	fy_advance_octets(fyp, start + length);
	fy_get_mark(fyp, &ec.start_mark);
	ec.end_mark = ec.start_mark;
	fy_error_report(fyp, &ec, "invalid tag handle content");
	goto err_out;
}

int fy_scan_yaml_version_length(struct fy_parser *fyp)
{
	int c, length, start_length;
	ssize_t offset;
	struct fy_error_ctx ec;

	/* now loop while it's alphanumeric */
	length = 0;
	offset = -1;
	while (fy_is_num(c = fy_parse_peek_at_internal(fyp, length, &offset)))
		length++;

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			length > 0,
			err_missing_major);
	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			c == '.',
			err_missing_comma);
	length++;

	start_length = length;
	while (fy_is_num(c = fy_parse_peek_at_internal(fyp, length, &offset)))
		length++;

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			length > start_length,
			err_missing_minor);
	return length;

err_out:
	return -1;

err_missing_major:
	fy_advance_octets(fyp, length);
	fy_get_mark(fyp, &ec.start_mark);
	ec.end_mark = ec.start_mark;
	fy_error_report(fyp, &ec, "version directive missing major number");
	goto err_out;

err_missing_comma:
	fy_advance_octets(fyp, length);
	fy_get_mark(fyp, &ec.start_mark);
	ec.end_mark = ec.start_mark;
	fy_error_report(fyp, &ec, "version directive missing comma separator");
	goto err_out;

err_missing_minor:
	fy_advance_octets(fyp, length);
	fy_get_mark(fyp, &ec.start_mark);
	ec.end_mark = ec.start_mark;
	fy_error_report(fyp, &ec, "version directive missing minor number");
	goto err_out;
}

int fy_scan_tag_handle(struct fy_parser *fyp, bool is_directive,
		struct fy_atom *handle)
{
	int length;

	length = fy_scan_tag_handle_length(fyp, 0);
	fy_error_check(fyp, length > 0, err_out,
			"fy_scan_tag_handle_length() failed");

	fy_fill_atom(fyp, length, handle);

	return 0;

err_out:
	return -1;
}


int fy_scan_tag_uri(struct fy_parser *fyp, bool is_directive,
		struct fy_atom *handle)
{
	int length;
	bool is_valid;

	length = fy_scan_tag_uri_length(fyp, 0);
	fy_error_check(fyp, length > 0, err_out,
			"fy_scan_tag_uri_length() failed");

	is_valid = fy_scan_tag_uri_is_valid(fyp, 0, length);
	fy_error_check(fyp, is_valid, err_out,
			"tag URI is invalid");

	fy_fill_atom(fyp, length, handle);
	handle->style = FYAS_URI;	/* this is a URI, need to handle URI escapes */

	return 0;

err_out:
	return -1;
}

int fy_scan_yaml_version(struct fy_parser *fyp, struct fy_atom *handle)
{
	int c, length;

	memset(handle, 0, sizeof(*handle));

	/* skip white space */
	while (fy_is_ws(c = fy_parse_peek(fyp)))
		fy_advance(fyp, c);

	length = fy_scan_yaml_version_length(fyp);
	fy_error_check(fyp, length > 0, err_out,
			"fy_scan_yaml_version_length() failed");

	fy_fill_atom(fyp, length, handle);

	return 0;

err_out:
	return -1;
}

int fy_scan_directive(struct fy_parser *fyp)
{
	int c, advance, version_length, tag_length, uri_length;
	enum fy_token_type type = FYTT_NONE;
	struct fy_atom handle;
	struct fy_error_ctx ec;
	bool is_uri_valid;
	struct fy_token *fyt;

	if (!fy_strcmp(fyp, "YAML")) {
		advance = 4;
		type = FYTT_VERSION_DIRECTIVE;
	} else if (!fy_strcmp(fyp, "TAG")) {
		advance = 3;
		type = FYTT_TAG_DIRECTIVE;
	} else {
		fy_warning(fyp, "Unsupported directive; skipping");
		/* skip until linebreak */
		while ((c = fy_parse_peek(fyp)) != -1 && !fy_is_lb(c))
			fy_advance(fyp, c);
		/* skip over linebreak too */
		if (fy_is_lb(c))
			fy_advance(fyp, c);

		/* bump activity counter */
		fyp->token_activity_counter++;

		return 0;
	}

	fy_error_check(fyp, type != FYTT_NONE, err_out,
			"neither YAML|TAG found");

	/* advance */
	fy_advance_by(fyp, advance);

	/* the next must be space */
	c = fy_parse_peek(fyp);

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			fy_is_ws(c),
			err_no_space_after_directive);

	/* skip white space */
	while (fy_is_ws(c = fy_parse_peek(fyp)))
		fy_advance(fyp, c);

	fy_fill_atom_start(fyp, &handle);

	/* for version directive, parse it */
	if (type == FYTT_VERSION_DIRECTIVE) {

		version_length = fy_scan_yaml_version_length(fyp);
		fy_error_check(fyp, version_length > 0, err_out,
				"fy_scan_yaml_version_length() failed");

		fy_advance_by(fyp, version_length);

		fy_fill_atom_end(fyp, &handle);

		fyt = fy_token_queue(fyp, FYTT_VERSION_DIRECTIVE, &handle);
		fy_error_check(fyp, fyt, err_out,
				"fy_token_queue() failed");

	} else {

		tag_length = fy_scan_tag_handle_length(fyp, 0);
		fy_error_check(fyp, tag_length > 0, err_out,
				"fy_scan_tag_handle_length() failed");

		fy_advance_by(fyp, tag_length);

		c = fy_parse_peek(fyp);
		fy_error_check(fyp, fy_is_ws(c), err_out,
				"missing whitespace after TAG");

		/* skip white space */
		while (fy_is_ws(c = fy_parse_peek(fyp)))
			fy_advance(fyp, c);

		uri_length = fy_scan_tag_uri_length(fyp, 0);
		fy_error_check(fyp, uri_length > 0, err_out,
				"fy_scan_tag_uri_length() failed");

		is_uri_valid = fy_scan_tag_uri_is_valid(fyp, 0, uri_length);
		fy_error_check(fyp, is_uri_valid, err_out,
				"tag URI is invalid");

		fy_advance_by(fyp, uri_length);

		fy_fill_atom_end(fyp, &handle);
		handle.style = FYAS_URI;

		c = fy_parse_peek(fyp);

		FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
				fy_is_ws(c) || fy_is_lb(c),
				err_garbage_trailing_tag);
		fy_advance(fyp, c);

		fyt = fy_token_queue(fyp, FYTT_TAG_DIRECTIVE, &handle, tag_length, uri_length);
		fy_error_check(fyp, fyt, err_out,
				"fy_token_queue() failed");
	}

	return 0;
err_out:
	return -1;

err_no_space_after_directive:
	fy_error_report(fyp, &ec, "missing space in %s directive",
			type == FYTT_VERSION_DIRECTIVE ? "YAML" : "TAG");
	goto err_out;

err_garbage_trailing_tag:
	fy_error_report(fyp, &ec, "garbage after trailing tag directive");
	goto err_out;
}

int fy_fetch_directive(struct fy_parser *fyp)
{
	int rc;

	fy_remove_all_simple_keys(fyp);

	rc = fy_parse_unroll_indent(fyp, -1);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_parse_unroll_indent() failed");

	rc = fy_scan_directive(fyp);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_scan_directive() failed");

	return 0;

err_out_rc:
	return rc;
}

int fy_fetch_document_indicator(struct fy_parser *fyp, enum fy_token_type type)
{
	int rc, c;
	struct fy_token *fyt;

	fy_remove_all_simple_keys(fyp);

	rc = fy_parse_unroll_indent(fyp, -1);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_parse_unroll_indent() failed");

	fyp->simple_key_allowed = false;
	fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");

	fyt = fy_token_queue(fyp, type, fy_fill_atom_a(fyp, 3));
	fy_error_check(fyp, fyt, err_out_rc,
			"fy_token_queue() failed");

	/* skip whitespace after the indicator */
	while (fy_is_ws(c = fy_parse_peek(fyp)))
		fy_advance(fyp, c);

	return 0;

err_out_rc:
	return rc;
}

int fy_fetch_flow_collection_mark_start(struct fy_parser *fyp, int c)
{
	enum fy_token_type type;
	struct fy_simple_key_mark skm;
	struct fy_error_ctx ec;
	int rc = -1;
	struct fy_token *fyt;

	type = c == '[' ? FYTT_FLOW_SEQUENCE_START :
				FYTT_FLOW_MAPPING_START;

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			!fyp->flow_level || fyp->column > fyp->indent,
			err_wrongly_indented_flow);

	fy_get_simple_key_mark(fyp, &skm);

	fyt = fy_token_queue(fyp, type, fy_fill_atom_a(fyp, 1));
	fy_error_check(fyp, fyt, err_out_rc,
			"fy_token_queue() failed");

	rc = fy_save_simple_key_mark(fyp, &skm, type, NULL);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_save_simple_key_mark() failed");

	/* increase flow level */
	fyp->flow_level++;
	fy_error_check(fyp, fyp->flow_level, err_out,
			"overflow for the flow level counter");

	/* push the current flow to the stack */
	rc = fy_parse_flow_push(fyp);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_parse_flow_push() failed");
	/* set the current flow mode */
	fyp->flow = c == '[' ? FYFT_SEQUENCE : FYFT_MAP;

	fyp->simple_key_allowed = true;
	fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");

	/* the comment indicator must have at least a space */
	c = fy_parse_peek(fyp);

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			c != '#',
			err_bad_comment);
	return 0;

err_out:
	rc = -1;

err_out_rc:
	return rc;

err_bad_comment:
	fy_error_report(fyp, &ec, "invalid comment after %s start",
			type == FYTT_FLOW_SEQUENCE_START ? "sequence" : "mapping");
	goto err_out;

err_wrongly_indented_flow:
	fy_error_report(fyp, &ec, "wrongly indented %s start in flow mode",
			type == FYTT_FLOW_SEQUENCE_START ? "sequence" : "mapping");
	goto err_out;
}

int fy_fetch_flow_collection_mark_end(struct fy_parser *fyp, int c)
{
	enum fy_token_type type = FYTT_NONE;
	enum fy_flow_type flow;
	struct fy_error_ctx ec;
	int i, rc;
	bool did_purge;
	struct fy_mark mark;
	struct fy_token *fyt;

	fy_get_mark(fyp, &mark);

	flow = c == ']' ? FYFT_SEQUENCE : FYFT_MAP;
	type = c == ']' ? FYTT_FLOW_SEQUENCE_END :
				FYTT_FLOW_MAPPING_END;

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			!fyp->flow_level || fyp->column > fyp->indent,
			err_wrongly_indented_flow);

	rc = fy_remove_simple_key(fyp, type);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_remove_simple_key() failed");

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			fyp->flow_level,
			err_bad_flow_collection_end);
	fyp->flow_level--;

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			fyp->flow == flow,
			err_mismatched_flow_collection_end);

	/* pop the flow type */
	rc = fy_parse_flow_pop(fyp);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_parse_flow_pop() failed");

	fyp->simple_key_allowed = false;
	fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");

	fyt = fy_token_queue(fyp, type, fy_fill_atom_a(fyp, 1));
	fy_error_check(fyp, fyt, err_out_rc,
			"fy_token_queue() failed");

	/* the comment indicator must have at least a space */
	c = fy_parse_peek(fyp);

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			c != '#',
			err_bad_comment);

	/* due to the weirdness with simple keys and multiline flow keys scan forward
	* until a linebreak, ';', or anything else */
	for (i = 0; ; i++) {
		c = fy_parse_peek_at(fyp, i);
		if (c < 0 || c == ':' || fy_is_lb(c) || !fy_is_ws(c))
			break;
	}

	/* we must be a key, purge */
	if (c == ':') {
		rc = fy_purge_stale_simple_keys(fyp, &did_purge, type);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_purge_stale_simple_keys() failed");

		/* if we did purge and the the list is now empty, we're hosed */
		FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
				!did_purge || !fy_simple_key_list_empty(&fyp->simple_keys),
				err_multiline_key);
	}

	return 0;

err_out:
	rc = -1;
err_out_rc:
	return rc;

err_bad_flow_collection_end:
	if (c == ']') {
		fy_error_report(fyp, &ec, "flow sequence with invalid extra closing bracket");
		goto err_out;
	} else if (c == '}') {
		fy_error_report(fyp, &ec, "flow mapping with invalid extra closing brace");
		goto err_out;
	}
	fy_error_report(fyp, &ec, "bad flow collection end");
	goto err_out;

err_bad_comment:
	fy_error_report(fyp, &ec, "invalid comment after end of flow %s",
				type == FYTT_FLOW_SEQUENCE_END ?
				"sequence" : "mapping");
	goto err_out;

err_multiline_key:
	ec.start_mark = ec.end_mark = mark;
	fy_error_report(fyp, &ec, "invalid multiline flow %s key ",
				type == FYTT_FLOW_SEQUENCE_END ?
				"sequence" : "mapping");
	goto err_out;

err_mismatched_flow_collection_end:
	fy_error_report(fyp, &ec, "mismatched flow %s end",
				type == FYTT_FLOW_SEQUENCE_END ?
				"mapping" : "sequence");
	goto err_out;

err_wrongly_indented_flow:
	fy_error_report(fyp, &ec, "wrongly indented %s end in flow mode",
			type == FYTT_FLOW_SEQUENCE_END ? "sequence" : "mapping");
	goto err_out;
}

int fy_fetch_flow_collection_entry(struct fy_parser *fyp, int c)
{
	enum fy_token_type type = FYTT_NONE;
	struct fy_error_ctx ec;
	int rc;
	struct fy_token *fyt, *fyt_last;

	type = FYTT_FLOW_ENTRY;

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			!fyp->flow_level || fyp->column > fyp->indent,
			err_wrongly_indented_flow);

	rc = fy_remove_simple_key(fyp, type);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_remove_simple_key() failed");

	fyp->simple_key_allowed = true;
	fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");

	fyt_last = fy_token_list_tail(&fyp->queued_tokens);
	fyt = fy_token_queue(fyp, type, fy_fill_atom_a(fyp, 1));
	fy_error_check(fyp, fyt, err_out_rc,
			"fy_token_queue() failed");

	/* the comment indicator must have at least a space */
	c = fy_parse_peek(fyp);

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			c != '#',
			err_bad_comment);

	/* skip white space */
	while (fy_is_ws(c = fy_parse_peek(fyp)))
		fy_advance(fyp, c);

	if (c == '#') {
		if (fyt_last)
			fyt = fyt_last;

		fy_notice(fyp, "attaching to token: %s", fy_token_debug_text_a(fyt));

		rc = fy_scan_comment(fyp, &fyt->comment[fycp_right], true);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_scan_comment() failed");

		fy_notice(fyp, "attaching comment:\n%s\n",
				fy_atom_get_text_a(&fyt->comment[fycp_right]));
	}

	return 0;
err_out:
	rc = -1;
err_out_rc:
	fy_error_report(fyp, &ec, "invalid comment after comma");
	return rc;

err_bad_comment:
	goto err_out;

err_wrongly_indented_flow:
	fy_error_report(fyp, &ec, "wrongly indented entry seperator in flow mode");
	goto err_out;
}

int fy_fetch_block_entry(struct fy_parser *fyp, int c)
{
	int rc;
	struct fy_mark mark;
	struct fy_error_ctx ec;
	struct fy_simple_key *fysk;
	struct fy_token *fyt;

	fy_error_check(fyp, c == '-', err_out,
			"illegal block entry");

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			!fyp->flow_level || (fyp->column + 2) > fyp->indent,
			err_wrongly_indented_flow);

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
		fyp->flow_level || fyp->simple_key_allowed,
		err_illegal_block_sequence);

	/* we have to save the start mark */
	fy_get_mark(fyp, &mark);

	if (!fyp->flow_level && fyp->indent < fyp->column) {

		/* push the new indent level */
		rc = fy_push_indent(fyp, fyp->column, false);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_push_indent() failed");

		fyt = fy_token_queue_internal(fyp, &fyp->queued_tokens,
				FYTT_BLOCK_SEQUENCE_START, fy_fill_atom_a(fyp, 0));
		fy_error_check(fyp, fyt, err_out_rc,
				"fy_token_queue_internal() failed");
	}

	if (c == '-' && fyp->flow_level) {
		/* this is an error, but we let the parser catch it */
		;
	}

	fysk = fy_would_remove_required_simple_key(fyp);

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
		!fysk,
		err_would_remove_required_simple_key);

	rc = fy_remove_simple_key(fyp, FYTT_BLOCK_ENTRY);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_remove_simple_key() failed");

	fyp->simple_key_allowed = true;
	fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");

	fyt = fy_token_queue(fyp, FYTT_BLOCK_ENTRY, fy_fill_atom_a(fyp, 1));
	fy_error_check(fyp, fyt, err_out_rc,
			"fy_token_queue() failed");

	/* special case for allowing whitespace (including tabs) after - */
	if (fy_is_ws(c = fy_parse_peek(fyp)))
		fy_advance(fyp, c);

	return 0;

err_out:
	rc = -1;
err_out_rc:
	return rc;

err_illegal_block_sequence:
	if (!fyp->simple_key_allowed && fyp->state == FYPS_BLOCK_MAPPING_VALUE) {
		ec.module = FYEM_PARSE;
		fy_error_report(fyp, &ec,
				"block sequence on the same line as a mapping key");
		goto err_out;
	}

	if (fyp->state == FYPS_BLOCK_SEQUENCE_FIRST_ENTRY || fyp->state == FYPS_BLOCK_SEQUENCE_ENTRY) {
		ec.module = FYEM_PARSE;
		fy_error_report(fyp, &ec,
				"block sequence on the same line as a previous item");
		goto err_out;
	}

	fy_error_report(fyp, &ec,
			"block sequence entries not allowed in this context");
	goto err_out;

err_would_remove_required_simple_key:
	if (fysk->token) {
		ec.start_mark = *fy_token_start_mark(fysk->token);
		ec.end_mark = *fy_token_end_mark(fysk->token);
	}

	if (fysk->token && fysk->token->type == FYTT_ANCHOR) {
		fy_error_report(fyp, &ec, "invalid anchor indent for sequence");
		goto err_out;
	}
	if (fysk->token && fysk->token->type == FYTT_TAG) {
		fy_error_report(fyp, &ec, "invalid tag indent for sequence");
		goto err_out;
	}
	fy_error_report(fyp, &ec, "missing ':'");
	goto err_out;

err_wrongly_indented_flow:
	fy_error_report(fyp, &ec, "wrongly indented block sequence in flow mode");
	goto err_out;
}

int fy_fetch_key(struct fy_parser *fyp, int c)
{
	int rc;
	struct fy_mark mark;
	struct fy_simple_key_mark skm;
	bool target_simple_key_allowed;
	struct fy_error_ctx ec;
	struct fy_token *fyt;

	fy_error_check(fyp, c == '?', err_out,
			"illegal block entry or key mark");

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			!fyp->flow_level || fyp->column > fyp->indent,
			err_wrongly_indented_flow);

	fy_get_simple_key_mark(fyp, &skm);

	/* we have to save the start mark */
	fy_get_mark(fyp, &mark);

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			fyp->flow_level || fyp->simple_key_allowed,
			err_invalid_context);

	if (!fyp->flow_level && fyp->indent < fyp->column) {

		/* push the new indent level */
		rc = fy_push_indent(fyp, fyp->column, true);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_push_indent() failed");

		fyt = fy_token_queue_internal(fyp, &fyp->queued_tokens,
				FYTT_BLOCK_MAPPING_START, fy_fill_atom_a(fyp, 0));
		fy_error_check(fyp, fyt, err_out_rc,
				"fy_token_queue_internal() failed");
	}

	rc = fy_remove_simple_key(fyp, FYTT_KEY);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_remove_simple_key() failed");

	target_simple_key_allowed = !fyp->flow_level;

	fyp->pending_complex_key_column = fyp->column;
	fyp->pending_complex_key_mark = mark;
	fy_scan_debug(fyp, "pending_complex_key_column %d",
			fyp->pending_complex_key_column);

	fyt = fy_token_queue(fyp, FYTT_KEY, fy_fill_atom_a(fyp, 1));
	fy_error_check(fyp, fyt, err_out_rc,
			"fy_token_queue() failed");

	fyp->simple_key_allowed = target_simple_key_allowed;
	fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");

	/* eat whitespace */
	while (fy_is_blank(c = fy_parse_peek(fyp)))
		fy_advance(fyp, c);

	/* comment? */
	if (c == '#') {
		rc = fy_scan_comment(fyp, &fyt->comment[fycp_right], false);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_scan_comment() failed");
	}

	return 0;

err_out:
	rc = -1;
err_out_rc:
	return rc;

err_invalid_context:
	fy_error_report(fyp, &ec, "invalid mapping key (not allowed in this context)");
	goto err_out;

err_wrongly_indented_flow:
	fy_error_report(fyp, &ec, "wrongly indented mapping key in flow mode");
	goto err_out;
}

int fy_fetch_value(struct fy_parser *fyp, int c)
{
	struct fy_token_list sk_tl;
	struct fy_simple_key *fysk = NULL;
	struct fy_mark mark, mark_insert, mark_end_insert;
	struct fy_token *fyt_insert, *fyt;
	struct fy_atom handle;
	bool target_simple_key_allowed, is_complex, has_bmap;
	bool push_bmap_start, push_key_only, did_purge, final_complex_key;
	bool is_multiline __FY_DEBUG_UNUSED__;
	struct fy_error_ctx ec;
	int rc;

	fy_error_check(fyp, c == ':', err_out,
		"illegal value mark");

	fy_get_mark(fyp, &mark);

	fy_token_list_init(&sk_tl);

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			!fyp->flow_level || fyp->column > fyp->indent,
			err_wrongly_indented_flow);

	rc = fy_purge_stale_simple_keys(fyp, &did_purge, FYTT_VALUE);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_purge_stale_simple_keys() failed");

	/* get the simple key (if available) for the value */
	fysk = fy_simple_key_list_head(&fyp->simple_keys);
	if (fysk && fysk->flow_level == fyp->flow_level)
		fy_simple_key_list_del(&fyp->simple_keys, fysk);
	else
		fysk = NULL;

	if (!fysk) {
		fy_scan_debug(fyp, "no simple key flow_level=%d", fyp->flow_level);

		fyt_insert = fy_token_list_tail(&fyp->queued_tokens);
		mark_insert = mark;
		mark_end_insert = mark;
	} else {
		assert(fysk->possible);
		assert(fysk->flow_level == fyp->flow_level);
		fyt_insert = fysk->token;
		mark_insert = fysk->mark;
		mark_end_insert = fysk->end_mark;

		fy_scan_debug(fyp, "have simple key flow_level=%d", fyp->flow_level);
	}

	fy_scan_debug(fyp, "flow_level=%d, column=%d parse_indent=%d",
			fyp->flow_level, mark_insert.column, fyp->indent);

	is_complex = fyp->pending_complex_key_column >= 0;
	final_complex_key = is_complex && (fyp->flow_level || fyp->column <= fyp->pending_complex_key_mark.column);
	is_multiline = mark_end_insert.line < fyp->line;
	has_bmap = fyp->generated_block_map;
	push_bmap_start = (!fyp->flow_level && mark_insert.column > fyp->indent);
	push_key_only = (!is_complex && (fyp->flow_level || has_bmap)) ||
		        (is_complex && !final_complex_key);

	fy_scan_debug(fyp, "mark_insert.line=%d/%d mark_end_insert.line=%d/%d fyp->line=%d",
			mark_insert.line, mark_insert.column,
			mark_end_insert.line, mark_end_insert.column,
			fyp->line);

	fy_scan_debug(fyp, "simple_key_allowed=%s is_complex=%s final_complex_key=%s is_multiline=%s has_bmap=%s push_bmap_start=%s push_key_only=%s",
			fyp->simple_key_allowed ? "true" : "false",
			is_complex ? "true" : "false",
			final_complex_key ? "true" : "false",
			is_multiline ? "true" : "false",
			has_bmap ? "true" : "false",
			push_bmap_start ? "true" : "false",
			push_key_only ? "true" : "false");

	fy_error_check(fyp, !(!is_complex && is_multiline && (!fyp->flow_level || fyp->flow != FYFT_MAP)),
			err_out, "Illegal placement of ':' indicator");

	if (push_bmap_start) {

		assert(!fyp->flow_level);

		fy_scan_debug(fyp, "--- parse_roll");

		/* push the new indent level */
		rc = fy_push_indent(fyp, mark_insert.column, true);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_push_indent() failed");

		fy_fill_atom_start(fyp, &handle);
		fy_fill_atom_end(fyp, &handle);

		handle.start_mark = handle.end_mark = mark_insert;

		/* and the block mapping start */
		fyt = fy_token_queue_internal(fyp, &sk_tl, FYTT_BLOCK_MAPPING_START, &handle);
		fy_error_check(fyp, fyt, err_out_rc,
				"fy_token_queue_internal() failed");
	}

	if (push_bmap_start || push_key_only) {

		fyt = fy_token_queue_internal(fyp, &sk_tl, FYTT_KEY, fy_fill_atom_a(fyp, 0));
		fy_error_check(fyp, fyt, err_out_rc,
				"fy_token_queue_internal() failed");

	}

	fy_debug_dump_token(fyp, fyt_insert, "insert-token: ");
	fy_debug_dump_token_list(fyp, &fyp->queued_tokens, fyt_insert, "fyp->queued_tokens (before): ");
	fy_debug_dump_token_list(fyp, &sk_tl, NULL, "sk_tl: ");

	if (fyt_insert) {

		if (fysk)
			fy_token_list_splice_before(&fyp->queued_tokens, fyt_insert, &sk_tl);
		else
			fy_token_list_splice_after(&fyp->queued_tokens, fyt_insert, &sk_tl);
	} else
		fy_token_lists_splice(&fyp->queued_tokens, &sk_tl);

	fy_debug_dump_token_list(fyp, &fyp->queued_tokens, fyt_insert, "fyp->queued_tokens (after): ");

	target_simple_key_allowed = fysk ? false : !fyp->flow_level;

	fyt = fy_token_queue(fyp, FYTT_VALUE, fy_fill_atom_a(fyp, 1));
	fy_error_check(fyp, fyt, err_out_rc,
			"fy_token_queue() failed");

	fyp->simple_key_allowed = target_simple_key_allowed;
	fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");

	if (fysk)
		fy_parse_simple_key_recycle(fyp, fysk);

	if (final_complex_key) {
		fyp->pending_complex_key_column = -1;
		fy_scan_debug(fyp, "pending_complex_key_column -> %d",
				fyp->pending_complex_key_column);
	}

	if (fyt_insert) {
		/* eat whitespace */
		while (fy_is_blank(c = fy_parse_peek(fyp)))
			fy_advance(fyp, c);

		/* comment? */
		if (c == '#') {
			rc = fy_scan_comment(fyp, &fyt_insert->comment[fycp_right], false);
			fy_error_check(fyp, !rc, err_out_rc,
					"fy_scan_comment() failed");

			fy_notice(fyp, "token: %s attaching right comment:\n%s\n",
					fy_token_debug_text_a(fyt_insert),
					fy_atom_get_text_a(&fyt_insert->comment[fycp_right]));
		}
	}

	return 0;

err_out:
	rc = -1;
err_out_rc:
	fy_parse_simple_key_recycle(fyp, fysk);
	return rc;

err_wrongly_indented_flow:
	fy_error_report(fyp, &ec, "wrongly indented mapping value in flow mode");
	goto err_out;
}

int fy_fetch_anchor_or_alias(struct fy_parser *fyp, int c)
{
	struct fy_atom handle;
	enum fy_token_type type;
	int i = 0, rc = -1, length;
	struct fy_simple_key_mark skm;
	struct fy_error_ctx ec;
	struct fy_token *fyt;

	fy_error_check(fyp, c == '*' || c == '&', err_out,
			"illegal anchor mark (not '*' or '&')");

	type = c == '*' ? FYTT_ALIAS : FYTT_ANCHOR;

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			!fyp->flow_level || fyp->column > fyp->indent,
			err_wrongly_indented_flow);

	/* we have to save the start mark (including the anchor/alias start) */
	fy_get_simple_key_mark(fyp, &skm);

	/* skip over the anchor mark */
	fy_advance(fyp, c);

	/* start mark */
	fy_fill_atom_start(fyp, &handle);

	length = 0;

	while ((c = fy_parse_peek(fyp)) >= 0) {
		if (fy_is_blankz(c) || fy_utf8_strchr("[]{},", c))
			break;
		fy_advance(fyp, c);
		length++;
	}

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			length > 0,
			err_bad_anchor_or_alias);

	fy_fill_atom_end(fyp, &handle);

	fyt = fy_token_queue(fyp, type, &handle);
	fy_error_check(fyp, fyt, err_out_rc,
			"fy_token_queue() failed");

	/* scan forward for '-' block sequence indicator */
	if (type == FYTT_ANCHOR && !fyp->flow_level) {
		for (i = 0; ; i++) {
			c = fy_parse_peek_at(fyp, i);
			if (c < 0 || fy_is_lb(c) || !fy_is_ws(c))
				break;
		}

		/* if it's '-' we have a problem */
		FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
				c != '-',
				err_block_seq_entry_after_anchor);
	}


	rc = fy_save_simple_key_mark(fyp, &skm, type, NULL);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_save_simple_key_mark() failed");

	fyp->simple_key_allowed = false;
	fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");

	return 0;

err_out:
	rc = -1;
err_out_rc:
	return rc;

err_block_seq_entry_after_anchor:
	fy_advance_by(fyp, i);
	fy_get_mark(fyp, &ec.start_mark);
	ec.end_mark = ec.start_mark;
	fy_error_report(fyp, &ec, "illegal block sequence on the same line as anchor");
	goto err_out;

err_bad_anchor_or_alias:
	fy_error_report(fyp, &ec, "invalid %s detected",
			type == FYTT_ALIAS ? "alias" : "anchor");
	goto err_out;

err_wrongly_indented_flow:
	fy_error_report(fyp, &ec, "wrongly indented %s in flow mode",
			type == FYTT_ALIAS ? "alias" : "anchor");
	goto err_out;
}

int fy_fetch_tag(struct fy_parser *fyp, int c)
{
	struct fy_atom handle;
	int rc = -1, total_length, handle_length, uri_length, i, prefix_length, suffix_length;
	const char *handlep;
	bool is_valid;
	struct fy_simple_key_mark skm;
	struct fy_document_state *fyds;
	struct fy_token *fyt_td;
	struct fy_error_ctx ec;
	struct fy_token *fyt;

	fy_error_check(fyp, c == '!', err_out,
			"illegal tag mark (not '!')");

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			!fyp->flow_level || fyp->column > fyp->indent,
			err_wrongly_indented_flow);

	fyds = fyp->current_document_state;

	fy_get_simple_key_mark(fyp, &skm);

	if (fy_parse_peek_at(fyp, 1) == '<') {
		/* skip over '!<' and '>' */
		prefix_length = 2;
		suffix_length = 1;
	} else
		prefix_length = suffix_length = 0;

	if (prefix_length)
		handle_length = 0; /* set the handle to '' */
	else {
		/* either !suffix or !handle!suffix */
		/* we scan back to back, and split handle/suffix */
		handle_length = fy_scan_tag_handle_length(fyp, prefix_length);
		fy_error_check(fyp, handle_length > 0, err_out,
				"fy_scan_tag_handle_length() failed");
	}

	uri_length = fy_scan_tag_uri_length(fyp, prefix_length + handle_length);
	fy_error_check(fyp, uri_length >= 0, err_out,
			"fy_scan_tag_uri_length() failed");

	/* a handle? */
	if (!prefix_length && (handle_length == 0 || fy_parse_peek_at(fyp, handle_length - 1) != '!')) {
		/* special case, '!', handle set to '' and suffix to '!' */
		if (handle_length == 1 && uri_length == 0) {
			handle_length = 0;
			uri_length = 1;
		} else {
			uri_length = handle_length - 1 + uri_length;
			handle_length = 1;
		}
	}

	is_valid = fy_scan_tag_uri_is_valid(fyp, prefix_length + handle_length, uri_length);
	fy_error_check(fyp, is_valid, err_out,
			"tag URI is invalid");

	if (suffix_length > 0) {
		c = fy_parse_peek_at(fyp, prefix_length + handle_length + uri_length);

		FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
				c == '>',
				err_bad_uri_missing_end);
	}

	total_length = prefix_length + handle_length + uri_length + suffix_length;
	fy_fill_atom(fyp, total_length, &handle);
	handle.style = FYAS_URI;	/* this is a URI, need to handle URI escapes */

	c = fy_parse_peek(fyp);

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			fy_is_blankz(c) || fy_utf8_strchr(",}]", c),
			err_bad_tag_end);

	handlep = fy_atom_data(&handle) + prefix_length;

	fyt_td = fy_document_state_lookup_tag_directive(fyds, handlep, handle_length);
	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_PARSE,
			fyt_td,
			err_undefined_tag_prefix);

	fyt = fy_token_queue(fyp, FYTT_TAG, &handle, prefix_length, handle_length, uri_length, fyt_td);
	fy_error_check(fyp, fyt, err_out_rc,
			"fy_token_queue() failed");

	/* scan forward for '-' block sequence indicator */
	if (!fyp->flow_level) {
		for (i = 0; ; i++) {
			c = fy_parse_peek_at(fyp, i);
			if (c < 0 || fy_is_lb(c) || !fy_is_ws(c))
				break;
		}

		/* if it's '-' we have a problem */
		FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
				c != '-',
				err_block_seq_entry_after_tag);
	}

	rc = fy_save_simple_key_mark(fyp, &skm, FYTT_TAG, NULL);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_save_simple_key_mark() failed");

	fyp->simple_key_allowed = false;
	fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");

	return 0;

err_out:
	rc = -1;
err_out_rc:
	return rc;

err_bad_tag_end:
	fy_error_report(fyp, &ec, "invalid tag terminator");
	goto err_out;

err_block_seq_entry_after_tag:
	fy_advance_by(fyp, i);
	fy_get_mark(fyp, &ec.start_mark);
	ec.end_mark = ec.start_mark;
	fy_error_report(fyp, &ec, "illegal block sequence on the same line as the tag");
	goto err_out;

err_bad_uri_missing_end:
	fy_error_report(fyp, &ec, "missing '>' uri terminator");
	goto err_out;

err_wrongly_indented_flow:
	fy_error_report(fyp, &ec, "wrongly indented tag in flow mode");
	goto err_out;

err_undefined_tag_prefix:
	ec.start_mark = handle.start_mark;
	ec.end_mark = handle.end_mark;
	fy_error_report(fyp, &ec, "undefined tag prefix");
	goto err_out;
}

int fy_scan_block_scalar_indent(struct fy_parser *fyp, int indent, int *breaks)
{
	int c, max_indent = 0, min_indent;
	struct fy_error_ctx ec;

	*breaks = 0;

	/* minimum indent is 0 for zero indent scalars */
	min_indent = fyp->document_first_content_token ? 0 : 1;

	/* scan over the indentation spaces */
	/* we don't format content for display */
	for (;;) {
		/* skip over indentation */
		while ((c = fy_parse_peek(fyp)) == ' ' &&
			(!indent || fyp->column < indent))
			fy_advance(fyp, c);

		if (fyp->column > max_indent)
			max_indent = fyp->column;

		FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
				c != '\t' || !(!indent && fyp->column < indent),
				err_invalid_tab_as_indentation);

		/* non-empty line? */
		if (!fy_is_break(c))
			break;

		fy_advance(fyp, c);

		(*breaks)++;
	}

	if (!indent) {
		indent = max_indent;
		if (indent < fyp->indent)
			indent = fyp->indent;
		if (indent < min_indent)
			indent = min_indent;
	}

	return indent;

err_out:
	return -1;

err_invalid_tab_as_indentation:
	fy_error_report(fyp, &ec, "invalid tab character as indent instead of space");
	goto err_out;
}

int fy_fetch_block_scalar(struct fy_parser *fyp, bool is_literal, int c)
{
	struct fy_atom handle;
	enum fy_atom_chomp chomp = FYAC_CLIP;	/* default */
	int rc, increment = 0, current_indent, new_indent, indent = 0, breaks, prev_breaks;
	bool doc_end_detected, empty, empty_line, prev_empty_line, needs_sep, indented, prev_indented, first;
	struct fy_error_ctx ec;
	struct fy_token *fyt;
	size_t length, line_length, trailing_ws, trailing_breaks_length;
	size_t leading_ws, prev_leading_ws;
	size_t prefix_length, suffix_length;

	fy_error_check(fyp, c == '|' || c == '>', err_out,
			"bad start of block scalar ('%s')",
				fy_utf8_format_a(c, fyue_singlequote));

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			!fyp->flow_level || fyp->column > fyp->indent,
			err_wrongly_indented_flow);

	rc = fy_remove_simple_key(fyp, FYTT_SCALAR);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_remove_simple_key() failed");

	fyp->simple_key_allowed = true;
	fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");

	/* skip over block scalar start */
	fy_advance(fyp, c);

	/* intentation indicator (either [-+]<digit> or <digit>[-+] */
	c = fy_parse_peek(fyp);
	if (c == '+' || c == '-') {

		chomp = c == '+' ? FYAC_KEEP : FYAC_STRIP;

		fy_advance(fyp, c);

		c = fy_parse_peek(fyp);
		if (fy_is_num(c)) {
			increment = c - '0';
			fy_error_check(fyp, increment != 0, err_out,
					"indentation indicator 0");
			fy_advance(fyp, c);
		}
	} else if (fy_is_num(c)) {

		increment = c - '0';
		fy_error_check(fyp, increment != 0, err_out,
				"indentation indicator 0");
		fy_advance(fyp, c);

		c = fy_parse_peek(fyp);
		if (c == '+' || c == '-') {
			chomp = c == '+' ? FYAC_KEEP : FYAC_STRIP;
			fy_advance(fyp, c);
		}
	}

	/* the comment indicator must have at least a space */
	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			c != '#',
			err_bad_comment);

	/* eat whitespace */
	while (fy_is_blank(c = fy_parse_peek(fyp)))
		fy_advance(fyp, c);

	/* comment? */
	if (c == '#') {
		struct fy_atom comment;

		rc = fy_scan_comment(fyp, &comment, true);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_scan_comment() failed");
	}

	c = fy_parse_peek(fyp);

	/* end of the line? */
	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			fy_is_breakz(c),
			err_no_lb_found);

	/* advance */
	fy_advance(fyp, c);

	fy_fill_atom_start(fyp, &handle);

	if (increment) {
		current_indent = fyp->indent;
		indent = current_indent >= 0 ? current_indent + increment : increment;
	}

	length = 0;
	trailing_breaks_length = 0;

	empty = true;

	new_indent = fy_scan_block_scalar_indent(fyp, indent, &breaks);
	fy_error_check(fyp, new_indent >= 0, err_out,
			"fy_scan_block_scalar_indent() failed");

	length = breaks;

	indent = new_indent;

	doc_end_detected = false;
	prev_breaks = 0;
	prev_empty_line = false;
	prev_leading_ws = 0;

	prefix_length = 0;
	suffix_length = 0;

	needs_sep = false;
	prev_indented = false;
	first = true;

	while ((c = fy_parse_peek(fyp)) > 0 && fyp->column >= indent) {

		/* consume the list */
		line_length = 0;
		trailing_ws = 0;
		empty_line = true;
		leading_ws = 0;

		indented = fy_is_ws(fy_parse_peek(fyp));

		while (!(fy_is_breakz(c = fy_parse_peek(fyp)))) {
			if (fyp->column == 0 &&
			    !fy_strncmp(fyp, "...", 3) && fy_is_blankz_at_offset(fyp, 3)) {
				doc_end_detected = true;
				break;
			}

			if (!fy_is_space(c)) {
				empty = false;
				empty_line = false;
				trailing_ws = 0;
			} else {
				if (empty_line)
					leading_ws++;
				trailing_ws++;
			}

			fy_advance(fyp, c);

			line_length += fy_utf8_width(c);
		}

		if (indented && empty_line)
			indented = false;

		FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
				c >= 0,
				err_eof_found);

		if (doc_end_detected)
			break;

		/* eat line break */
		fy_advance(fyp, c);

		new_indent = fy_scan_block_scalar_indent(fyp, indent, &breaks);
		fy_error_check(fyp, new_indent >= 0, err_out,
				"fy_scan_block_scalar_indent() failed");

		if (is_literal) {
			if (!empty_line) {
				prefix_length = trailing_breaks_length;
				trailing_breaks_length = 0;
			}

			suffix_length = 1;
			trailing_breaks_length += breaks;
		} else {

			if (!empty_line) {
				prefix_length += trailing_breaks_length;
				trailing_breaks_length = 0;
			}

			if (!empty_line && !indented) {
				if (!first && needs_sep && !prev_breaks)
					prefix_length++;
			} else if (indented) {
				if (!first && (!prev_indented || prev_breaks > 0))
					prefix_length++;
			}

			if (!empty_line && !indented) {
				needs_sep = !trailing_ws && breaks <= 0;
			} else if (!empty_line && indented) {
				if (prev_indented == 0 || prev_breaks == 0)
					prefix_length++;
				needs_sep = !trailing_ws && breaks < 0;
			} else if (empty_line) {
				if (prev_indented == 0)
					prefix_length++;
				suffix_length++;
				needs_sep = false;
			}

			trailing_breaks_length += breaks;
		}

		length += prefix_length + line_length + suffix_length;

		indent = new_indent;

		prev_empty_line = empty_line;
		prev_breaks = breaks;
		prev_leading_ws = leading_ws;
		prev_indented = indented;

		prefix_length = 0;
		suffix_length = 0;

		first = false;
	}

	if (empty) {
		trailing_breaks_length = breaks;
		length = 0;
	} else if (!is_literal) {
		if ((needs_sep || trailing_breaks_length) && !prev_indented)
			length++;
		else if (prev_empty_line && prev_leading_ws)
			length -= (prev_leading_ws + 1);
	}

	/* detect wrongly indented block scalar */
	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
		!empty || fyp->column <= fyp->indent || c == '#',
		err_wrongly_indented);

	/* end... */
	fy_fill_atom_end(fyp, &handle);

	switch (chomp) {
	case FYAC_CLIP:
		/* nothing */
		break;
	case FYAC_KEEP:
		length += trailing_breaks_length;
		break;
	case FYAC_STRIP:
		if (length > 0)
			length--;
		break;
	}

	/* need to process to present */
	handle.style = is_literal ? FYAS_LITERAL : FYAS_FOLDED;
	handle.chomp = chomp;
	handle.increment = increment;

	handle.storage_hint = length;
	handle.storage_hint_valid = true;
	handle.direct_output = handle.end_mark.line == handle.start_mark.line &&
				is_literal &&
				fy_atom_size(&handle) == length;
#ifdef ATOM_SIZE_CHECK
	length = fy_atom_format_internal(&handle, NULL, NULL);
	fy_error_check(fyp,
		length == handle.storage_hint,
		err_out, "storage hint calculation failed real %zu != hint %zu - \"%s\"",
		length, handle.storage_hint,
		fy_utf8_format_text_a(fy_atom_data(&handle), fy_atom_size(&handle), fyue_doublequote));
#endif

	fyt = fy_token_queue(fyp, FYTT_SCALAR, &handle, is_literal ? FYSS_LITERAL : FYSS_FOLDED);
	fy_error_check(fyp, fyt, err_out_rc,
			"fy_token_queue() failed");

	rc = fy_attach_comments_if_any(fyp, fyt);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_attach_right_hand_comment() failed");

	return 0;

err_out:
	rc = -1;
err_out_rc:
	return rc;

err_wrongly_indented:
	fy_error_report(fyp, &ec,
			"block scalar with wrongly indented line after spaces only");
	goto err_out;

err_bad_comment:
	fy_error_report(fyp, &ec, "invalid comment without whitespace after block scalar indicator");

	goto err_out;

err_no_lb_found:
	fy_error_report(fyp, &ec,
			"block scalar no linebreak found");
	goto err_out;

err_eof_found:
	fy_error_report(fyp, &ec,
			"unterminated block scalar until end of input");
	goto err_out;

err_wrongly_indented_flow:
	fy_error_report(fyp, &ec, "wrongly indented block scalar in flow mode");
	goto err_out;
}

int fy_fetch_flow_scalar(struct fy_parser *fyp, int c)
{
	struct fy_atom handle;
	size_t length;
	int rc = -1, code_length, i = 0, value, end_c, last_line;
	int breaks_found, blanks_found;
	bool is_single, is_multiline, is_complex, esc_lb;
	struct fy_simple_key_mark skm;
	struct fy_error_ctx ec;
	struct fy_mark mark;
	struct fy_token *fyt;

	is_single = c == '\'';
	end_c = c;

	fy_error_check(fyp, c == '\'' || c == '"', err_out,
			"bad start of flow scalar ('%s')",
				fy_utf8_format_a(c, fyue_singlequote));

	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			!fyp->flow_level || fyp->column > fyp->indent,
			err_wrongly_indented_flow);

	fy_get_mark(fyp, &mark);
	fy_get_simple_key_mark(fyp, &skm);

	/* skip over block scalar start */
	fy_advance(fyp, c);

	fy_fill_atom_start(fyp, &handle);

	length = 0;
	breaks_found = 0;
	blanks_found = 0;
	esc_lb = false;

	last_line = -1;
	for (;;) {
		/* no document indicators please */
		FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			!(fyp->column == 0 &&
				(!fy_strncmp(fyp, "---", 3) || !fy_strncmp(fyp, "...", 3)) &&
				fy_is_blankz_at_offset(fyp, 3)),
			err_document_indicator);

		/* no EOF either */
		c = fy_parse_peek(fyp);

		FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN, c > 0,
				err_end_of_stream);

		while (!fy_is_blankz(c = fy_parse_peek(fyp))) {

			esc_lb = false;
			/* track line change (and first non blank) */
			if (last_line != fyp->line) {
				last_line = fyp->line;

				FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
					fyp->column > fyp->indent,
					err_wrongly_indented);
			}

			if (breaks_found) {
				/* minimum 1 sep, or more for consecutive */
				length += breaks_found > 1 ? (breaks_found - 1) : 1;
				breaks_found = 0;
				blanks_found = 0;
			} else if (blanks_found) {
				length += blanks_found;
				blanks_found = 0;
			}

			/* escaped single quote? */
			if (is_single && c == '\'' && fy_parse_peek_at(fyp, 1) == '\'') {
				length++;
				fy_advance_by(fyp, 2);
				continue;
			}

			/* right quote? */
			if (c == end_c)
				break;

			/* escaped line break */
			if (!is_single && c == '\\' && fy_is_break(fy_parse_peek_at(fyp, 1))) {

				fy_advance_by(fyp, 2);

				esc_lb = true;
				c = fy_parse_peek(fyp);
				break;
			}

			/* escaped sequence? */
			if (!is_single && c == '\\') {

				/* note we don't generate formatted output */
				/* we are merely checking for validity */
				c = fy_parse_peek_at(fyp, 1);

				/* check if it's valid escape sequence */
				/* TODO verify that removing ' is correct */

				FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
					c > 0 && fy_utf8_strchr("0abt\tnvfre \"/\\N_LPxuU", c),
					err_invalid_escape);

				fy_advance_by(fyp, 2);

				/* hex, unicode marks */
				if (c == 'x' || c == 'u' || c == 'U') {
					code_length = c == 'x' ? 2 :
						c == 'u' ? 4 : 8;
					value = 0;
					for (i = 0; i < code_length; i++) {
						c = fy_parse_peek_at(fyp, i);


						FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
							fy_is_hex(c),
							err_invalid_hex_escape);

						value <<= 4;
						if (c >= '0' && c <= '9')
							value |= c - '0';
						else if (c >= 'a' && c <= 'f')
							value |= 10 + c - 'a';
						else
							value |= 10 + c - 'A';
					}

					/* check for validity */
					FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
						!(value < 0 || (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff),
						err_invalid_utf8_escape);

					fy_advance_by(fyp, code_length);

					length += fy_utf8_width(value);

				} else if (c == 'N' || c == '_') /* NEL, 0xa0 two bytes */
					length += 2;
				else if (c == 'L' || c == 'P')	/* LS, PS, 3 bytes */
					length += 3;
				else
					length++; /* all others single byte */
				continue;
			}

			/* regular character */
			fy_advance(fyp, c);

			length += fy_utf8_width(c);
		}

		/* end of scalar */
		if (c == end_c)
			break;

		/* consume blanks */
		breaks_found = 0;
		blanks_found = 0;
		while (fy_is_blank(c = fy_parse_peek(fyp)) || fy_is_break(c)) {
			fy_advance(fyp, c);

			if (fy_is_break(c)) {
				breaks_found++;
				blanks_found = 0;
				esc_lb = false;
			} else {
				if (!esc_lb)
					blanks_found++;
			}
		}
	}

	/* end... */
	fy_fill_atom_end(fyp, &handle);

	is_multiline = handle.end_mark.line > handle.start_mark.line;
	is_complex = fyp->pending_complex_key_column >= 0;

	/* need to process to present */
	handle.style = is_single ? FYAS_SINGLE_QUOTED : FYAS_DOUBLE_QUOTED;
	handle.storage_hint = length;
	handle.storage_hint_valid = true;
	handle.direct_output = !is_multiline && fy_atom_size(&handle) == length;

	/* skip over block scalar end */
	fy_advance_by(fyp, 1);

#ifdef ATOM_SIZE_CHECK
	length = fy_atom_format_internal(&handle, NULL, NULL);
	fy_error_check(fyp,
		length == handle.storage_hint,
		err_out, "storage hint calculation failed real %zu != hint %zu - \"%s\"",
		length, handle.storage_hint,
		fy_utf8_format_text_a(fy_atom_data(&handle), fy_atom_size(&handle), fyue_doublequote));
#endif

	/* and we're done */
	fyt = fy_token_queue(fyp, FYTT_SCALAR, &handle, is_single ? FYSS_SINGLE_QUOTED : FYSS_DOUBLE_QUOTED);
	fy_error_check(fyp, fyt, err_out_rc,
			"fy_token_queue() failed");

	if (!fyp->flow_level) {
		/* due to the weirdness with simple keys scan forward
		* until a linebreak, ';', or anything else */
		for (i = 0; ; i++) {
			c = fy_parse_peek_at(fyp, i);
			if (c < 0 || c == ':' || fy_is_lb(c) || !fy_is_ws(c))
				break;
		}

		if (is_multiline && !is_complex) {
			/* if we're a multiline key that's bad */
			FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
					c != ':', err_multiline_key);
		}

		FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
				c < 0 || c == ':' || c == '#' || fy_is_lb(c),
				err_trailing_content);
	}

	/* a plain scalar could be simple key */
	rc = fy_save_simple_key_mark(fyp, &skm, FYTT_SCALAR, &handle.end_mark);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_save_simple_key_mark() failed");

	/* cannot follow a flow scalar */
	fyp->simple_key_allowed = false;
	fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");

	/* make sure that no comment follows directly afterwards */
	c = fy_parse_peek(fyp);
	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			c != '#',
			err_bad_comment);

	rc = fy_attach_comments_if_any(fyp, fyt);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_attach_right_hand_comment() failed");

	return 0;

err_out:
	rc = -1;
err_out_rc:
	return rc;

err_invalid_escape:
	fy_advance_by(fyp, 2);
	fy_get_mark(fyp, &ec.end_mark);
	fy_error_report(fyp, &ec, "invalid escape '%s' in %s string",
			fy_utf8_format_a(c, fyue_singlequote),
			is_single ? "single-quoted" : "double-quoted");
	goto err_out;

err_document_indicator:
	fy_advance_by(fyp, 3);
	fy_get_mark(fyp, &ec.end_mark);
	fy_error_report(fyp, &ec, "invalid document-%s marker in %s string",
			c == '-' ? "start" : "end",
			is_single ? "single-quoted" : "double-quoted");
	goto err_out;

err_end_of_stream:
	ec.start_mark = mark;
	fy_error_report(fyp, &ec, "%s string without closing quote",
			is_single ? "single-quoted" : "double-quoted");
	goto err_out;

err_multiline_key:
	ec.start_mark = ec.end_mark = mark;
	fy_error_report(fyp, &ec, "invalid multiline %s string used as key",
				is_single ? "single-quoted" : "double-quoted");
	goto err_out;

err_bad_comment:
	fy_error_report(fyp, &ec, "invalid comment without whitespace after %s scalar",
				is_single ? "single-quoted" : "double-quoted");
	goto err_out;

err_trailing_content:
	fy_advance_by(fyp, i);
	fy_get_mark(fyp, &ec.start_mark);
	ec.end_mark = ec.start_mark;
	fy_error_report(fyp, &ec, "invalid trailing content after %s scalar",
				is_single ? "single-quoted" : "double-quoted");
	goto err_out;

err_wrongly_indented:
	fy_error_report(fyp, &ec, "wrongly indented %s scalar",
				is_single ? "single-quoted" : "double-quoted");
	goto err_out;

err_invalid_hex_escape:
	fy_advance_by(fyp, i);
	fy_error_report(fyp, &ec, "double-quoted scalar has invalid hex escape");
	goto err_out;

err_invalid_utf8_escape:
	fy_error_report(fyp, &ec, "double-quoted scalar has invalid UTF8 escape");
	goto err_out;

err_wrongly_indented_flow:
	fy_error_report(fyp, &ec, "wrongly indented %s scalar in flow mode",
				is_single ? "single-quoted" : "double-quoted");
	goto err_out;
}

int fy_fetch_plain_scalar(struct fy_parser *fyp, int c)
{
	struct fy_atom handle;
	size_t length;
	int rc = -1, indent, run, nextc, i, breaks_found, blanks_found;
	bool has_leading_blanks, had_breaks;
	const char *last_ptr;
	struct fy_mark mark, last_mark;
	bool target_simple_key_allowed, is_multiline, is_complex;
	struct fy_simple_key_mark skm;
	struct fy_error_ctx ec;
	struct fy_token *fyt;

	/* may not start with blankz */
	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			!fy_is_blankz(c), err_starts_with_blankz);
	/* may not start with any of ,[]{}#&*!|>'\"%@` */
	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			!fy_utf8_strchr(",[]{}#&*!|>'\"%@`", c),
			err_starts_with_invalid_character);
	/* may not start with - not followed by blankz */
	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			c != '-' || !fy_is_blank_at_offset(fyp, 1),
			err_starts_with_invalid_character_followed_by_blank);
	/* may not start with -?: not followed by blankz (in block context) */
	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			fyp->flow_level || !((c == '?' || c == ':') && fy_is_blank_at_offset(fyp, 1)),
			err_starts_with_invalid_character_followed_by_blank);

	/* check indentation */
	FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
			!fyp->flow_level || fyp->column > fyp->indent,
			err_wrongly_indented);

	fy_get_mark(fyp, &mark);
	target_simple_key_allowed = false;
	fy_get_simple_key_mark(fyp, &skm);

	fy_fill_atom_start(fyp, &handle);

	has_leading_blanks = false;
	had_breaks = false;
	length = 0;
	breaks_found = 0;
	blanks_found = 0;
	indent = fyp->indent + 1;
	last_ptr = NULL;
	memset(&last_mark, 0, sizeof(last_mark));
	for (;;) {
		/* break for document indicators */
		if (fyp->column == 0 &&
		   (!fy_strncmp(fyp, "---", 3) || !fy_strncmp(fyp, "...", 3)) &&
		   fy_is_blankz_at_offset(fyp, 3))
			break;

		c = fy_parse_peek(fyp);
		if (c == '#')
			break;

		run = 0;
		for (;;) {
			if (fy_is_blankz(c))
				break;

			nextc = fy_parse_peek_at(fyp, 1);

			/* ':' followed by space terminates */
			if (c == ':' && fy_is_blankz(nextc))
				break;

			/* in flow context ':' followed by flow markers */
			if (fyp->flow_level && c == ':' && fy_utf8_strchr(",[]{}", nextc))
				break;

			/* in flow context any or , [ ] { } */
			if (fyp->flow_level && (c == ',' || c == '[' || c == ']' || c == '{' || c == '}'))
				break;

			if (breaks_found) {
				/* minimum 1 sep, or more for consecutive */
				length += breaks_found > 1 ? (breaks_found - 1) : 1;
				breaks_found = 0;
				blanks_found = 0;
			} else if (blanks_found) {
				/* just the blanks mam' */
				length += blanks_found;
				blanks_found = 0;
			}

			fy_advance(fyp, c);
			run++;

			length += fy_utf8_width(c);

			c = nextc;
		}

		/* save end mark if we processed more than one non-blank */
		if (run > 0) {
			/* fy_scan_debug(fyp, "saving mark"); */
			last_ptr = fyp->current_ptr;
			fy_get_mark(fyp, &last_mark);
		}

		/* end? */
		if (!(fy_is_blank(c) || fy_is_break(c)))
			break;

		/* consume blanks */
		breaks_found = 0;
		blanks_found = 0;
		do {
			fy_advance(fyp, c);

			/* check for tab */

			FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
					c != '\t' || !has_leading_blanks || fyp->column >= indent,
					err_invalid_tab_indentation);

			nextc = fy_parse_peek(fyp);

			/* if it's a break */
			if (fy_is_break(c)) {
				/* first break, turn on leading blanks */
				if (!has_leading_blanks)
					has_leading_blanks = true;
				had_breaks = true;
				breaks_found++;
				blanks_found = 0;
			} else
				blanks_found++;

			c = nextc;

		} while (fy_is_blank(c) || fy_is_break(c));

		/* break out if indentation is less */
		if (!fyp->flow_level && fyp->column < indent)
			break;
	}

	/* end... */
	if (!last_ptr)
		fy_fill_atom_end(fyp, &handle);
	else
		fy_fill_atom_end_at(fyp, &handle, &last_mark);

	is_multiline = handle.end_mark.line > handle.start_mark.line;
	is_complex = fyp->pending_complex_key_column >= 0;

	handle.style = FYAS_PLAIN;
	handle.chomp = FYAC_STRIP;
	handle.storage_hint = length;
	handle.storage_hint_valid = true;
	handle.direct_output = !is_multiline && fy_atom_size(&handle) == length;

#ifdef ATOM_SIZE_CHECK
	length = fy_atom_format_internal(&handle, NULL, NULL);
	fy_error_check(fyp,
		length == handle.storage_hint,
		err_out, "storage hint calculation failed real %zu != hint %zu - '%s'",
		length, handle.storage_hint,
		fy_utf8_format_text_a(fy_atom_data(&handle), fy_atom_size(&handle), fyue_singlequote));
#endif
	/* and we're done */
	fyt = fy_token_queue(fyp, FYTT_SCALAR, &handle, FYSS_PLAIN);
	fy_error_check(fyp, fyt, err_out_rc,
			"fy_token_queue() failed");

	if (is_multiline && !fyp->flow_level && !is_complex) {
		/* due to the weirdness with simple keys scan forward
		* until a linebreak, ';', or anything else */
		for (i = 0; ; i++) {
			c = fy_parse_peek_at(fyp, i);
			if (c < 0 || c == ':' || fy_is_lb(c) || !fy_is_ws(c))
				break;
		}

		/* if we're a key, that's invalid */
		FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
				c != ':',
				err_multiline_key);
	}

	target_simple_key_allowed = had_breaks;

	rc = fy_save_simple_key_mark(fyp, &skm, FYTT_SCALAR, &handle.end_mark);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_save_simple_key_mark() failed");

	fyp->simple_key_allowed = target_simple_key_allowed;
	fy_scan_debug(fyp, "simple_key_allowed -> %s\n", fyp->simple_key_allowed ? "true" : "false");

	rc = fy_attach_comments_if_any(fyp, fyt);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_attach_right_hand_comment() failed");

	return 0;

err_out:
	rc = -1;
err_out_rc:
	return rc;

err_starts_with_blankz:
	if (fyp->state == FYPS_BLOCK_MAPPING_VALUE && fy_is_tab(c)) {
		ec.module = FYEM_PARSE;
		fy_error_report(fyp, &ec, "invalid tab as indendation in a mapping");
		goto err_out;
	}

	fy_error_report(fyp, &ec, "plain scalar cannot start with blank or zero");
	goto err_out;

err_starts_with_invalid_character:
	fy_error_report(fyp, &ec, "plain scalar cannot start with '%c'", c);
	goto err_out;

err_starts_with_invalid_character_followed_by_blank:
	fy_error_report(fyp, &ec, "plain scalar cannot start with '%c' followed by blank", c);
	goto err_out;

err_wrongly_indented:
	fy_error_report(fyp, &ec,
			"wrongly indented flow %s",
			fyp->flow == FYFT_SEQUENCE ? "sequence" : "mapping");
	goto err_out;

err_multiline_key:
	ec.start_mark = ec.end_mark = mark;
	fy_error_report(fyp, &ec, "invalid multiline plain key");
	goto err_out;

err_invalid_tab_indentation:
	fy_error_report(fyp, &ec, "invalid tab used as indentation");
	goto err_out;
}

int fy_fetch_tokens(struct fy_parser *fyp)
{
	struct fy_error_ctx ec;
	int c, rc;

	if (!fyp->stream_start_produced) {
		rc = fy_parse_get_next_input(fyp);
		fy_error_check(fyp, rc >= 0, err_out_rc,
			"fy_parse_get_next_input() failed");

		if (rc > 0) {
			rc = fy_fetch_stream_start(fyp);
			fy_error_check(fyp, !rc, err_out_rc,
					"fy_fetch_stream_start() failed");
		}
		return 0;
	}

	fy_scan_debug(fyp, "-------------------------------------------------");
	rc = fy_scan_to_next_token(fyp);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_scan_to_next_token() failed");

	rc = fy_parse_unroll_indent(fyp, fyp->column);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_parse_unroll_indent() failed");

	c = fy_parse_peek(fyp);
	if (c < 0 || c == '\0') {
		if (c >= 0)
			fy_advance(fyp, c);
		rc = fy_fetch_stream_end(fyp);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_fetch_stream_end() failed");
		return 0;
	}

	if (fyp->column == 0 && c == '%') {

		FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
				!fyp->bare_document_only,
				err_illegal_directive_in_bare_doc_mode);

		fy_advance(fyp, c);
		rc = fy_fetch_directive(fyp);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_fetch_directive() failed");
		return 0;
	}

	/* probable document start/end indicator */
	if (fyp->column == 0 &&
	(!fy_strncmp(fyp, "---", 3) || !fy_strncmp(fyp, "...", 3)) &&
	fy_is_blankz_at_offset(fyp, 3)) {

		FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
				!fyp->bare_document_only,
				err_illegal_doc_ind_in_bare_doc_mode);

		rc = fy_fetch_document_indicator(fyp,
				c == '-' ? FYTT_DOCUMENT_START :
					FYTT_DOCUMENT_END);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_fetch_document_indicator() failed");

		/* for document end, nothing must follow except whitespace and comment */
		if (c == '.') {
			c = fy_parse_peek(fyp);
			FY_ERROR_CHECK(fyp, NULL, &ec, FYEM_SCAN,
					c == -1 || c == '#' || fy_is_lb(c),
					err_illegal_content_after_end_indicator);
		}

		return 0;
	}

	fy_scan_debug(fyp, "indent=%d, parent indent=%d\n",
			fyp->indent, fyp->parent_indent);

	if (c == '[' || c == '{') {

		rc = fy_fetch_flow_collection_mark_start(fyp, c);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_fetch_flow_collection_mark_start() failed");
		return 0;
	}

	if (c == ']' || c == '}') {

		rc = fy_fetch_flow_collection_mark_end(fyp, c);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_fetch_flow_collection_mark_end() failed");
		return 0;
	}


	if (c == ',') {

		rc = fy_fetch_flow_collection_entry(fyp, c);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_fetch_flow_collection_entry() failed");
		return 0;
	}

	if (c == '-' && fy_is_blankz_at_offset(fyp, 1)) {

		rc = fy_fetch_block_entry(fyp, c);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_fetch_block_entry() failed");
		return 0;
	}

	if (c == '?' && (fyp->flow_level || fy_is_blankz_at_offset(fyp, 1))) {

		rc = fy_fetch_key(fyp, c);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_fetch_key() failed");
		return 0;
	}

	if (c == ':' && ((fyp->flow_level && !fyp->simple_key_allowed) || fy_is_blankz_at_offset(fyp, 1))) {

		rc = fy_fetch_value(fyp, c);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_fetch_value() failed");
		return 0;
	}

	if (c == '*' || c == '&') {

		rc = fy_fetch_anchor_or_alias(fyp, c);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_fetch_anchor_or_alias() failed");
		return 0;
	}

	if (c == '!') {

		rc = fy_fetch_tag(fyp, c);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_fetch_tag() failed");
		return 0;
	}

	if (!fyp->flow_level && (c == '|' || c == '>')) {

		rc = fy_fetch_block_scalar(fyp, c == '|', c);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_fetch_block_scalar() failed");
		return 0;
	}

	if (c == '\'' || c == '"') {

		rc = fy_fetch_flow_scalar(fyp, c);
		fy_error_check(fyp, !rc, err_out_rc,
				"fy_fetch_flow_scalar() failed");
		return 0;
	}

	rc = fy_fetch_plain_scalar(fyp, c);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_fetch_plain_scalar() failed");
	return 0;

err_out:
	rc = -1;
err_out_rc:
	return rc;

err_illegal_content_after_end_indicator:
	fy_error_report(fyp, &ec, "invalid content after document end marker");
	goto err_out;

err_illegal_directive_in_bare_doc_mode:
	fy_error_report(fyp, &ec, "invalid directive in bare document mode");
	goto err_out;

err_illegal_doc_ind_in_bare_doc_mode:
	fy_error_report(fyp, &ec, "invalid document %s indicator in bare document mode",
			c == '-' ? "start" : "end");
	goto err_out;
}

struct fy_token *fy_scan_peek(struct fy_parser *fyp)
{
	struct fy_token *fyt;
	int rc, last_token_activity_counter;
	bool have_simple_keys;

	/* nothing if stream end produced (and no stream end token in queue) */
	if (fyp->stream_end_produced) {
		fyt = fy_token_list_head(&fyp->queued_tokens);
		if (fyt && fyt->type == FYTT_STREAM_END)
			return fyt;

		/* OK, we're done, flush everything */
		fy_token_list_unref_all(&fyp->queued_tokens);

		/* try to get the next input */
		rc = fy_parse_get_next_input(fyp);
		fy_error_check(fyp, rc >= 0, err_out,
			"fy_parse_get_next_input() failed");

		/* no more inputs */
		if (rc == 0) {
			fy_scan_debug(fyp, "token stream ends");
			return NULL;
		}

		fy_scan_debug(fyp, "starting new token stream");

		fyp->stream_start_produced = false;
		fyp->stream_end_produced = false;
	}

	/* we loop until we have a token and the simple key list is empty */
	for (;;) {
		fyt = fy_token_list_head(&fyp->queued_tokens);
		have_simple_keys = !fy_simple_key_list_empty(&fyp->simple_keys);

		/* we can produce a token when:
		* a) one exists
		* b) no simple keys exist at all
		*/
		if (fyt && !have_simple_keys)
			break;

		/* on stream error we're done */
		if (fyp->stream_error)
			return NULL;

		/* keep track of token activity, if it didn't change
		* after the fetch tokens call, the state machine is stuck
		*/
		last_token_activity_counter = fyp->token_activity_counter;

		/* fetch more then */
		rc = fy_fetch_tokens(fyp);
		fy_error_check(fyp, !rc, err_out,
				"fy_fetch_tokens() failed");

		fy_error_check(fyp, last_token_activity_counter != fyp->token_activity_counter, err_out,
				"out of tokens and failed to produce anymore");
	}

	switch (fyt->type) {
	case FYTT_STREAM_START:
		fy_scan_debug(fyp, "setting stream_start_produced to true");
		fyp->stream_start_produced = true;
		break;
	case FYTT_STREAM_END:
		fy_scan_debug(fyp, "setting stream_end_produced to true");
		fyp->stream_end_produced = true;

		rc = fy_parse_input_done(fyp);
		fy_error_check(fyp, !rc, err_out,
				"fy_parse_input_done() failed");
		break;
	default:
		break;
	}

	return fyt;

err_out:
	return NULL;
}

struct fy_token *fy_scan_remove(struct fy_parser *fyp, struct fy_token *fyt)
{
	if (!fyp || !fyt)
		return NULL;

	fy_token_list_del(&fyp->queued_tokens, fyt);

	return fyt;
}

struct fy_token *fy_scan_remove_peek(struct fy_parser *fyp, struct fy_token *fyt)
{
	fy_token_unref(fy_scan_remove(fyp, fyt));

	return fy_scan_peek(fyp);
}

struct fy_token *fy_scan(struct fy_parser *fyp)
{
	struct fy_token *fyt;

	fyt = fy_scan_remove(fyp, fy_scan_peek(fyp));

	if (fyt)
		fy_debug_dump_token(fyp, fyt, "producing: ");
	return fyt;
}

int fy_parse_state_push(struct fy_parser *fyp, enum fy_parser_state state)
{
	struct fy_parse_state_log *fypsl;

	fypsl = fy_parse_parse_state_log_alloc(fyp);
	fy_error_check(fyp, fypsl != NULL, err_out,
			"fy_parse_state_log_alloc() failed!");
	fypsl->state = state;
	fy_parse_state_log_list_push(&fyp->state_stack, fypsl);

	return 0;
err_out:
	return -1;
}

enum fy_parser_state fy_parse_state_pop(struct fy_parser *fyp)
{
	struct fy_parse_state_log *fypsl;
	enum fy_parser_state state;

	fypsl = fy_parse_state_log_list_pop(&fyp->state_stack);
	if (!fypsl)
		return FYPS_NONE;

	state = fypsl->state;

	fy_parse_parse_state_log_recycle(fyp, fypsl);

	return state;
}

void fy_parse_state_set(struct fy_parser *fyp, enum fy_parser_state state)
{
	fy_parse_debug(fyp, "state %s -> %s\n", state_txt[fyp->state], state_txt[state]);
	fyp->state = state;
}

enum fy_parser_state fy_parse_state_get(struct fy_parser *fyp)
{
	return fyp->state;
}

static struct fy_eventp *
fy_parse_node(struct fy_parser *fyp, struct fy_token *fyt, struct fy_eventp *fyep,
		bool is_block, bool is_indentless_sequence)
{
	struct fy_document_state *fyds;
	struct fy_event *fye = &fyep->e;
	struct fy_token *anchor = NULL, *tag = NULL;
	const char *handle;
	size_t handle_size;
	struct fy_token *fyt_td;
	struct fy_error_ctx ec;

	fyds = fyp->current_document_state;
	assert(fyds);

	fy_parse_debug(fyp, "parse_node: is_block=%s is_indentless=%s - fyt %s",
			is_block ? "true" : "false",
			is_indentless_sequence ? "true" : "false",
			fy_token_type_txt[fyt->type]);

	if (fyt->type == FYTT_ALIAS) {
		fy_parse_state_set(fyp, fy_parse_state_pop(fyp));

		fye->type = FYET_ALIAS;
		fye->alias.anchor = fy_scan_remove(fyp, fyt);

		goto return_ok;
	}

	while ((!anchor && fyt->type == FYTT_ANCHOR) || (!tag && fyt->type == FYTT_TAG)) {
		if (fyt->type == FYTT_ANCHOR)
			anchor = fy_scan_remove(fyp, fyt);
		else
			tag = fy_scan_remove(fyp, fyt);

		fyt = fy_scan_peek(fyp);
		fy_error_check(fyp, fyt, err_out,
				"failed to peek token");

		fy_parse_debug(fyp, "parse_node: ANCHOR|TAG got -  fyt %s",
			fy_token_type_txt[fyt->type]);

		FY_ERROR_CHECK(fyp, fyt, &ec, FYEM_PARSE,
				fyt->type != FYTT_ALIAS, err_unexpected_alias);
	}

	/* check tag prefix */
	if (tag && tag->tag.handle_length) {
		handle = fy_atom_data(&tag->handle) + tag->tag.skip;
		handle_size = tag->tag.handle_length;

		fyt_td = fy_document_state_lookup_tag_directive(fyds, handle, handle_size);
		FY_ERROR_CHECK(fyp, fyt, &ec, FYEM_PARSE,
				fyt_td, err_undefined_tag_prefix);
	}

	if ((fyp->state == FYPS_BLOCK_NODE_OR_INDENTLESS_SEQUENCE ||
	     fyp->state == FYPS_BLOCK_MAPPING_VALUE ||
	     fyp->state == FYPS_BLOCK_MAPPING_FIRST_KEY)
		&& fyt->type == FYTT_BLOCK_ENTRY) {

		fye->type = FYET_SEQUENCE_START;
		fye->sequence_start.anchor = anchor;
		fye->sequence_start.tag = tag;
		fye->sequence_start.sequence_start = NULL;
		fy_parse_state_set(fyp, FYPS_INDENTLESS_SEQUENCE_ENTRY);
		goto return_ok;
	}

	if (fyt->type == FYTT_SCALAR) {
		fy_parse_state_set(fyp, fy_parse_state_pop(fyp));

		fye->type = FYET_SCALAR;
		fye->scalar.anchor = anchor;
		fye->scalar.tag = tag;
		fye->scalar.value = fy_scan_remove(fyp, fyt);
		goto return_ok;
	}

	if (fyt->type == FYTT_FLOW_SEQUENCE_START) {
		fye->type = FYET_SEQUENCE_START;
		fye->sequence_start.anchor = anchor;
		fye->sequence_start.tag = tag;
		fye->sequence_start.sequence_start = fy_scan_remove(fyp, fyt);
		fy_parse_state_set(fyp, FYPS_FLOW_SEQUENCE_FIRST_ENTRY);
		goto return_ok;
	}

	if (fyt->type == FYTT_FLOW_MAPPING_START) {
		fye->type = FYET_MAPPING_START;
		fye->mapping_start.anchor = anchor;
		fye->mapping_start.tag = tag;
		fye->mapping_start.mapping_start = fy_scan_remove(fyp, fyt);
		fy_parse_state_set(fyp, FYPS_FLOW_MAPPING_FIRST_KEY);
		goto return_ok;
	}

	if (is_block && fyt->type == FYTT_BLOCK_SEQUENCE_START) {
		fye->type = FYET_SEQUENCE_START;
		fye->sequence_start.anchor = anchor;
		fye->sequence_start.tag = tag;
		fye->sequence_start.sequence_start = fy_scan_remove(fyp, fyt);
		fy_parse_state_set(fyp, FYPS_BLOCK_SEQUENCE_FIRST_ENTRY);
		goto return_ok;
	}

	if (is_block && fyt->type == FYTT_BLOCK_MAPPING_START) {
		fye->type = FYET_MAPPING_START;
		fye->mapping_start.anchor = anchor;
		fye->mapping_start.tag = tag;
		fye->mapping_start.mapping_start = fy_scan_remove(fyp, fyt);
		fy_parse_state_set(fyp, FYPS_BLOCK_MAPPING_FIRST_KEY);
		goto return_ok;
	}

	FY_ERROR_CHECK(fyp, fyt, &ec, FYEM_PARSE,
			anchor || tag,
			err_unexpected_content);

	fy_parse_debug(fyp, "parse_node: empty scalar...");

	/* empty scalar */
	fy_parse_state_set(fyp, fy_parse_state_pop(fyp));

	fye->type = FYET_SCALAR;
	fye->scalar.anchor = anchor;
	fye->scalar.tag = tag;
	fye->scalar.value = NULL;

return_ok:
	fy_parse_debug(fyp, "parse_node: > %s",
			fy_event_type_txt[fye->type]);

	return fyep;

err_out:
	fy_token_unref(anchor);
	fy_token_unref(tag);
	fy_parse_eventp_recycle(fyp, fyep);

	return NULL;

err_unexpected_content:
	if (fyt->type == FYTT_FLOW_ENTRY &&
		(fyp->state == FYPS_FLOW_SEQUENCE_FIRST_ENTRY ||
		fyp->state == FYPS_FLOW_SEQUENCE_ENTRY)) {

		fy_error_report(fyp, &ec, "flow sequence with invalid %s",
				fyp->state == FYPS_FLOW_SEQUENCE_FIRST_ENTRY ?
				"comma in the beginning" : "extra comma");
		goto err_out;
	}
	if ((fyt->type == FYTT_DOCUMENT_START || fyt->type == FYTT_DOCUMENT_END) &&
		(fyp->state == FYPS_FLOW_SEQUENCE_FIRST_ENTRY ||
		fyp->state == FYPS_FLOW_SEQUENCE_ENTRY)) {

		fy_error_report(fyp, &ec, "invalid document %s indicator in a flow sequence",
				fyt->type == FYTT_DOCUMENT_START ? "start" : "end");
		goto err_out;
	}
	fy_error_report(fyp, &ec, "did not find expected node content");
	goto err_out;

err_undefined_tag_prefix:
	assert(tag);	/* paranoid */
	ec.start_mark = *fy_token_start_mark(tag);
	ec.end_mark = *fy_token_end_mark(tag);
	fy_error_report(fyp, &ec, "undefined tag prefix '%.*s'",
					(int)handle_size, handle);
	goto err_out;

err_unexpected_alias:
	fy_error_report(fyp, &ec, "unexpected alias");
	goto err_out;
}

static struct fy_eventp *
fy_parse_empty_scalar(struct fy_parser *fyp, struct fy_eventp *fyep)
{
	struct fy_event *fye = &fyep->e;

	fye->type = FYET_SCALAR;
	fye->scalar.anchor = NULL;
	fye->scalar.tag = NULL;
	fye->scalar.value = NULL;
	return fyep;
}

int fy_parse_stream_start(struct fy_parser *fyp)
{
	fyp->indent = -2;
	fyp->generated_block_map = false;
	fyp->flow = FYFT_NONE;
	fyp->pending_complex_key_column = -1;

	fy_parse_indent_list_recycle_all(fyp, &fyp->indent_stack);
	fy_parse_simple_key_list_recycle_all(fyp, &fyp->simple_keys);
	fy_parse_parse_state_log_list_recycle_all(fyp, &fyp->state_stack);
	fy_parse_flow_list_recycle_all(fyp, &fyp->flow_stack);

	fy_token_unref(fyp->stream_end_token);
	fyp->stream_end_token = NULL;

	return 0;
}

int fy_parse_stream_end(struct fy_parser *fyp)
{
	fy_token_unref(fyp->stream_end_token);
	fyp->stream_end_token = NULL;

	return 0;
}

static struct fy_eventp *fy_parse_internal(struct fy_parser *fyp)
{
	struct fy_eventp *fyep = NULL;
	struct fy_event *fye = NULL;
	struct fy_token *fyt = NULL;
	struct fy_document_state *fyds = NULL;
	bool is_block, is_seq, is_value, is_first, had_doc_end, had_directives;
	enum fy_parser_state orig_state;
	struct fy_token *version_directive;
	struct fy_token_list tag_directives;
	const struct fy_mark *fym;
	char tbuf[16] __FY_DEBUG_UNUSED__;
	struct fy_error_ctx ec;
	int rc;

	/* XXX */
	memset(&ec, 0, sizeof(ec));

	version_directive = NULL;
	fy_token_list_init(&tag_directives);

	/* are we done? */
	if (fyp->stream_error || fyp->state == FYPS_END)
		return NULL;

	fyt = fy_scan_peek(fyp);

	/* special case without an error message for start */
	if (!fyt && fyp->state == FYPS_NONE)
		return NULL;

	/* keep a copy of stream end */
	if (fyt && fyt->type == FYTT_STREAM_END && !fyp->stream_end_token) {
		fyp->stream_end_token = fy_token_ref(fyt);
		fy_parse_debug(fyp, "kept copy of STRM-");
	}

	/* keep on producing STREAM_END */
	if (!fyt && fyp->stream_end_token) {
		fyt = fyp->stream_end_token;
		fy_token_list_add_tail(&fyp->queued_tokens, fyt);

		fy_parse_debug(fyp, "generated copy of STRM-");
	}

	fy_error_check(fyp, fyt, err_out,
			"failed to peek token");

	assert(fyt->handle.fyi);

	fyep = fy_parse_eventp_alloc(fyp);
	fy_error_check(fyp, fyep, err_out,
			"fy_eventp_alloc() failed!");
	fyep->fyp = fyp;
	fye = &fyep->e;

	fye->type = FYET_NONE;

	fy_parse_debug(fyp, "[%s] <- %s", state_txt[fyp->state],
			fy_token_dump_format(fyt, tbuf, sizeof(tbuf)));

	is_first = false;
	had_doc_end = false;

	orig_state = fyp->state;
	switch (fyp->state) {
	case FYPS_NONE:
		fy_parse_state_set(fyp, FYPS_STREAM_START);
		/* fallthrough */

	case FYPS_STREAM_START:

		fy_error_check(fyp, fyt->type == FYTT_STREAM_START, err_out,
				"failed to get valid stream start token");
		fye->type = FYET_STREAM_START;

		fye->stream_start.stream_start = fy_scan_remove(fyp, fyt);

		rc = fy_parse_stream_start(fyp);
		fy_error_check(fyp, !rc, err_out,
				"stream start failed");

		fy_parse_state_set(fyp, FYPS_IMPLICIT_DOCUMENT_START);

		return fyep;

	case FYPS_IMPLICIT_DOCUMENT_START:

		/* fallthrough */

	case FYPS_DOCUMENT_START:

		had_doc_end = false;

		/* remove all extra document end indicators */
		while (fyt->type == FYTT_DOCUMENT_END) {

			/* reset document has content flag */
			fyp->document_has_content = false;
			fyp->document_first_content_token = true;

			fyt = fy_scan_remove_peek(fyp, fyt);
			fy_error_check(fyp, fyt, err_out,
					"failed to peek token");
			fy_debug_dump_token(fyp, fyt, "next: ");

			had_doc_end = true;
		}

		if (!fyp->current_document_state) {
			rc = fy_reset_document_state(fyp);
			fy_error_check(fyp, !rc, err_out,
					"fy_reset_document_state() failed");
		}

		fyds = fyp->current_document_state;
		fy_error_check(fyp, fyds, err_out,
				"no current document state error");

		/* process directives */
		had_directives = false;
		while (fyt->type == FYTT_VERSION_DIRECTIVE ||
			fyt->type == FYTT_TAG_DIRECTIVE) {

			had_directives = true;
			if (fyt->type == FYTT_VERSION_DIRECTIVE) {

				rc = fy_parse_version_directive(fyp, fy_scan_remove(fyp, fyt));
				fyt = NULL;
				fy_error_check(fyp, !rc, err_out,
						"failed to fy_parse_version_directive()");
			} else {
				rc = fy_parse_tag_directive(fyp, fy_scan_remove(fyp, fyt));
				fyt = NULL;

				fy_error_check(fyp, !rc, err_out,
						"failed to fy_parse_tag_directive()");
			}

			fyt = fy_scan_peek(fyp);
			fy_error_check(fyp, fyt, err_out,
					"failed to peek token");
			fy_debug_dump_token(fyp, fyt, "next: ");
		}

		/* the end */
		if (fyt->type == FYTT_STREAM_END) {

			rc = fy_parse_stream_end(fyp);
			fy_error_check(fyp, !rc, err_out,
					"stream end failed");

			fye->type = FYET_STREAM_END;
			fye->stream_end.stream_end = fy_scan_remove(fyp, fyt);

			fy_parse_state_set(fyp,
				fy_parse_have_more_inputs(fyp) ? FYPS_NONE : FYPS_END);

			return fyep;
		}

		/* document start */
		fye->type = FYET_DOCUMENT_START;
		fye->document_start.document_start = NULL;
		fye->document_start.document_state = NULL;

		FY_ERROR_CHECK(fyp, fyt, &ec, FYEM_PARSE,
				fyp->state == FYPS_IMPLICIT_DOCUMENT_START || had_doc_end || fyt->type == FYTT_DOCUMENT_START,
				err_missing_document_start);

		fym = fy_token_start_mark(fyt);
		if (fym)
			fyds->start_mark = *fym;
		else
			memset(&fyds->start_mark, 0, sizeof(fyds->start_mark));

		if (fyt->type != FYTT_DOCUMENT_START) {
			fye->document_start.document_start = NULL;

			fyds->start_implicit = true;
			fy_parse_debug(fyp, "document_start_implicit=true");

			FY_ERROR_CHECK(fyp, fyt, &ec, FYEM_PARSE,
					fyt->type != FYTT_DOCUMENT_END || !had_directives,
					err_directives_without_doc);

			fy_parse_state_set(fyp, FYPS_BLOCK_NODE);
		} else {
			fye->document_start.document_start = fy_scan_remove(fyp, fyt);

			fyds->start_implicit = false;
			fy_parse_debug(fyp, "document_start_implicit=false");

			fy_parse_state_set(fyp, FYPS_DOCUMENT_CONTENT);
		}

		rc = fy_parse_state_push(fyp, FYPS_DOCUMENT_END);
		fy_error_check(fyp, !rc, err_out,
				"failed to fy_parse_state_push()");

		fye->document_start.document_state = fy_document_state_ref(fyds);
		fye->document_start.implicit = fyds->start_implicit;

		return fyep;

	case FYPS_DOCUMENT_END:

		FY_ERROR_CHECK(fyp, fyt, &ec, FYEM_PARSE,
			!(fyp->document_has_content &&
				(fyt->type == FYTT_VERSION_DIRECTIVE ||
				fyt->type == FYTT_TAG_DIRECTIVE)),
			err_missing_doc_end_before_directives);

		fyds = fyp->current_document_state;
		fy_error_check(fyp, fyds, err_out,
				"no current document state error");

		fym = fy_token_end_mark(fyt);
		if (fym)
			fyds->end_mark = *fym;
		else
			memset(&fyds->end_mark, 0, sizeof(fyds->end_mark));

		/* document end */
		fye->type = FYET_DOCUMENT_END;
		if (fyt->type == FYTT_DOCUMENT_END) {
			/* TODO pull the document end token and deliver */
			fye->document_end.document_end = NULL;
			fyds->end_implicit = false;

			/* reset document has content flag */
			fyp->document_has_content = false;
			fyp->document_first_content_token = true;

		} else {
			fye->document_end.document_end = NULL;
			fyds->end_implicit = true;
		}

		fye->document_end.implicit = fyds->end_implicit;

		fy_parse_state_set(fyp, FYPS_DOCUMENT_START);

		/* and reset document state */
		rc = fy_reset_document_state(fyp);
		fy_error_check(fyp, !rc, err_out,
				"fy_reset_document_state() failed");

		return fyep;

	case FYPS_DOCUMENT_CONTENT:

		if (fyt->type == FYTT_VERSION_DIRECTIVE ||
		    fyt->type == FYTT_TAG_DIRECTIVE ||
		    fyt->type == FYTT_DOCUMENT_START ||
		    fyt->type == FYTT_DOCUMENT_END ||
		    fyt->type == FYTT_STREAM_END) {

			if (fyt->type == FYTT_DOCUMENT_START ||
			fyt->type == FYTT_DOCUMENT_END) {
				fyp->document_has_content = false;
				fyp->document_first_content_token = true;
			}

			fy_parse_state_set(fyp, fy_parse_state_pop(fyp));

			return fy_parse_empty_scalar(fyp, fyep);
		}

		fyp->document_has_content = true;
		fy_parse_debug(fyp, "document has content now");
		/* fallthrough */

	case FYPS_BLOCK_NODE:
	case FYPS_BLOCK_NODE_OR_INDENTLESS_SEQUENCE:
	case FYPS_FLOW_NODE:

		fyep = fy_parse_node(fyp, fyt, fyep,
				fyp->state == FYPS_BLOCK_NODE ||
				fyp->state == FYPS_BLOCK_NODE_OR_INDENTLESS_SEQUENCE ||
				fyp->state == FYPS_DOCUMENT_CONTENT,
				fyp->state == FYPS_BLOCK_NODE_OR_INDENTLESS_SEQUENCE);
		fy_error_check(fyp, fyep, err_out,
				"fy_parse_node() failed");
		return fyep;

	case FYPS_BLOCK_SEQUENCE_FIRST_ENTRY:
		is_first = true;
		/* fallthrough */

	case FYPS_BLOCK_SEQUENCE_ENTRY:
	case FYPS_INDENTLESS_SEQUENCE_ENTRY:

		if (fyp->state == FYPS_BLOCK_SEQUENCE_ENTRY || is_first) {
			FY_ERROR_CHECK(fyp, fyt, &ec, FYEM_PARSE,
					fyt->type == FYTT_BLOCK_ENTRY || fyt->type == FYTT_BLOCK_END,
					err_missing_block_seq_indicator);
		}

		if (fyt->type == FYTT_BLOCK_ENTRY) {

			/* BLOCK entry */
			fyt = fy_scan_remove_peek(fyp, fyt);
			fy_error_check(fyp, fyt, err_out,
					"failed to peek token");
			fy_debug_dump_token(fyp, fyt, "next: ");

			/* check whether it's a sequence entry or not */
			is_seq = fyt->type != FYTT_BLOCK_ENTRY && fyt->type != FYTT_BLOCK_END;
			if (!is_seq && fyp->state == FYPS_INDENTLESS_SEQUENCE_ENTRY)
				is_seq = fyt->type != FYTT_KEY && fyt->type != FYTT_VALUE;

			if (is_seq) {
				rc = fy_parse_state_push(fyp, fyp->state);
				fy_error_check(fyp, !rc, err_out,
						"failed to push state");

				fyep = fy_parse_node(fyp, fyt, fyep, true, false);
				fy_error_check(fyp, fyep, err_out,
						"fy_parse_node() failed");
				return fyep;
			}
			fy_parse_state_set(fyp, FYPS_BLOCK_SEQUENCE_ENTRY);
			return fy_parse_empty_scalar(fyp, fyep);

		}

		/* FYTT_BLOCK_END */
		fy_parse_state_set(fyp, fy_parse_state_pop(fyp));
		fye->type = FYET_SEQUENCE_END;
		fye->sequence_end.sequence_end = orig_state != FYPS_INDENTLESS_SEQUENCE_ENTRY ? fy_scan_remove(fyp, fyt) : NULL;
		return fyep;

	case FYPS_BLOCK_MAPPING_FIRST_KEY:
		is_first = true;
		/* fallthrough */

	case FYPS_BLOCK_MAPPING_KEY:

		FY_ERROR_CHECK(fyp, fyt, &ec, FYEM_PARSE,
				fyt->type == FYTT_KEY || fyt->type == FYTT_BLOCK_END || fyt->type == FYTT_STREAM_END,
				err_did_not_find_expected_key);

		if (fyt->type == FYTT_KEY) {

			fyp->last_block_mapping_key_line = fy_token_end_line(fyt);

			/* KEY entry */
			fyt = fy_scan_remove_peek(fyp, fyt);
			fy_error_check(fyp, fyt, err_out,
					"failed to peek token");
			fy_debug_dump_token(fyp, fyt, "next: ");

			/* check whether it's a block entry or not */
			is_block = fyt->type != FYTT_KEY && fyt->type != FYTT_VALUE &&
				fyt->type != FYTT_BLOCK_END;

			if (is_block) {
				rc = fy_parse_state_push(fyp, FYPS_BLOCK_MAPPING_VALUE);
				fy_error_check(fyp, !rc, err_out,
						"failed to push state");

				fyep = fy_parse_node(fyp, fyt, fyep, true, true);
				fy_error_check(fyp, fyep, err_out,
						"fy_parse_node() failed");
				return fyep;
			}
			fy_parse_state_set(fyp, FYPS_BLOCK_MAPPING_VALUE);
			return fy_parse_empty_scalar(fyp, fyep);
		}

		/* FYTT_BLOCK_END */
		fy_parse_state_set(fyp, fy_parse_state_pop(fyp));
		fye->type = FYET_MAPPING_END;
		fye->mapping_end.mapping_end = fy_scan_remove(fyp, fyt);
		return fyep;

	case FYPS_BLOCK_MAPPING_VALUE:

		if (fyt->type == FYTT_VALUE) {

			/* VALUE entry */
			fyt = fy_scan_remove_peek(fyp, fyt);
			fy_error_check(fyp, fyt, err_out,
					"failed to peek token");
			fy_debug_dump_token(fyp, fyt, "next: ");

			/* check whether it's a block entry or not */
			is_value = fyt->type != FYTT_KEY && fyt->type != FYTT_VALUE &&
				fyt->type != FYTT_BLOCK_END;

			if (is_value) {
				rc = fy_parse_state_push(fyp, FYPS_BLOCK_MAPPING_KEY);
				fy_error_check(fyp, !rc, err_out,
						"failed to push state");

				fyep = fy_parse_node(fyp, fyt, fyep, true, true);
				fy_error_check(fyp, fyep, err_out,
						"fy_parse_node() failed");
				return fyep;
			}
		}

		fy_parse_state_set(fyp, FYPS_BLOCK_MAPPING_KEY);
		return fy_parse_empty_scalar(fyp, fyep);

	case FYPS_FLOW_SEQUENCE_FIRST_ENTRY:
		is_first = true;
		/* fallthrough */

	case FYPS_FLOW_SEQUENCE_ENTRY:

		if (fyt->type != FYTT_FLOW_SEQUENCE_END &&
		    fyt->type != FYTT_STREAM_END) {

			if (!is_first) {
				FY_ERROR_CHECK(fyp, fyt, &ec, FYEM_PARSE,
						fyt->type == FYTT_FLOW_ENTRY,
						err_missing_comma);

				fyt = fy_scan_remove_peek(fyp, fyt);
				fy_error_check(fyp, fyt, err_out,
						"failed to peek token");
				fy_debug_dump_token(fyp, fyt, "next: ");
			}

			if (fyt->type == FYTT_KEY) {
				fy_parse_state_set(fyp, FYPS_FLOW_SEQUENCE_ENTRY_MAPPING_KEY);
				fye->type = FYET_MAPPING_START;
				fye->mapping_start.anchor = NULL;
				fye->mapping_start.tag = NULL;
				fye->mapping_start.mapping_start = fy_scan_remove(fyp, fyt);
				return fyep;
			}

			if (fyt->type != FYTT_FLOW_SEQUENCE_END) {
				rc = fy_parse_state_push(fyp, FYPS_FLOW_SEQUENCE_ENTRY);
				fy_error_check(fyp, !rc, err_out,
						"failed to push state");

				fyep = fy_parse_node(fyp, fyt, fyep, false, false);
				fy_error_check(fyp, fyep, err_out,
						"fy_parse_node() failed");
				return fyep;
			}
		}

		FY_ERROR_CHECK(fyp, fyt, &ec, FYEM_PARSE,
				fyt->type != FYTT_STREAM_END || !fyp->flow_level,
				err_stream_end_on_flow_seq);

		/* FYTT_FLOW_SEQUENCE_END */
		fy_parse_state_set(fyp, fy_parse_state_pop(fyp));
		fye->type = FYET_SEQUENCE_END;
		fye->sequence_end.sequence_end = fy_scan_remove(fyp, fyt);
		return fyep;

	case FYPS_FLOW_SEQUENCE_ENTRY_MAPPING_KEY:
		if (fyt->type != FYTT_VALUE && fyt->type != FYTT_FLOW_ENTRY &&
		    fyt->type != FYTT_FLOW_SEQUENCE_END) {
			rc = fy_parse_state_push(fyp, FYPS_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE);
			fy_error_check(fyp, !rc, err_out,
					"failed to push state");

			fyep = fy_parse_node(fyp, fyt, fyep, false, false);
			fy_error_check(fyp, fyep, err_out,
					"fy_parse_node() failed");
			return fyep;
		}

		fy_parse_state_set(fyp, FYPS_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE);
		return fy_parse_empty_scalar(fyp, fyep);

	case FYPS_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE:
		if (fyt->type == FYTT_VALUE) {
			fyt = fy_scan_remove_peek(fyp, fyt);
			fy_error_check(fyp, fyt, err_out,
					"failed to peek token");
			fy_debug_dump_token(fyp, fyt, "next: ");

			if (fyt->type != FYTT_FLOW_ENTRY && fyt->type != FYTT_FLOW_SEQUENCE_END) {
				rc = fy_parse_state_push(fyp, FYPS_FLOW_SEQUENCE_ENTRY_MAPPING_END);
				fy_error_check(fyp, !rc, err_out,
						"failed to push state");

				fyep = fy_parse_node(fyp, fyt, fyep, false, false);
				fy_error_check(fyp, fyep, err_out,
						"fy_parse_node() failed");
				return fyep;
			}
		}
		fy_parse_state_set(fyp, FYPS_FLOW_SEQUENCE_ENTRY_MAPPING_END);
		return fy_parse_empty_scalar(fyp, fyep);

	case FYPS_FLOW_SEQUENCE_ENTRY_MAPPING_END:
		fy_parse_state_set(fyp, FYPS_FLOW_SEQUENCE_ENTRY);
		fye->type = FYET_MAPPING_END;
		fye->mapping_end.mapping_end = /* fy_scan_remove(fyp, fyt) */ NULL;
		return fyep;

	case FYPS_FLOW_MAPPING_FIRST_KEY:
		is_first = true;
		/* fallthrough */

	case FYPS_FLOW_MAPPING_KEY:
		if (fyt->type != FYTT_FLOW_MAPPING_END) {

			if (!is_first) {
				FY_ERROR_CHECK(fyp, fyt, &ec, FYEM_PARSE,
						fyt->type == FYTT_FLOW_ENTRY,
						err_missing_comma);

				fyt = fy_scan_remove_peek(fyp, fyt);
				fy_error_check(fyp, fyt, err_out,
						"failed to peek token");
				fy_debug_dump_token(fyp, fyt, "next: ");
			}

			if (fyt->type == FYTT_KEY) {
				/* next token */
				fyt = fy_scan_remove_peek(fyp, fyt);
				fy_error_check(fyp, fyt, err_out,
						"failed to peek token");
				fy_debug_dump_token(fyp, fyt, "next: ");

				if (fyt->type != FYTT_VALUE &&
				fyt->type != FYTT_FLOW_ENTRY &&
				fyt->type != FYTT_FLOW_MAPPING_END) {

					rc = fy_parse_state_push(fyp, FYPS_FLOW_MAPPING_VALUE);
					fy_error_check(fyp, !rc, err_out,
							"failed to push state");

					fyep = fy_parse_node(fyp, fyt, fyep, false, false);
					fy_error_check(fyp, fyep, err_out,
							"fy_parse_node() failed");
					return fyep;
				}
				fy_parse_state_set(fyp, FYPS_FLOW_MAPPING_VALUE);
				return fy_parse_empty_scalar(fyp, fyep);
			}

			if (fyt->type != FYTT_FLOW_MAPPING_END) {
				rc = fy_parse_state_push(fyp, FYPS_FLOW_MAPPING_EMPTY_VALUE);
				fy_error_check(fyp, !rc, err_out,
						"failed to push state");

				fyep = fy_parse_node(fyp, fyt, fyep, false, false);
				fy_error_check(fyp, fyep, err_out,
						"fy_parse_node() failed");
				return fyep;
			}
		}

		/* FYTT_FLOW_MAPPING_END */
		fy_parse_state_set(fyp, fy_parse_state_pop(fyp));
		fye->type = FYET_MAPPING_END;
		fye->mapping_end.mapping_end = fy_scan_remove(fyp, fyt);
		return fyep;

	case FYPS_FLOW_MAPPING_VALUE:
		if (fyt->type == FYTT_VALUE) {
			/* next token */
			fyt = fy_scan_remove_peek(fyp, fyt);
			fy_error_check(fyp, fyt, err_out,
					"failed to peek token");
			fy_debug_dump_token(fyp, fyt, "next: ");

			if (fyt->type != FYTT_FLOW_ENTRY &&
			fyt->type != FYTT_FLOW_MAPPING_END) {

				rc = fy_parse_state_push(fyp, FYPS_FLOW_MAPPING_KEY);
				fy_error_check(fyp, !rc, err_out,
						"failed to push state");

				fyep = fy_parse_node(fyp, fyt, fyep, false, false);
				fy_error_check(fyp, fyep, err_out,
						"fy_parse_node() failed");
				return fyep;
			}
		}
		fy_parse_state_set(fyp, FYPS_FLOW_MAPPING_KEY);
		return fy_parse_empty_scalar(fyp, fyep);

	case FYPS_FLOW_MAPPING_EMPTY_VALUE:
		fy_parse_state_set(fyp, FYPS_FLOW_MAPPING_KEY);
		return fy_parse_empty_scalar(fyp, fyep);

	case FYPS_END:
		/* should never happen */
		assert(0);
		break;
	}

err_out:
	fy_token_unref(version_directive);
	fy_token_list_unref_all(&tag_directives);
	fy_parse_eventp_recycle(fyp, fyep);
	fyp->stream_error = true;
	return NULL;

	/* error analysis path */
err_did_not_find_expected_key:
	if (fyt->type == FYTT_SCALAR) {
		if (!fyp->simple_key_allowed && !fyp->flow_level && fy_parse_peek(fyp) == ':')
			fy_error_report(fyp, &ec, "invalid block mapping key on same line as previous key");
		else
			fy_error_report(fyp, &ec, "invalid value after mapping");
		goto err_out;
	}
	if (fyt->type == FYTT_BLOCK_SEQUENCE_START) {
		fy_error_report(fyp, &ec, "wrong indendation in sequence while in mapping");
		goto err_out;
	}
	if (fyt->type == FYTT_ANCHOR) {
		fy_error_report(fyp, &ec, "two anchors for a single value while in mapping");
		goto err_out;
	}
	if (fyt->type == FYTT_BLOCK_MAPPING_START) {
		if (!fyp->flow_level && fyp->last_block_mapping_key_line == fy_token_start_line(fyt))
			fy_error_report(fyp, &ec, "invalid nested block mapping on the same line");
		else
			fy_error_report(fyp, &ec, "invalid indentation in mapping");
		goto err_out;
	}
	if (fyt->type == FYTT_ALIAS) {
		fy_error_report(fyp, &ec, "invalid combination of anchor plus alias");
		goto err_out;
	}

	fy_error_report(fyp, &ec, "did not find expected key");
	goto err_out;

err_missing_document_start:
	if (fyt->type == FYTT_BLOCK_MAPPING_START) {
		fyds = fyp->current_document_state;

		if (fyds && !fyds->start_implicit && fyds->start_mark.line == fy_token_start_line(fyt)) {
			fy_error_report(fyp, &ec, "invalid mapping starting at --- line");
			goto err_out;
		}

		fy_error_report(fyp, &ec, "invalid mapping in plain multiline");
		goto err_out;
	}
	fy_error_report(fyp, &ec, "missing document start");
	goto err_out;

err_stream_end_on_flow_seq:
	fy_error_report(fyp, &ec, "flow sequence without a closing bracket");
	goto err_out;

err_missing_block_seq_indicator:
	if (fyt->type == FYTT_SCALAR) {
		fy_error_report(fyp, &ec, "invalid scalar at the end of block sequence");
		goto err_out;
	}
	if (fyt->type == FYTT_BLOCK_SEQUENCE_START) {
		fy_error_report(fyp, &ec, "wrongly indented sequence item");
		goto err_out;
	}
	fy_error_report(fyp, &ec, "did not find expected '-' indicator");
	goto err_out;

err_directives_without_doc:
	fy_error_report(fyp, &ec, "directive(s) without a document");
	goto err_out;

err_missing_comma:
	fy_error_report(fyp, &ec, "missing comma in flow %s",
			fyp->state == FYPS_FLOW_SEQUENCE_ENTRY ? "sequence" : "mapping");
	goto err_out;

err_missing_doc_end_before_directives:
	fy_error_report(fyp, &ec, "missing explicit document end marker before directive(s)");
	goto err_out;
}

const char *fy_event_type_txt[] = {
	[FYET_NONE]		= "NONE",
	[FYET_STREAM_START]	= "+STR",
	[FYET_STREAM_END]	= "-STR",
	[FYET_DOCUMENT_START]	= "+DOC",
	[FYET_DOCUMENT_END]	= "-DOC",
	[FYET_MAPPING_START]	= "+MAP",
	[FYET_MAPPING_END]	= "-MAP",
	[FYET_SEQUENCE_START]	= "+SEQ",
	[FYET_SEQUENCE_END]	= "-SEQ",
	[FYET_SCALAR]		= "=VAL",
	[FYET_ALIAS]		= "=ALI",
};

struct fy_eventp *fy_parse_private(struct fy_parser *fyp)
{
	struct fy_eventp *fyep = NULL;

	fyep = fy_parse_internal(fyp);
	fy_parse_debug(fyp, "> %s", fyep ? fy_event_type_txt[fyep->e.type] : "NULL");

	return fyep;
}

void *fy_parse_alloc(struct fy_parser *fyp, size_t size)
{
	return fy_talloc(&fyp->tallocs, size);
}

void fy_parse_free(struct fy_parser *fyp, void *data)
{
	fy_tfree(&fyp->tallocs, data);
}

char *fy_parse_strdup(struct fy_parser *fyp, const char *str)
{
	size_t len;
	char *copy;

	len = strlen(str);
	copy = fy_parse_alloc(fyp, len + 1);
	if (!copy)
		return NULL;
	memcpy(copy, str, len + 1);
	return copy;
}

struct fy_parser *fy_parser_create(const struct fy_parse_cfg *cfg)
{
	struct fy_parser *fyp;
	int rc;

	if (!cfg)
		return NULL;

	fyp = malloc(sizeof(*fyp));
	if (!fyp)
		return NULL;

	rc = fy_parse_setup(fyp, cfg);
	if (rc) {
		free(fyp);
		return NULL;
	}

	/* pretend it's a fy_parser */
	return fyp;
}

void fy_parser_destroy(struct fy_parser *fyp)
{
	if (!fyp)
		return;

	fy_parse_cleanup(fyp);

	free(fyp);
}

int fy_parser_set_input_file(struct fy_parser *fyp, const char *file)
{
	struct fy_input_cfg *fyic;
	int rc;

	if (!fyp || !file)
		return -1;

	fyic = fy_parse_alloc(fyp, sizeof(*fyic));
	fy_error_check(fyp, fyic, err_out,
			"fy_parse_alloc() failed");
	memset(fyic, 0, sizeof(*fyic));

	if (!strcmp(file, "-")) {
		fyic->type = fyit_stream;
		fyic->stream.name = "stdin";
		fyic->stream.fp = stdin;
	} else {
		fyic->type = fyit_file;
		fyic->file.filename = fy_parse_strdup(fyp, file);
		fy_error_check(fyp, fyic->file.filename, err_out,
			"fy_parse_strdup() failed");
	}

	rc = fy_parse_input_reset(fyp);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_input_parse_reset() failed");

	rc = fy_parse_input_append(fyp, fyic);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_parse_input_append() failed");

	return 0;
err_out:
	rc = -1;
err_out_rc:
	return -1;
}

int fy_parser_set_string(struct fy_parser *fyp, const char *str)
{
	struct fy_input_cfg *fyic;
	int rc;

	if (!fyp || !str)
		return -1;

	fyic = fy_parse_alloc(fyp, sizeof(*fyic));
	fy_error_check(fyp, fyic, err_out,
			"fy_parse_alloc() failed");
	memset(fyic, 0, sizeof(*fyic));

	fyic->type = fyit_memory;
	fyic->memory.data = str;
	fyic->memory.size = strlen(str);

	rc = fy_parse_input_reset(fyp);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_input_parse_reset() failed");

	rc = fy_parse_input_append(fyp, fyic);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_parse_input_append() failed");

	return 0;
err_out:
	rc = -1;
err_out_rc:
	return -1;
}

int fy_parser_set_input_fp(struct fy_parser *fyp, const char *name, FILE *fp)
{
	struct fy_input_cfg *fyic;
	int rc;

	if (!fyp || !fp)
		return -1;

	fyic = fy_parse_alloc(fyp, sizeof(*fyic));
	fy_error_check(fyp, fyic, err_out,
			"fy_parse_alloc() failed");
	memset(fyic, 0, sizeof(*fyic));

	fyic->type = fyit_stream;
	fyic->stream.name = name ? : "<stream>";
	fyic->stream.fp = fp;

	rc = fy_parse_input_reset(fyp);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_input_parse_reset() failed");

	rc = fy_parse_input_append(fyp, fyic);
	fy_error_check(fyp, !rc, err_out_rc,
			"fy_parse_input_append() failed");

	return 0;
err_out:
	rc = -1;
err_out_rc:
	return -1;
}

void *fy_parser_alloc(struct fy_parser *fyp, size_t size)
{
	if (!fyp)
		return NULL;

	return fy_parse_alloc(fyp, size);
}

void fy_parser_free(struct fy_parser *fyp, void *data)
{
	if (!fyp || !data)
		return;

	fy_parse_free(fyp, data);
}

struct fy_event *fy_parser_parse(struct fy_parser *fyp)
{
	struct fy_eventp *fyep;

	if (!fyp)
		return NULL;

	fyep = fy_parse_private(fyp);
	if (!fyep)
		return NULL;

	return &fyep->e;
}

void fy_parser_event_free(struct fy_parser *fyp, struct fy_event *fye)
{
	struct fy_eventp *fyep;

	if (!fyp || !fye)
		return;

	fyp = (struct fy_parser *)fyp;

	fyep = container_of(fye, struct fy_eventp, e);

	assert(fyep->fyp == fyp);

	fy_parse_eventp_recycle(fyep->fyp, fyep);
}

bool fy_parser_get_stream_error(struct fy_parser *fyp)
{
	if (!fyp)
		return true;

	return fyp->stream_error;
}

bool fy_document_event_is_implicit(const struct fy_event *fye)
{
	if (fye->type == FYET_DOCUMENT_START)
		return fye->document_start.implicit;

	if (fye->type == FYET_DOCUMENT_END)
		return fye->document_end.implicit;

	return false;
}

struct fy_token *fy_document_event_get_token(struct fy_event *fye)
{
	switch (fye->type) {
	case FYET_NONE:
		break;

	case FYET_STREAM_START:
		return fye->stream_start.stream_start;

	case FYET_STREAM_END:
		return fye->stream_end.stream_end;

	case FYET_DOCUMENT_START:
		return fye->document_start.document_start;

	case FYET_DOCUMENT_END:
		return fye->document_end.document_end;

	case FYET_MAPPING_START:
		return fye->mapping_start.mapping_start;

	case FYET_MAPPING_END:
		return fye->mapping_end.mapping_end;

	case FYET_SEQUENCE_START:
		return fye->sequence_start.sequence_start;

	case FYET_SEQUENCE_END:
		return fye->sequence_end.sequence_end;

	case FYET_SCALAR:
		return fye->scalar.value;

	case FYET_ALIAS:
		return fye->alias.anchor;

	}

	return NULL;
}

void fy_error_vreport(struct fy_parser *fyp, struct fy_error_ctx *fyec, const char *fmt, va_list ap)
{
	struct fy_input *fyi;
	const char *name;
	bool do_color;
	const void *s, *e, *rs, *re, *rp, *rpe;
	int ww, cc;
	FILE *fp;

	fyi = fyec->fyi;
	assert(fyi);
	fp = fy_parser_get_error_fp(fyp);
	do_color = fy_parser_is_colorized(fyp);

	if (fyi->cfg.type == fyit_file) {
		name = fyi->cfg.file.filename;
	} else if (fyi->cfg.type == fyit_stream) {
		if (fyi->cfg.stream.fp == stdin)
			name = "<stdin>";
		else
			name = fyi->cfg.stream.name;
	} else
		name = NULL;

	if (do_color)
		fprintf(fp, "\x1b[37;1m");	/* white */
	if (name)
		fprintf(fp, "%s:", name);
	fprintf(fp, "%d:%d: ",
			fyec->start_mark.line + 1, fyec->start_mark.column + 1);
	if (do_color)
		fprintf(fp, "\x1b[31;1m");	/* red */
	fprintf(fp, "%s: ",
			"error");
	if (do_color)
		fprintf(fp, "\x1b[0m");	/* reset */
	vfprintf(fp, fmt, ap);
	fprintf(fp, "\n");

	s = fy_input_start(fyi);
	e = s + fy_input_size(fyi);

	rp = s + fyec->start_mark.input_pos;
	rpe = s + fyec->end_mark.input_pos;
	rs = rp;
	re = fy_find_lb(rp, e - rp);
	if (!re)
		re = e;
	if (rpe > re)
		rpe = re;

	while (rs > s) {
		cc = fy_utf8_get_right(s, (int)(rs - s), &ww);
		if (cc <= 0 || fy_is_lb(cc))
			break;
		rs -= ww;
	}

	fwrite(rs, re - rs, 1, fp);
	fprintf(fp, "\n");

	for (s = rs; (cc = fy_utf8_get(s, rp - s, &ww)) > 0; s += ww) {
		if (fy_is_blank(cc))
			fwrite(s, ww, 1, fp);
		else
			fputc(' ', fp);
	}
	if (do_color)
		fprintf(fp, "\x1b[32;1m");	/* green */

	fputc('^', fp);
	cc = fy_utf8_get(s, re - s, &ww);
	s += ww;
	for (; (cc = fy_utf8_get(s, rpe - s, &ww)) > 0; s += ww)
		fputc('~', fp);

	if (do_color)
		fprintf(fp, "\x1b[0m");	/* reset */
	fprintf(fp, "\n");

	if (fyp && !fyp->stream_error)
		fyp->stream_error = true;
}

void fy_error_report(struct fy_parser *fyp, struct fy_error_ctx *fyec, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fy_error_vreport(fyp, fyec, fmt, ap);
	va_end(ap);
}

FILE *fy_parser_get_error_fp(struct fy_parser *fyp)
{
	if (!fyp || !(fyp->cfg.flags & FYPCF_COLLECT_DIAG))
		return stderr;

	if (fyp->errfp)
		return fyp->errfp;

	fyp->errfp = open_memstream(&fyp->errbuf, &fyp->errsz);
	if (!fyp->errfp) {
		/* if this happens we are out of memory anyway */
		fprintf(stderr, "Unable to open error memstream!");
		abort();
	}
	return fyp->errfp;
}

static enum fy_parse_cfg_flags default_parser_cfg_flags =
	FYPCF_QUIET | FYPCF_DEBUG_LEVEL_WARNING |
	FYPCF_DEBUG_DIAG_TYPE | FYPCF_COLOR_NONE;

void fy_set_default_parser_cfg_flags(enum fy_parse_cfg_flags pflags)
{
	default_parser_cfg_flags = pflags;
}

enum fy_parse_cfg_flags fy_parser_get_cfg_flags(const struct fy_parser *fyp)
{
	if (!fyp)
		return default_parser_cfg_flags;

	return fyp->cfg.flags;
}

bool fy_parser_is_colorized(struct fy_parser *fyp)
{
	unsigned int color_flags;

	if (!fyp)
		return false;

	/* never colorize when collecting */
	if (fyp->cfg.flags & FYPCF_COLLECT_DIAG)
		return false;

	color_flags = fyp->cfg.flags & FYPCF_COLOR(FYPCF_COLOR_MASK);
	if (color_flags == FYPCF_COLOR_AUTO)
		return isatty(fileno(stderr));

	return color_flags == FYPCF_COLOR_FORCE;
}
