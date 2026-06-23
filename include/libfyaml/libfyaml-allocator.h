/*
 * libfyaml-allocator.h - libfyaml allocator API
 * Copyright (c) 2019-2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef LIBFYAML_ALLOCATOR_H
#define LIBFYAML_ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>

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
struct fy_thread_pool;	/* opaque; defined in libfyaml-thread.h */

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
 * @FYACF_DURABLE: Allocations persist at stable addresses across processes and
 *                 sessions (memory-mapped, fixed-base, never relocated). Pointer
 *                 identity is therefore a durable canonical identity.
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
	FYACF_DURABLE				= FY_BIT(7),
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
 * struct fy_allocator_usage - Aggregate allocator usage summary.
 *
 * @free:  Bytes still available.
 * @used:  Bytes currently allocated.
 * @total: Total size (used + free).
 */
struct fy_allocator_usage {
	size_t free;
	size_t used;
	size_t total;
};

/**
 * fy_allocator_get_usage() - Summarize allocator usage (free/used/total)
 *
 * Fill @usage with the aggregate byte counts for @tag, or for every tag when
 * @tag is FY_ALLOC_TAG_NONE.
 *
 * @a:     The allocator
 * @tag:   The tag to summarize, or FY_ALLOC_TAG_NONE for all tags
 * @usage: Receives the summary; zeroed first; never NULL
 *
 * Returns:
 * 0 on success, -1 on error or if the allocator does not support it.
 */
int
fy_allocator_get_usage(struct fy_allocator *a, int tag,
		       struct fy_allocator_usage *usage)
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
 * @minimum_bucket_occupancy: Minimum bloom-filter fill ratio before a
 * 			      chain-length-triggered grow is allowed (default 50%,
 * 			      or 0.0 to grow on the trigger alone)
 */
struct fy_dedup_allocator_cfg {
	struct fy_allocator *parent_allocator;
	unsigned int bloom_filter_bits;
	unsigned int bucket_count_bits;
	size_t dedup_threshold;
	unsigned int chain_length_grow_trigger;
	size_t estimated_content_size;
	float minimum_bucket_occupancy;
	/*
	 * Route content and index allocations to explicit parent tags rather than
	 * deriving them from the parent's tag capability. Used when the parent is
	 * a durable arena with a separate dedup-index region: @content_tag selects
	 * the content file series, @entries_tag the index file series. Ignored
	 * unless @use_explicit_tags is set.
	 */
	bool use_explicit_tags;
	int content_tag;
	int entries_tag;
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

/**
 * DOC: Durable allocator ("durable")
 *
 * The "durable" allocator is an on-disk, content-addressed allocation region
 * made of fixed-size chunk files mapped at a *fixed* virtual base address that
 * is identical across every process and every session. Because the base never
 * changes, an in-arena pointer is a stable canonical identity.
 *
 * Its durable-only operations are exposed as generic allocator calls that other
 * allocators do not support: fy_allocator_sync(), fy_allocator_refs_get() /
 * fy_allocator_refs_publish(), fy_allocator_generation(),
 * fy_allocator_chunk_count(), fy_allocator_region_base() and
 * fy_allocator_index_region_base().
 *
 * On systems that support this allocator a local filesystem is required and will
 * refuse to operate on network filesystems (NFS/SMB/FUSE), due to cross-process atomic
 * semantics.
 */

/* fy_durable_allocator_cfg.flags */
#define FY_DURABLE_ARENA_CREATE		FY_BIT(0)	/* create the directory/chunk 0 if absent */
#define FY_DURABLE_ARENA_READONLY	FY_BIT(1)	/* map read-only; no allocation/grow */
#define FY_DURABLE_ARENA_SPARSE		FY_BIT(2)	/* ftruncate chunks instead of fallocate */
#define FY_DURABLE_ARENA_DEDUP		FY_BIT(3)	/* content-address (dedup) allocations in-arena */
#define FY_DURABLE_ARENA_SEPARATE_INDEX	FY_BIT(4)	/* put the dedup index in its own file series (index-N.bin) */
#define FY_DURABLE_ARENA_CHECKPOINT	FY_BIT(5)	/* arena supports checkpointing */

/**
 * struct fy_durable_allocator_cfg - durable allocator configuration
 *
 * @dir:         Path to the arena directory (holds arena-{N}.bin chunk files).
 * @region_base: Fixed virtual base address for the whole region. 0 selects the
 *               default base of the platform.
 * @region_size: Total reserved virtual size. 0 selects the default size of the platform.
 * @chunk_size:  Size of each chunk file. 0 selects the default
 * @flags:       FY_DURABLE_ARENA_* bits.
 * @index_region_base: With FY_DURABLE_ARENA_SEPARATE_INDEX, the fixed virtual
 *               base of the separate dedup-index region (index-N.bin). 0 selects
 *               a default.
 * @index_region_size: Reserved virtual size of the index region. 0 -> default.
 * @index_chunk_size:  Size of each index chunk file. 0 -> default.
 */
struct fy_durable_allocator_cfg {
	const char *dir;
	uint64_t region_base;
	uint64_t region_size;
	uint64_t chunk_size;
	unsigned int flags;
	uint64_t index_region_base;
	uint64_t index_region_size;
	uint64_t index_chunk_size;
	struct fy_thread_pool *tp;	/* optional thread pool for BLAKE3 hashing */
};

/**
 * fy_allocator_sync() - Flush a durable arena to stable storage
 *
 * Durably persist everything allocated so far in a durable arena, the way
 * msync()/fsync() would. The operation walks the allocator's parent chain, so
 * it works whether @a is the durable base or a dedup layer over it.
 *
 * On allocators that do not support the operation (everything but "durable")
 * this is a no-op that returns -1.
 *
 * @a: The allocator (durable, or a layer over one)
 *
 * Returns:
 * 0 on success, -1 on error or if the allocator does not support syncing.
 */
int
fy_allocator_sync(struct fy_allocator *a)
	FY_EXPORT;

/**
 * fy_allocator_refs_get() - Read the durable refs head word
 *
 * Return the published refs head word of a durable arena: the single
 * application-defined root pointer/cookie that names the currently committed
 * graph. It is 0 until first published. Walks the parent chain.
 *
 * @a: The allocator (durable, or a layer over one)
 *
 * Returns:
 * The current refs head word, or 0 if unpublished / unsupported.
 */
uint64_t
fy_allocator_refs_get(struct fy_allocator *a)
	FY_EXPORT;

/* flags for fy_allocator_refs_publish() */
#define FY_ALLOC_REFS_CHECKPOINT	FY_BIT(0)	/* enforce the durability ordering barrier */

/**
 * fy_allocator_refs_publish() - Atomically publish a new durable refs head
 *
 * Compare-and-swap the durable refs head word from @expected to @desired,
 * atomically across processes and sessions. This is how a new committed root is
 * made visible to other readers of the arena.
 *
 * With FY_ALLOC_REFS_CHECKPOINT in @flags the call enforces the durability
 * ordering barrier (the arena contents are persisted before the head is
 * published), so a crash never exposes a head that points at unflushed data.
 *
 * @a: The allocator (durable, or a layer over one)
 * @expected: The refs head value expected to be current
 * @desired: The new refs head value to publish
 * @flags: FY_ALLOC_REFS_* bits
 *
 * Returns:
 * 0 if the swap succeeded, 1 if @expected did not match (no change), or -1 on
 * error / if the allocator does not support publishing.
 */
int
fy_allocator_refs_publish(struct fy_allocator *a, uint64_t expected,
			  uint64_t desired, unsigned int flags)
	FY_EXPORT;

/**
 * fy_allocator_generation() - Get the durable arena generation
 *
 * Return a monotonically increasing generation counter for a durable arena,
 * which advances as the arena grows. Walks the parent chain.
 *
 * @a: The allocator (durable, or a layer over one)
 *
 * Returns:
 * The current generation, or 0 if unsupported.
 */
uint64_t
fy_allocator_generation(struct fy_allocator *a)
	FY_EXPORT;

/**
 * fy_allocator_chunk_count() - Get the durable arena chunk count
 *
 * Return the number of chunk files currently backing a durable arena's content
 * region. Walks the parent chain.
 *
 * @a: The allocator (durable, or a layer over one)
 *
 * Returns:
 * The number of content chunks, or 0 if unsupported.
 */
unsigned int
fy_allocator_chunk_count(struct fy_allocator *a)
	FY_EXPORT;

/**
 * fy_allocator_region_base() - Get the durable content region base address
 *
 * Return the fixed virtual base address of a durable arena's content region.
 * Because the base is identical across every process and session, an in-arena
 * pointer is a stable canonical identity. Walks the parent chain.
 *
 * @a: The allocator (durable, or a layer over one)
 *
 * Returns:
 * The content region base address, or 0 if unsupported.
 */
uint64_t
fy_allocator_region_base(struct fy_allocator *a)
	FY_EXPORT;

/**
 * fy_allocator_region_size() - Get the durable content region reserved size
 *
 * Return the total reserved virtual size of a durable arena's content region.
 * Walks the parent chain.
 *
 * @a: The allocator (durable, or a layer over one)
 *
 * Returns:
 * The content region reserved size, or 0 if unsupported.
 */
uint64_t
fy_allocator_region_size(struct fy_allocator *a)
	FY_EXPORT;

/**
 * fy_allocator_index_region_base() - Get the durable index region base address
 *
 * Return the fixed virtual base address of a durable arena's separate
 * dedup-index region (FY_DURABLE_ARENA_SEPARATE_INDEX). Walks the parent chain.
 *
 * @a: The allocator (durable, or a layer over one)
 *
 * Returns:
 * The index region base address, or 0 if there is no separate index region /
 * if unsupported.
 */
uint64_t
fy_allocator_index_region_base(struct fy_allocator *a)
	FY_EXPORT;

/**
 * fy_allocator_index_region_size() - Get the durable index region reserved size
 *
 * Return the total reserved virtual size of a durable arena's separate
 * dedup-index region (FY_DURABLE_ARENA_SEPARATE_INDEX). Walks the parent chain.
 *
 * @a: The allocator (durable, or a layer over one)
 *
 * Returns:
 * The index region reserved size, or 0 if there is no separate index region /
 * if unsupported.
 */
uint64_t
fy_allocator_index_region_size(struct fy_allocator *a)
	FY_EXPORT;

/**
 * fy_allocator_checkpoint() - Take a checkpoint of an allocator
 *
 * Take a checkpoint of an allocator - All the details are internal to
 * the allocator, but it is directed at the durable iterator for now.
 *
 * Afterwards the state of the allocator can be verified, queried or
 * even rolled back to a previous state.
 *
 * @a: The allocator
 *
 * Returns:
 * 0 on success, -1 on error.
 */
int
fy_allocator_checkpoint(struct fy_allocator *a)
	FY_EXPORT;

/**
 * fy_allocator_verify() - Verify a previous checkpoint'ed allocator
 *
 * Verify that allocator is in a consistent state against the
 * latest checkpoint.
 *
 * @a: The allocator
 *
 * Returns:
 * 0 on success, -1 on error.
 */
int
fy_allocator_verify(struct fy_allocator *a)
	FY_EXPORT;

/**
 * typedef fy_alloc_checkpoint_iter_fn - Checkpoint callback
 *
 * Called once per valid slot, oldest-to-newest.
 *
 * @slot_gen: The slot generation
 * @head_chunk_gen: The head chunk generation
 * @head_chunk_bump: The bump allocation for that chunk
 * @refs_head_snap: The refs head snapshot value
 * @arg: User supplied argument to fy_allocator_checkpoint_iterate()
 *
 * Returns:
 * true to continue iterating
 * false to stop iterating
 */
typedef bool (*fy_alloc_checkpoint_iter_fn)(
	uint64_t slot_gen,
	uint64_t head_chunk_gen,
	uint64_t head_chunk_bump,
	uint64_t refs_head_snap,
	void *arg);

/**
 * fy_allocator_checkpoint_iterate() - Iterate over valid checkpoints
 *
 * @a: The allocator
 * @cb: The checkpoint callback
 * @arg: A user supplied argument to the callback
 *
 * Returns:
 * 0 if at least one checkpoint, -1 if none
 */
int
fy_allocator_checkpoint_iterate(struct fy_allocator *a,
				fy_alloc_checkpoint_iter_fn cb,
				void *arg)
	FY_EXPORT;

/**
 * fy_allocator_checkpoint_recover() - Rollback to the given generation
 *
 * Roll back the allocator to the checkpoint identified by @slot_gen.
 *
 * @a: The allocator
 * @slot_gen: The generation of the slot to restore to
 *
 * Returns:
 * 0 if recovery was successful, -1 on error
 */
int
fy_allocator_checkpoint_recover(struct fy_allocator *a,
				uint64_t slot_gen)
	FY_EXPORT;

/**
 * fy_durable_arena_gc() - Offline garbage-collect (compact) a durable arena
 *
 * Reclaim everything in the durable arena at @dir that is no longer reachable
 * from its published refs head, rewriting the arena as a compacted copy at the
 * same canonical base address.
 *
 * @dir: Path to the arena directory.
 *
 * Returns:
 * 0 on success, 1 if another collector currently holds the arena's GC lock, or
 * -1 on error
 */
int
fy_durable_arena_gc(const char *dir)
	FY_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYAML_ALLOCATOR_H */
