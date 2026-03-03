/*
 * libfyaml-allocator.h - libfyaml allocator API
 * Copyright (c) 2019-2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef LIBFYAML_ALLOCATOR_H
#define LIBFYAML_ALLOCATOR_H

#include <stddef.h>

/* pull in common definitions and platform abstraction macros */
#include <libfyaml/libfyaml-util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOC: Pluggable memory allocator interface
 *
 * This header exposes libfyaml's pluggable allocator subsystem.  Rather than
 * using ``malloc``/``free`` directly, the library routes certain internal
 * allocations through a ``struct fy_allocator``.
 * This lets callers trade memory footprint, speed,
 * and deduplication behaviour to match their workload.
 *
 * **Available strategies** (select by name when calling
 * ``fy_allocator_create()``):
 *
 * - ``"linear"`` — bump-pointer arena.  Allocation is O(1) and
 *   near-zero overhead; individual frees are a no-op.  Ideal for
 *   parse-and-discard workflows where the entire arena is released at once.
 *
 * - ``"malloc"`` — thin wrapper around the system ``malloc``/``free``.
 *   Familiar semantics; useful when individual node lifetimes vary.
 *   Should never be used for regular application builds. The presence
 *   of it is for having a ASAN/valgrind compatible allocator where
 *   buffer overflows can be detected easily.
 *
 * - ``"mremap"`` — growable linear arena backed by ``mremap(2)``.
 *   Avoids copying when the arena needs to grow.
 *   While mremap is Linux specific, this allocator can be configured
 *   to use mmap() or malloc() areas where they work for other platforms.
 *
 * - ``"dedup"`` — content-addressed store built on xxhash hashing.
 *   Stores each unique byte sequence exactly once and returns a shared
 *   pointer to all callers.  Dramatically reduces memory use when parsing
 *   documents with many repeated keys or values (e.g. large YAML
 *   configurations, test-suite corpora).
 *
 * - ``"auto"`` — heuristic selection: given a policy. It usually can
 *   do the right thing and is a safe bet.
 *
 * **Tags** partition an allocator's address space.  Obtain a tag with
 * ``fy_allocator_get_tag()`` and pass it to every ``fy_allocator_alloc()``
 * / ``fy_allocator_store()`` call; release the whole tag's memory in one
 * shot with ``fy_allocator_release_tag()``.  This maps naturally to
 * document lifetimes.
 *
 * **In-place variants** (``fy_linear_allocator_create_in_place()``,
 * ``fy_dedup_allocator_create_in_place()``) initialise the allocator
 * inside a caller-supplied buffer — zero dynamic setup cost, useful in
 * stack-allocated or shared-memory contexts.
 */

/* forward decl of allocator interfaces */
struct fy_allocator;

/* A tag that represents the default tag */
#define FY_ALLOC_TAG_DEFAULT	0
/* A tag that denotes error */
#define FY_ALLOC_TAG_ERROR	-1
/* A tag that represents 'none' */
#define FY_ALLOC_TAG_NONE	-2

/**
 * fy_allocator_iterate() - Iterate over available allocator names
 *
 * This method iterates over all the available allocator names.
 * The start of the iteration is signalled by a NULL in \*prevp.
 *
 * @prevp: The previous allocator iterator pointer
 *
 * Returns:
 * The next allocator name in sequence or NULL at the end.
 */
const char *
fy_allocator_iterate(const char **prevp)
	FY_EXPORT;

/**
 * fy_allocator_is_available() - Check if an allocator is available
 *
 * Check if the named allocator is available.
 *
 * @name: The name of the allocator to check
 *
 * Returns:
 * true if the allocator is available, false otherwise
 */
bool
fy_allocator_is_available(const char *name)
	FY_EXPORT;

/**
 * fy_allocator_create() - Create an allocator.
 *
 * Creates an allocator of the given type, using the configuration
 * argument provided.
 * The allocator may be destroyed by a corresponding call to
 * fy_allocator_destroy().
 *
 * You can retrieve the names of available allocators
 * with the fy_allocator_get_names() method.
 *
 * @name: The name of the allocator
 * @cfg: The type specific configuration for the allocator, or NULL
 *       for the default.
 *
 * Returns:
 * A pointer to the allocator or NULL in case of an error.
 */
struct fy_allocator *
fy_allocator_create(const char *name, const void *cfg)
	FY_EXPORT;

/**
 * fy_allocator_destroy() - Destroy the given allocator
 *
 * Destroy an allocator created earlier via fy_allocator_create().
 * Tracking allocators will release all memory allocated using them.
 *
 * @a: The allocator to destroy
 */
void
fy_allocator_destroy(struct fy_allocator *a)
	FY_EXPORT;

/** The minimum amount of memory for an inplace linear allocator */
#define FY_LINEAR_ALLOCATOR_IN_PLACE_MIN_SIZE	256

/**
 * fy_linear_allocator_create_in_place() - Create a linear allocator in place
 *
 * Creates a linear allocator in place, using the buffer provided.
 * No memory allocations will be performed, so it's safe to embed.
 * There is no need to call fy_allocator_destroy for this allocator.
 *
 * @buffer: The memory buffer to use for both storage and the allocator
 * @size: The size of the memory buffer
 *
 * Returns:
 * A pointer to the allocator, or NULL if there is no space
 */
struct fy_allocator *
fy_linear_allocator_create_in_place(void *buffer, size_t size)
	FY_EXPORT;

/** The minimum amount of memory for an inplace dedup allocator */
#define FY_DEDUP_ALLOCATOR_IN_PLACE_MIN_SIZE	4096

/**
 * fy_dedup_allocator_create_in_place() - Create a dedup allocator in place
 *
 * Creates a dedup allocator in place, using the buffer provided.
 * No memory allocations will be performed, so it's safe to embed.
 * There is no need to call fy_allocator_destroy for this allocator.
 * The parent allocator of this will be a linear allocator.
 *
 * @buffer: The memory buffer to use for both storage and the allocator
 * @size: The size of the memory buffer
 *
 * Returns:
 * A pointer to the allocator, or NULL if there is no space
 */
struct fy_allocator *
fy_dedup_allocator_create_in_place(void *buffer, size_t size)
	FY_EXPORT;

/**
 * fy_allocator_get_tag() - Get a tag from an allocator
 *
 * The allocator interface requires all allocation to belong
 * to a tag. This call creates a tag and returns its value,
 * or an error if not available.
 *
 * If an allocator only provides a single tag (like the linear
 * allocator for instance), the same tag number, usually 0, is
 * returned.
 *
 * @a: The allocator
 *
 * Returns:
 * The created tag or -1 in case of an error.
 */
int
fy_allocator_get_tag(struct fy_allocator *a)
	FY_EXPORT;

/**
 * fy_allocator_release_tag() - Release a tag from an allocator
 *
 * Releases a tag from an allocator and frees all memory it
 * allocated (if such an operation is provided by the allocator).
 *
 * @a: The allocator
 * @tag: The tag to release
 */
void
fy_allocator_release_tag(struct fy_allocator *a, int tag)
	FY_EXPORT;

/**
 * fy_allocator_get_tag_count() - Get the maximum number of tags a
 * 				  allocator supports
 *
 * Get the maximum amount of tags an allocator supports.
 *
 * If an allocator only provides a single tag (like the linear
 * allocator for instance), 1 will be returned.
 *
 * @a: The allocator
 *
 * Returns:
 * The number of tags, or -1 on error
 */
int
fy_allocator_get_tag_count(struct fy_allocator *a)
	FY_EXPORT;

/**
 * fy_allocator_set_tag_count() - Set the maximum number of tags a
 * 				  allocator supports
 *
 * Sets the maximum amount of tags an allocator supports.
 * If the set allocator tag count is less than the current
 * the additional tags will be released.
 *
 * @a: The allocator
 * @count: The amount of tags the allocator should support
 *
 * Returns:
 * 0 on success, -1 on error
 */
int
fy_allocator_set_tag_count(struct fy_allocator *a, unsigned int count)
	FY_EXPORT;

/**
 * fy_allocator_trim_tag() - Trim a tag
 *
 * Trim a tag, that is free any excess memory it allocator, fitting
 * it to the size of the content it carries.
 * Allocators that cannot perform this operation treat it as a NOP.
 *
 * @a: The allocator
 * @tag: The tag to trim
 */
void
fy_allocator_trim_tag(struct fy_allocator *a, int tag)
	FY_EXPORT;

/**
 * fy_allocator_reset_tag() - Reset a tag
 *
 * Reset a tag, that is free any content it carries, but do not
 * release the tag.
 *
 * @a: The allocator
 * @tag: The tag to reset
 */
void
fy_allocator_reset_tag(struct fy_allocator *a, int tag)
	FY_EXPORT;

/**
 * fy_allocator_alloc() - Allocate memory from an allocator
 *
 * Allocate memory from the given allocator tag, satisfying the
 * size and align restrictions.
 *
 * @a: The allocator
 * @tag: The tag to allocate from
 * @size: The size of the memory to allocate
 * @align: The alignment of the object
 *
 * Returns:
 * A pointer to the allocated memory or NULL
 */
void *
fy_allocator_alloc(struct fy_allocator *a, int tag, size_t size, size_t align)
	FY_EXPORT;

/**
 * fy_allocator_free() - Free memory allocated from an allocator
 *
 * Attempt to free the memory allocated previously by fy_allocator_alloc()
 * Note that non per object tracking allocators treat this as a NOP
 *
 * @a: The allocator
 * @tag: The tag used to allocate the memory
 * @ptr: The pointer to the memory to free
 */
void
fy_allocator_free(struct fy_allocator *a, int tag, void *ptr)
	FY_EXPORT;

/**
 * fy_allocator_store() - Store an object to an allocator
 *
 * Store an object to an allocator and return a pointer to the location
 * it was stored. When using a deduplicating allocator no new allocation
 * will take place and a pointer to the object already stored will be
 * returned.
 *
 * The return pointer must not be modified, the objects stored are idempotent.
 *
 * @a: The allocator
 * @tag: The tag used to allocate the memory
 * @data: The pointer to object to store
 * @size: The size of the object
 * @align: The alignment restriction of the object
 *
 * Returns:
 * A constant pointer to the object stored, or NULL in case of an error
 */
const void *
fy_allocator_store(struct fy_allocator *a, int tag, const void *data, size_t size, size_t align)
	FY_EXPORT;

/**
 * fy_allocator_storev() - Store an object to an allocator (scatter gather)
 *
 * Store an object to an allocator and return a pointer to the location
 * it was stored. When using a deduplicating allocator no new allocation
 * will take place and a pointer to the object already stored will be
 * returned.
 *
 * The object is created linearly from the scatter gather io vector provided.
 *
 * The return pointer must not be modified, the objects stored are immutable.
 *
 * @a: The allocator
 * @tag: The tag used to allocate the memory from
 * @iov: The I/O scatter gather vector
 * @iovcnt: The number of vectors
 * @align: The alignment restriction of the object
 *
 * Returns:
 * A constant pointer to the object stored, or NULL in case of an error
 */
const void *
fy_allocator_storev(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align)
	FY_EXPORT;

/**
 * fy_allocator_lookup() - Lookup for object in an allocator.
 *
 * Lookup for the exact contents of an object stored in an allocator
 * and return a pointer to the location it was stored.
 * The allocator must have the FYACF_CAN_LOOKUP capability.
 *
 * @a: The allocator
 * @tag: The tag used to locate the memory
 * @data: The pointer to object to store
 * @size: The size of the object
 * @align: The alignment restriction of the object
 *
 * Returns:
 * A constant pointer to the object stored, or NULL if the object does not exist
 */
const void *
fy_allocator_lookup(struct fy_allocator *a, int tag, const void *data, size_t size, size_t align)
	FY_EXPORT;

/**
 * fy_allocator_lookupv() - Lookup for object in an allocator (scatter gather)
 *
 * Lookup for the exact contents of an object stored in an allocator
 * and return a pointer to the location it was stored.
 * The allocator must have the FYACF_CAN_LOOKUP capability.
 *
 * The scatter gather vector is used to recreate the object.
 *
 * @a: The allocator
 * @tag: The tag used to search into
 * @iov: The I/O scatter gather vector
 * @iovcnt: The number of vectors
 * @align: The alignment restriction of the object
 *
 * Returns:
 * A constant pointer to the object stored, or NULL in case the object does not exist
 */
const void *
fy_allocator_lookupv(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align)
	FY_EXPORT;

/**
 * fy_allocator_dump() - Dump internal allocator state
 *
 * @a: The allocator
 */
void
fy_allocator_dump(struct fy_allocator *a)
	FY_EXPORT;

/**
 * enum fy_allocator_cap_flags - Allocator capability flags
 *
 * @FYACF_CAN_FREE_INDIVIDUAL: Allocator supports freeing individual allocations
 * @FYACF_CAN_FREE_TAG: Allocator supports releasing entire tags
 * @FYACF_CAN_DEDUP: Allocator supports deduplication
 * @FYACF_HAS_CONTAINS: Allocator can report if it contains a pointer (even if inefficiently)
 * @FYACF_HAS_EFFICIENT_CONTAINS: Allocator can report if it contains a pointer (efficiently)
 * @FYACF_HAS_TAGS: Allocator has individual tags or not
 * @FYACF_CAN_LOOKUP: Allocator supports lookup for content
 *
 * These flags describe what operations an allocator supports.
 */
enum fy_allocator_cap_flags {
	FYACF_CAN_FREE_INDIVIDUAL		= FY_BIT(0),
	FYACF_CAN_FREE_TAG			= FY_BIT(1),
	FYACF_CAN_DEDUP				= FY_BIT(2),
	FYACF_HAS_CONTAINS			= FY_BIT(3),
	FYACF_HAS_EFFICIENT_CONTAINS		= FY_BIT(4),
	FYACF_HAS_TAGS				= FY_BIT(5),
	FYACF_CAN_LOOKUP			= FY_BIT(6),
};

/**
 * fy_allocator_get_caps() - Get allocator capabilities
 *
 * Retrieve the capabilities of an allocator.
 *
 * @a: The allocator
 *
 * Returns:
 * The capabilities of the allocator
 */
enum fy_allocator_cap_flags
fy_allocator_get_caps(struct fy_allocator *a)
	FY_EXPORT;

/**
 * fy_allocator_contains() - Check if a allocator contains a pointer
 *
 * Report if an allocator contains the pointer
 *
 * @a: The allocator
 * @tag: Tag to search in, -1 for all
 * @ptr: The object pointer
 *
 * Returns:
 * true if the pointer ptr is contained in the allocator, false otherwise
 */
bool
fy_allocator_contains(struct fy_allocator *a, int tag, const void *ptr)
	FY_EXPORT;

/**
 * fy_allocator_get_tag_linear_size() - Get the linear size of an allocator tag
 *
 * Retrieve the linear size of the content of a tag.
 * That is the size of a buffer if one was to copy the content of the tag in
 * that buffer in a linear manner.
 *
 * @a: The allocator
 * @tag: The tag
 *
 * Returns:
 * The linear size of the content stored in the tag or -1 in case of an error.
 */
ssize_t
fy_allocator_get_tag_linear_size(struct fy_allocator *a, int tag)
	FY_EXPORT;

/**
 * fy_allocator_get_tag_single_linear() - Get the linear extend of a tag
 *
 * If a tag stores it's content in a single linear buffer, retrieve it
 * directly. This is possible only under careful arrangement of allocator
 * configuration, but it is an important optimization case.
 *
 * @a: The allocator
 * @tag: The tag
 * @sizep: Pointer to a variable that will be filled with the size.
 *
 * Returns:
 * A pointer to the linear content of the tag, or NULL if othersize.
 */
const void *
fy_allocator_get_tag_single_linear(struct fy_allocator *a, int tag, size_t *sizep)
	FY_EXPORT;

/**
 * struct fy_linear_allocator_cfg - linear allocator configuration
 *
 * @buf: A pointer to a buffer that will be used, or NULL in order to allocate
 * @size: Size of the buffer in bytes
 */
struct fy_linear_allocator_cfg {
	void *buf;
	size_t size;
};

/**
 * enum fy_mremap_arena_type - The mremap allocator arena types
 *
 * @FYMRAT_DEFAULT: Use what's optimal for this platform
 * @FYMRAT_MALLOC: Use malloc/realloc arena type (not recommended)
 * @FYMRAT_MMAP: Use mmap/mremap arena type
 *
 */
enum fy_mremap_arena_type {
	FYMRAT_DEFAULT,
	FYMRAT_MALLOC,
	FYMRAT_MMAP,
};

/**
 * struct fy_mremap_allocator_cfg - mremap allocator configuration
 *
 * If any of the fields is zero, then the system will provide (somewhat)
 * reasonable defaults.
 *
 * @big_alloc_threshold: Threshold for immediately creating a new arena.
 * @empty_threshold: The threshold under which an arena is moved to the full list.
 * @minimum_arena_size: The minimum (and starting size) of an arena.
 * @grow_ratio: The ratio which an arena will try to grow if full (>1.0)
 * @balloon_ratio: The multiplier for the vm area first allocation
 * @arena_type: The arena type
 */
struct fy_mremap_allocator_cfg {
	size_t big_alloc_threshold;
	size_t empty_threshold;
	size_t minimum_arena_size;
	float grow_ratio;
	float balloon_ratio;
	enum fy_mremap_arena_type arena_type;
};

/* malloc allocator has no configuration data, pass NULL */

/**
 * struct fy_dedup_allocator_cfg - dedup allocator configuration
 *
 * @parent_allocator: The parent allocator (required)
 * @bloom_filter_bits: Number of bits of the bloom filter (or 0 for default)
 * @bucket_count_bits: Number of bits for the bucket count (or 0 for default)
 * @dedup_threshold: Number of bytes over which dedup takes place (default 0=always)
 * @chain_length_grow_trigger: Chain length of a bucket over which a grow takes place (or 0 for auto)
 * @estimated_content_size: Estimated content size (or 0 for don't know)
 * @minimum_bucket_occupancy: The minimum amount that a tag bucket must be full before
 * 			      growth is allowed (default 50%, or 0.0)
 */
struct fy_dedup_allocator_cfg {
	struct fy_allocator *parent_allocator;
	unsigned int bloom_filter_bits;
	unsigned int bucket_count_bits;
	size_t dedup_threshold;
	unsigned int chain_length_grow_trigger;
	size_t estimated_content_size;
	float minimum_bucket_occupancy;
};

/**
 * enum fy_auto_allocator_scenario_type - auto allocator scenario type
 *
 * @FYAST_PER_TAG_FREE: only per tag freeing, no individual obj free
 * @FYAST_PER_TAG_FREE_DEDUP: per tag freeing, dedup obj store
 * @FYAST_PER_OBJ_FREE:	object freeing allowed, tag freeing still works
 * @FYAST_PER_OBJ_FREE_DEDUP: per obj freeing, dedup obj store
 * @FYAST_SINGLE_LINEAR_RANGE: just a single linear range, no frees at all
 * @FYAST_SINGLE_LINEAR_RANGE_DEDUP: single linear range, with dedup
 */
enum fy_auto_allocator_scenario_type {
	FYAST_PER_TAG_FREE,
	FYAST_PER_TAG_FREE_DEDUP,
	FYAST_PER_OBJ_FREE,
	FYAST_PER_OBJ_FREE_DEDUP,
	FYAST_SINGLE_LINEAR_RANGE,
	FYAST_SINGLE_LINEAR_RANGE_DEDUP,
};

/**
 * struct fy_auto_allocator_cfg - auto allocator configuration
 *
 * @scenario: Auto allocator scenario
 * @estimated_max_size: Estimated max content size (or 0 for don't know)
 */
struct fy_auto_allocator_cfg {
	enum fy_auto_allocator_scenario_type scenario;
	size_t estimated_max_size;
};

#ifdef __cplusplus
}
#endif

#endif /* LIBFYAML_ALLOCATOR_H */
