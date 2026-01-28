#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <stdlib.h>

#include "blake3.h"
#include "blake3_impl.h"
#include "blake3_internal.h"

#include "fy-thread.h"
#include "fy-win32.h"

struct cpusimd_data {
	unsigned int num_cpus;
	unsigned int simd_cpus;
	unsigned int be_simd_degree_mult;
	const blake3_backend *be_best;
	char *description;
	struct fy_thread_pool *tp;
};

static void blake3_cpusimd_hash_many_thread(void *arg)
{
	blake3_hash_many_state *s = arg;
	const blake3_hash_many_common_state *c;

	c = s->common;
	c->hash_many(s->inputs, s->num_inputs, c->blocks, c->key, s->counter, c->increment_counter, c->flags, c->flags_start, c->flags_end, s->out);
}

// #define CPUSIMD_CHECK

static void blake3_hash_many_cpusimd(
		const uint8_t *const *inputs, size_t num_inputs,
		size_t blocks, const uint32_t key[8],
		uint64_t counter, bool increment_counter,
		uint8_t flags, uint8_t flags_start,
		uint8_t flags_end, uint8_t *out)
{
	struct cpusimd_data *d;
	const blake3_backend *be, *be_best;
	unsigned int i, stepi, be_simd_degree_mult, simd_degree, count, states_count;
	blake3_hash_many_state *states, *s;
	blake3_hash_many_common_state common_local, *common = &common_local;
	const uint8_t *const *inputsi;
	uint8_t *outi;
	uint64_t counteri;
#ifdef CPUSIMD_CHECK
	size_t out_size;
	uint8_t *out_cmp;
#endif

	(void)simd_degree;

	be = &blake3_backends[B3BID_CPUSIMD];
	d = be->user;
	assert(d);

	count = (unsigned int)num_inputs;
	simd_degree = be->info.simd_degree;

	be_best = d->be_best;
	be_simd_degree_mult = d->be_simd_degree_mult;

#ifdef CPUSIMD_CHECK
	/* if less than the underlyind backend degree just do it there */
	out_size = BLAKE3_OUT_LEN * num_inputs;
	out_cmp = alloca(out_size);
	be_best->hash_many(inputs, num_inputs, blocks, key, counter, increment_counter, flags, flags_start, flags_end, out_cmp);
#endif

	states_count = (count / be_simd_degree_mult) + !!(count % be_simd_degree_mult);
	states = alloca(sizeof(*states) * states_count);

	/* the common */
	common->hash_many = be_best->hash_many;
	common->blocks = blocks;
	common->key = key;
	common->increment_counter = increment_counter;
	common->flags = flags;
	common->flags_start = flags_start;
	common->flags_end = flags_end;

	inputsi = inputs;
	outi = out;
	counteri = counter;
	for (i = 0, s = states; i < count; i += stepi, s++) {

		stepi = (i + be_simd_degree_mult) <= count ? be_simd_degree_mult : (count - i);

		s->inputs = inputsi;
		s->num_inputs = stepi;
		s->counter = counteri;
		s->out = outi;
		s->common = common;

		if (increment_counter)
			counteri += stepi;
		inputsi += stepi;
		outi += BLAKE3_OUT_LEN * stepi;
	}

	fy_thread_arg_array_join(d->tp, blake3_cpusimd_hash_many_thread, NULL, states, sizeof(states[0]), states_count);

#ifdef CPUSIMD_CHECK
	for (i = 0; i < count; i += stepi) {
		stepi = (i + be_simd_degree_mult) <= count ? be_simd_degree_mult : (count - i);

		if (memcmp(out + i * BLAKE3_OUT_LEN, out_cmp + i * BLAKE3_OUT_LEN, BLAKE3_OUT_LEN)) {
			fprintf(stderr, "%s: differ at #%u\n", __func__, i);
		}
	}
#endif
}

static void cpusimd_data_free(struct cpusimd_data *d)
{
	if (!d)
		return;

	if (d->description)
		free(d->description);

	if (d->tp)
		fy_thread_pool_destroy(d->tp);

	free(d);
}

int blake3_backend_cpusimd_setup(unsigned int num_cpus, unsigned int mult_fact)
{
	blake3_backend *be = &blake3_backends[B3BID_CPUSIMD];
	const blake3_backend *be_best;
	struct cpusimd_data *d = NULL;
	struct fy_thread_pool_cfg tp_cfg;
	long scval;
	uint64_t supported_backends, detected_backends, selectable_backends;
	unsigned int num_simd_cpus;
	ssize_t n;

	if (!num_cpus) {
		scval = sysconf(_SC_NPROCESSORS_ONLN);
		assert(scval > 0);
		num_cpus = (unsigned int)scval;
	}

	if (num_cpus <= 1)
		return 0;

	num_simd_cpus = (unsigned int)round_down_to_power_of_2(num_cpus);

	if (!mult_fact)
		mult_fact = 1;

	d = malloc(sizeof(*d));
	if (!d)
		goto err_out;

	memset(d, 0, sizeof(*d));

	memset(&tp_cfg, 0, sizeof(tp_cfg));
	tp_cfg.flags = 0;
	tp_cfg.num_threads = num_simd_cpus;
	tp_cfg.userdata = NULL;

	d->tp = fy_thread_pool_create(&tp_cfg);
	if (!d->tp)
		goto err_out;

	/* probe for available backends */
	supported_backends = blake3_get_supported_backends();
	detected_backends = blake3_get_detected_backends();
	selectable_backends = supported_backends & detected_backends;

	/* remove ourselves (and anything above) */
	selectable_backends &= B3BF_BIT(B3BID_CPUSIMD) - 1;

	/* select the best one for hash many */
	be_best = blake3_backend_select_function(selectable_backends, B3FID_HASH_MANY);
	assert(be_best);

	d->num_cpus = num_cpus;
	d->simd_cpus = num_simd_cpus;
	d->be_best = be_best;
	d->be_simd_degree_mult = be_best->info.simd_degree * mult_fact;

	be->hasher_ops = be_best->hasher_ops;

	d->description = NULL;
	n = 0;
	for (;;) {
		n = snprintf(d->description, n ? (n + 1) : 0, "SIMD like acceleration using %u CPUs (using %s x %u) x %u = total x %u",
				d->simd_cpus,
				d->be_best->info.name, d->be_best->info.simd_degree,
				mult_fact,
				d->be_simd_degree_mult * d->simd_cpus);
		if (d->description)
			break;

		d->description = malloc(n + 1);
		if (!d->description)
			goto err_out;
	}

	be->user = d;

	be->info.id = B3BID_CPUSIMD;
	be->info.name = "cpusimd";
	be->info.simd_degree = d->simd_cpus * d->be_best->info.simd_degree * mult_fact;
	be->info.description = d->description;

	be->hash_many = blake3_hash_many_cpusimd;
	be->info.funcs = B3FF_HASH_MANY;

	return 0;

err_out:
	cpusimd_data_free(d);
	return -1;
}

void blake3_backend_cpusimd_cleanup(void)
{
	blake3_backend *be = &blake3_backends[B3BID_CPUSIMD];
	struct cpusimd_data *d;

	d = be->user;
	if (!d)
		return;
	cpusimd_data_free(d);

	memset(be, 0, sizeof(*be));
}
