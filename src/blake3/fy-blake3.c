#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <stdlib.h>
#include <errno.h>

#include "blake3.h"
#include "blake3_impl.h"

#include <libfyaml.h>

struct fy_blake3_hasher {
	uint8_t output[FY_BLAKE3_OUT_LEN] FY_CACHELINE_ALIGN;
	struct fy_blake3_hasher_cfg cfg;
	struct blake3_host_state *hs;
	struct blake3_hasher *hasher;
};

struct fy_blake3_hasher *fy_blake3_hasher_create(const struct fy_blake3_hasher_cfg *cfg)
{
	struct fy_blake3_hasher *fyh = NULL;
	blake3_host_config hs_cfg;

	fyh = malloc(sizeof(*fyh));
	if (!fyh)
		goto err_out;

	memset(fyh, 0, sizeof(*fyh));

	/* copy config - if it exists */
	if (cfg)
		fyh->cfg = *cfg;

	memset(&hs_cfg, 0, sizeof(hs_cfg));
	hs_cfg.debug = false;
	hs_cfg.backend = cfg->backend;
	if (!cfg->tp) {
		if (cfg->num_threads >= 0)
			hs_cfg.num_threads = (unsigned int)cfg->num_threads;
		else
			hs_cfg.no_mthread = true;
	} else
		hs_cfg.tp = cfg->tp;
	hs_cfg.no_mmap = cfg->no_mmap;
	hs_cfg.file_io_bufsz = cfg->file_buffer;
	hs_cfg.mmap_min_chunk = cfg->mmap_min_chunk;
	hs_cfg.mmap_max_chunk = cfg->mmap_max_chunk;
	fyh->hs = blake3_host_state_create(&hs_cfg);
	if (!fyh->hs)
		goto err_out;

	fyh->hasher = blake3_hasher_create(fyh->hs, fyh->cfg.key, fyh->cfg.context, fyh->cfg.context ? fyh->cfg.context_len : 0);
	if (!fyh->hasher)
		goto err_out;

	return fyh;

err_out:
	fy_blake3_hasher_destroy(fyh);
	return NULL;
}

void fy_blake3_hasher_destroy(struct fy_blake3_hasher *fyh)
{
	if (!fyh)
		return;
	if (fyh->hasher)
		blake3_hasher_destroy(fyh->hasher);
	if (fyh->hs)
		blake3_host_state_destroy(fyh->hs);
	free(fyh);
}

void fy_blake3_hasher_update(struct fy_blake3_hasher *fyh, const void *input, size_t input_len)
{
	if (!fyh)
		return;
	blake3_hasher_update(fyh->hasher, input, input_len);
}

const uint8_t *fy_blake3_hasher_finalize(struct fy_blake3_hasher *fyh)
{
	if (!fyh)
		return NULL;
	blake3_hasher_finalize(fyh->hasher, fyh->output, FY_BLAKE3_OUT_LEN);
	return fyh->output;
}

void fy_blake3_hasher_reset(struct fy_blake3_hasher *fyh)
{
	if (!fyh)
		return;
	blake3_hasher_reset(fyh->hasher);
}

const uint8_t *fy_blake3_hash_file(struct fy_blake3_hasher *fyh, const char *filename)
{
	int rc;

	if (!fyh || !filename)
		return NULL;

	rc = blake3_hash_file(fyh->hasher, filename, fyh->output);
	if (rc)
		return NULL;

	return fyh->output;
}

const uint8_t *fy_blake3_hash(struct fy_blake3_hasher *fyh, const void *mem, size_t size)
{
	if (!fyh || !mem)
		return NULL;

	blake3_hash(fyh->hasher, mem, size, fyh->output);

	return fyh->output;
}

const char *fy_blake3_backend_iterate(const char **prevp)
{
	const char *backend_names[64];	/* maximum 64 backends + 1 for NULL */
	const blake3_backend_info *bei;
	uint64_t backends, avail;
	unsigned int i, count;

	if (!prevp)
		return NULL;

	avail = blake3_get_selectable_backends() &
		blake3_get_detected_backends();

	/* first pass, fill the backend name array */
	for (i = 0, backends = avail, count = 0; backends && i < B3BID_COUNT; i++) {
		if (!(backends & ((uint64_t)1 << i)))
			continue;
		bei = blake3_get_backend_info(i);
		if (!bei)
			continue;
		backends &= ~((uint64_t)1 << i);
		backend_names[count++] = bei->name;
	}

	/* start of the iterator */
	if (!*prevp) {
		if (!count)
			return NULL;
		i = 0;

	} else {

		/* ok, find the current spot */
		for (i = 0; i < count; i++) {
			if (!strcmp(*prevp, backend_names[i]))
				break;
		}

		/* increase for next, last or not found? */
		if (++i >= count) {
			*prevp = NULL;
			return NULL;
		}
	}

	return *prevp = backend_names[i];
}
