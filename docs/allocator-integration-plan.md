# Allocator Integration Plan for Document/Node Structures

**Date:** 2025-10-30 (Original), 2025-11-01 (Updated)
**Author:** Claude Code Analysis
**Target:** libfyaml document and node memory allocation
**Status:** ✅ **IMPLEMENTED** (as of commit ff8db26)

---

## 🎉 IMPLEMENTATION STATUS

### Summary

The custom allocator integration for document and node structures **has been successfully implemented** with a different (and superior) approach than originally proposed. Instead of extending `fy_parse_cfg` with a pointer field, the implementation uses **flag-based allocator selection** which maintains ABI compatibility.

**Implementation Commits:** b2a098c through ff8db26

### Actual Implementation vs Original Proposal

| Aspect | Proposed | Actually Implemented | Advantage |
|--------|----------|---------------------|-----------|
| **API Design** | Add `struct fy_allocator *allocator` field to `fy_parse_cfg` | Use flags in `fy_parse_cfg_flags` enum | ✅ ABI compatible! |
| **Allocator Selection** | User passes allocator pointer | User sets `FYPCF_ALLOCATOR_*` flags | ✅ Simpler user API |
| **Storage** | Document stores copy of pointer | Document creates and owns allocator instance | ✅ Better lifetime management |
| **Tags** | Proposed single tag per type | Implemented tag array indexed by type | ✅ More flexible |
| **Capabilities** | Not considered | Caches allocator capabilities | ✅ Performance optimization |

### Files Modified (Actual)

| File | Lines Added | Description |
|------|-------------|-------------|
| `include/libfyaml.h` | +52 | Added allocator flags and public API |
| `src/lib/fy-doc.h` | +13 | Added `enum fy_doc_alloc_tag`, allocator fields to document |
| `src/lib/fy-doc.c` | +204 | Wrapper functions, descriptor table, allocation replacements |
| `src/allocator/*.c` | +55 | Added refcounting support to all allocators |
| `src/allocator/fy-allocator.h` | +4 | Refcounting function declarations |
| `src/tool/fy-tool.c` | +25 | Added `--allocator` command-line option |
| `test/libfyaml-test-allocator.c` | +308 | Comprehensive allocator test suite |
| **Total** | **661 lines** | **7 commits** |

### Key Implementation Details

#### 1. Flag-Based Allocator Selection (ABI Compatible!)

**Location:** `include/libfyaml.h:311-353`

```c
/* Shift amount of the allocator type */
#define FYPCF_ALLOCATOR_SHIFT      20
/* Mask of the allocator type */
#define FYPCF_ALLOCATOR_MASK       ((1U << 3) - 1)
/* Build an allocator type option */
#define FYPCF_ALLOCATOR(x)         (((unsigned int)(x) & FYPCF_ALLOCATOR_MASK) << FYPCF_ALLOCATOR_SHIFT)

enum fy_parse_cfg_flags {
    ...
    FYPCF_ALLOCATOR_DEFAULT  = FYPCF_ALLOCATOR(0),  // Use default malloc/free
    FYPCF_ALLOCATOR_MALLOC   = FYPCF_ALLOCATOR(1),  // malloc allocator
    FYPCF_ALLOCATOR_LINEAR   = FYPCF_ALLOCATOR(2),  // Linear arena
    FYPCF_ALLOCATOR_MREMAP   = FYPCF_ALLOCATOR(3),  // mremap-based
    FYPCF_ALLOCATOR_DEDUP    = FYPCF_ALLOCATOR(4),  // Deduplication
    FYPCF_ALLOCATOR_AUTO     = FYPCF_ALLOCATOR(5),  // Auto-selection
    ...
};
```

**Advantage:** No ABI break! Existing `fy_parse_cfg` structure unchanged.

#### 2. Document Structure Enhancements

**Location:** `src/lib/fy-doc.h:113-135`

```c
struct fy_document {
    ...
    struct fy_parse_cfg parse_cfg;
    struct fy_allocator *allocator;           // NEW: Allocator instance
    struct fy_node *root;
    ...
    int allocator_tags[FYDAT_COUNT];          // NEW: Per-type tags
    enum fy_allocator_cap_flags allocator_caps; // NEW: Cached capabilities
};
```

#### 3. Allocation Tags

**Location:** `src/lib/fy-doc.h:105-111`

```c
enum fy_doc_alloc_tag {
    FYDAT_NODE,       // fy_node structure
    FYDAT_NODE_PAIR,  // fy_node_pair structure
    FYDAT_ANCHOR,     // fy_anchor structure
    FYDAT_ACCEL,      // fy_accel structures (hashtables)
    FYDAT_COUNT,
};
```

**Note:** Reduced from proposed FYDAT_DOCUMENT, FYDAT_ITERATOR, etc. Focus on core structures only.

#### 4. Object Descriptor Table

**Location:** `src/lib/fy-doc.c:121-141`

```c
static const struct {
    size_t size;
    size_t align;
} doc_obj_desc[FYDAT_COUNT] = {
    [FYDAT_NODE] = {
        .size = sizeof(struct fy_node),
        .align = __alignof__(struct fy_node),
    },
    [FYDAT_NODE_PAIR] = {
        .size = sizeof(struct fy_node_pair),
        .align = __alignof__(struct fy_node_pair),
    },
    [FYDAT_ANCHOR] = {
        .size = sizeof(struct fy_anchor),
        .align = __alignof__(struct fy_anchor),
    },
    [FYDAT_ACCEL] = {
        .size = sizeof(struct fy_accel),
        .align = __alignof__(struct fy_accel),
    },
};
```

**Advantage:** Type-safe, compile-time size/alignment information.

#### 5. Allocation Wrapper Functions

**Location:** `src/lib/fy-doc.c:143-196`

```c
static inline void *fy_doc_calloc(struct fy_document *fyd, unsigned int obj_kind, size_t nmemb)
{
    size_t size, align, total;
    void *p;
    int tag;

    tag = fyd->allocator_tags[obj_kind];
    size = doc_obj_desc[obj_kind].size;

    if (!fyd->allocator || tag < 0)
        return calloc(nmemb, size);  // Fallback to malloc

    if (FY_MUL_OVERFLOW(size, nmemb, &total))
        return NULL;

    align = doc_obj_desc[tag].align;
    p = fy_allocator_alloc(fyd->allocator, tag, total, align);
    if (!p)
        return NULL;
    memset(p, 0, total);

    return p;
}

static inline void *fy_doc_alloc(struct fy_document *fyd, unsigned int obj_kind)
{
    return fy_doc_calloc(fyd, obj_kind, 1);
}

static inline void fy_doc_free(struct fy_document *fyd, int tag_hint, void *ptr)
{
    int tag;

    if (!ptr)
        return;

    if (fyd->allocator && (unsigned int)tag_hint < ARRAY_SIZE(fyd->allocator_tags) &&
        (tag = fyd->allocator_tags[tag_hint]) >= 0) {

        // Only free if allocator supports individual frees
        if (fyd->allocator_caps & FYACF_CAN_FREE_INDIVIDUAL)
            fy_allocator_free(fyd->allocator, tag, ptr);

        return;
    }
    free(ptr);  // Fallback to free
}
```

**Key Features:**
- ✅ Uses descriptor table for size/align
- ✅ Checks allocator capabilities before freeing
- ✅ Overflow-safe multiplication
- ✅ Graceful fallback to malloc/free

#### 6. Allocator Refcounting

**Location:** `src/allocator/fy-allocator.h:4` and all `*-allocator.c` files

```c
struct fy_allocator *fy_allocator_ref(struct fy_allocator *a);
void fy_allocator_unref(struct fy_allocator *a);
```

Implemented in all allocators:
- `fy-allocator-malloc.c` (+7 lines)
- `fy-allocator-linear.c` (+7 lines)
- `fy-allocator-mremap.c` (+7 lines)
- `fy-allocator-dedup.c` (+14 lines)
- `fy-allocator-auto.c` (+13 lines)

**Solves:** Issue 1 (Allocator Lifetime Management) from original analysis.

#### 7. Integration Coverage

**Fully Integrated:**
- ✅ `fy_node` (src/lib/fy-doc.c - fy_node_alloc)
- ✅ `fy_node_pair` (src/lib/fy-doc.c - fy_node_pair_alloc)
- ✅ `fy_accel` (document accelerators: axl, naxl)
- ✅ `fy_accel` (node mapping accelerators: xl)

**Not Yet Integrated (still using malloc):**
- ⚠️ `fy_anchor` (src/lib/fy-doc.c:211 - still uses malloc)
- ⚠️ `fy_document` itself (document structure allocation)
- ⚠️ `fy_document_builder`
- ⚠️ `fy_document_iterator`
- ⚠️ `fy_composer`

**Rationale:** Focus on high-frequency allocations (nodes, pairs) first.

#### 8. Public API Extensions

**Location:** `include/libfyaml.h:9345-9732`

New public functions:
- `fy_allocator_iterate()` - Iterate available allocators
- `fy_allocator_is_available()` - Check allocator availability
- `fy_allocator_create()` - Create allocator instance
- `fy_allocator_destroy()` - Destroy allocator
- `fy_allocator_get_tag()` - Get new tag from allocator
- `fy_allocator_release_tag()` - Release tag and its memory
- `fy_allocator_trim_tag()` - Trim excess memory
- `fy_allocator_reset_tag()` - Reset tag content
- `fy_allocator_alloc()` - Allocate memory
- `fy_allocator_free()` - Free memory
- `fy_allocator_store()` - Store object (with dedup support)
- `fy_allocator_storev()` - Store object (scatter-gather)
- `fy_allocator_dump()` - Dump allocator state
- `fy_allocator_get_caps()` - Get allocator capabilities (**NEW**)
- `fy_allocator_get_tag_linear_size()` - Get linear size of tag (**NEW**)
- `fy_allocator_get_tag_single_linear()` - Get single linear buffer (**NEW**)

#### 9. Capability Flags

**Location:** `include/libfyaml.h:9576-9582`

```c
enum fy_allocator_cap_flags {
    FYACF_CAN_FREE_INDIVIDUAL             = FY_BIT(0),
    FYACF_CAN_FREE_TAG                    = FY_BIT(1),
    FYACF_CAN_DEDUP                       = FY_BIT(2),
    FYCAF_CAN_QUERY_POINTER               = FY_BIT(3),
    FYCAF_CAN_QUERY_POINTER_EFFICIENTLY   = FY_BIT(4),
};
```

**Usage:** Document caches capabilities for fast checks in `fy_doc_free()`.

#### 10. Tool Integration

**Location:** `src/tool/fy-tool.c:25`

Added `--allocator` command-line option to fy-tool:

```bash
fy-tool --allocator=linear parse document.yaml
fy-tool --allocator=mremap dump document.yaml
fy-tool --allocator=dedup compose -
```

#### 11. Comprehensive Test Suite

**Location:** `test/libfyaml-test-allocator.c:308 lines`

Tests include:
- Allocator builtin availability checks
- Alignment tests for all allocators
- Linear allocator buffer tests
- Malloc allocator tests
- Mremap allocator tests
- Dedup allocator tests
- Auto allocator scenario tests
- Tag management tests
- Capability API tests

### Usage Examples

#### Example 1: Using Linear Allocator

```c
struct fy_parse_cfg cfg = {
    .flags = FYPCF_DEFAULT | FYPCF_ALLOCATOR_LINEAR
};

struct fy_parser *fyp = fy_parser_create(&cfg);
fy_parser_set_input_file(fyp, "large_document.yaml");

struct fy_document *fyd = fy_parse_load_document(fyp);
// All nodes/pairs allocated via linear allocator

fy_document_destroy(fyd);  // Bulk free via allocator
fy_parser_destroy(fyp);
```

#### Example 2: Command-Line Usage

```bash
# Parse with linear allocator (faster for large documents)
fy-tool --allocator=linear parse big.yaml

# Dump with dedup allocator (saves memory on duplicates)
fy-tool --allocator=dedup dump anchors.yaml

# Auto-select optimal allocator
fy-tool --allocator=auto compose huge.yaml
```

#### Example 3: Programmatic Allocator Creation

```c
// Create custom-configured linear allocator
struct fy_linear_allocator_cfg lcfg = {
    .buf = my_buffer,
    .size = 1024 * 1024  // 1MB
};
struct fy_allocator *alloc = fy_allocator_create("linear", &lcfg);

// Use with parser
struct fy_parse_cfg cfg = {
    .flags = FYPCF_ALLOCATOR_LINEAR  // Will use same config
};
struct fy_parser *fyp = fy_parser_create(&cfg);
```

### Differences from Original Proposal

| Feature | Proposed | Implemented | Rationale |
|---------|----------|-------------|-----------|
| **ABI Compatibility** | Acknowledged break | Maintained via flags | Better for users |
| **Allocator Ownership** | User-provided pointer | Document-owned instance | Cleaner lifetime |
| **Tag Management** | Single tag per type | Array of tags per type | More flexible |
| **Capability Caching** | Not proposed | Implemented | Performance optimization |
| **Public API** | Minimal | Comprehensive | Full featured |
| **Document Allocation** | Proposed | Not yet implemented | Phased approach |
| **Anchor Integration** | Proposed | Not yet implemented | Phased approach |

### Benefits Realized

1. ✅ **Zero ABI Break** - Existing binaries continue working
2. ✅ **Simpler User API** - Just set a flag, no allocator management
3. ✅ **Better Lifetime** - Document owns allocator, automatic cleanup
4. ✅ **Performance** - Capability caching, descriptor tables
5. ✅ **Flexibility** - Can still create custom allocators if needed
6. ✅ **Testing** - Comprehensive test suite included

### Known Limitations

1. ⚠️ **Anchor allocation** - Not yet integrated (still uses malloc directly)
2. ⚠️ **Document structure** - Not yet integrated
3. ⚠️ **Builder/Iterator/Composer** - Not yet integrated
4. ⚠️ **Partial coverage** - Only high-frequency allocations covered

**Status:** These are planned for future phases (see "Future Work" below).

### Future Work

Based on the original analysis, remaining integration points:

**Phase 3 (Recommended Next):**
- [ ] Integrate `fy_anchor` allocation (src/lib/fy-doc.c:211)
- [ ] Integrate `fy_document` itself
- [ ] Integrate `fy_document_iterator` stack
- [ ] Integrate `fy_document_builder` stack
- [ ] Integrate `fy_composer`

**Phase 4 (Nice to Have):**
- [ ] Dynamic buffer allocations (path memory, sort arrays, etc.)
- [ ] Diag subsystem integration
- [ ] Token/event allocator integration

---

## Original Analysis Document

The sections below contain the **original analysis and proposal** that led to the implementation above. They are preserved for reference and to show the thought process.

**Note:** The actual implementation differs from the proposal in beneficial ways (primarily the flag-based approach for ABI compatibility).

---

## Executive Summary (Original Proposal)

This document analyzes the feasibility and approach for integrating custom allocators into the document parser subsystem of libfyaml, specifically for `fy_document` and `fy_node` structures.

**Key Findings:**
- The parser already uses sophisticated type-safe object recycling for internal structures (tokens, events, etc.)
- Document and node structures currently use direct `malloc`/`free` calls
- Custom allocator infrastructure already exists in `src/allocator/` but is not used by document/node code
- Integration is feasible with minimal API changes by extending `fy_parse_cfg`
- Approximately 31 allocation sites need modification across 3 files

---

## Table of Contents (Original Proposal)

1. [Current State Analysis](#current-state-analysis)
2. [Allocator Infrastructure](#allocator-infrastructure)
3. [Memory Allocation Patterns](#memory-allocation-patterns)
4. [Proposed Integration Architecture](#proposed-integration-architecture)
5. [Implementation Strategy](#implementation-strategy)
6. [Threading Path](#threading-path)
7. [Backward Compatibility](#backward-compatibility)
8. [Testing Strategy](#testing-strategy)
9. [Potential Issues & Solutions](#potential-issues-and-solutions)
10. [Rollout Plan](#rollout-plan)
11. [Summary of Changes](#summary-of-changes)

---

## Current State Analysis

### Parser's Recycling Mechanism (Already Working)

The parser uses object pools defined via macros in `src/lib/fy-types.h`:

```c
// Macro generates these functions for each type:
fy_parse_<type>_alloc(fyp)       // Pop from recycled list or malloc
fy_parse_<type>_recycle(fyp, n)  // Push to recycled list
fy_parse_<type>_vacuum(fyp)      // Free all recycled objects
```

**Implementation Pattern:**

```c
#define FY_PARSE_TYPE_DEFINE(_type)
struct fy_ ## _type *fy_parse_ ## _type ## _alloc_simple(struct fy_parser *fyp)
{
    return fy_ ## _type ## _alloc_simple_internal(&fyp->recycled_ ## _type);
}

void fy_parse_ ## _type ## _recycle_simple(struct fy_parser *fyp, struct fy_ ## _type *_n)
{
    if (!fyp->suppress_recycling)
        fy_ ## _type ## _recycle_internal(&fyp->recycled_ ## _type, _n);
    else
        free(_n);
}
```

**Recycled Lists in `fy_parser`** (src/lib/fy-parse.h:242-254):
- `recycled_indent`
- `recycled_simple_key`
- `recycled_parse_state_log`
- `recycled_flow`
- `recycled_streaming_alias`
- `recycled_eventp`
- `recycled_token`

**Key Insight:** The recycling mechanism provides efficient reuse of frequently allocated/freed structures by maintaining per-parser object pools. When `suppress_recycling` flag is set, it falls back to direct `free()`.

### Document/Node Allocation (Currently Direct malloc/free)

Despite the sophisticated recycling for parser structures, document and node allocations use direct `malloc`/`free`.

---

## Allocator Infrastructure

### Existing Implementation

Location: `src/allocator/fy-allocator.h`

**Allocator Operations:**

```c
struct fy_allocator_ops {
    int (*setup)(struct fy_allocator *a, const void *cfg);
    void (*cleanup)(struct fy_allocator *a);
    struct fy_allocator *(*create)(const void *cfg);
    void (*destroy)(struct fy_allocator *a);
    void (*dump)(struct fy_allocator *a);
    void *(*alloc)(struct fy_allocator *a, int tag, size_t size, size_t align);
    void (*free)(struct fy_allocator *a, int tag, void *data);
    int (*update_stats)(struct fy_allocator *a, int tag, struct fy_allocator_stats *stats);
    const void *(*store)(struct fy_allocator *a, int tag, const void *data, size_t size, size_t align);
    const void *(*storev)(struct fy_allocator *a, int tag, const struct iovec *iov, int iovcnt, size_t align);
    void (*release)(struct fy_allocator *a, int tag, const void *data, size_t size);
    int (*get_tag)(struct fy_allocator *a);
    void (*release_tag)(struct fy_allocator *a, int tag);
    void (*trim_tag)(struct fy_allocator *a, int tag);
    void (*reset_tag)(struct fy_allocator *a, int tag);
    struct fy_allocator_info *(*get_info)(struct fy_allocator *a, int tag);
};
```

**Available Allocator Implementations:**

| Allocator | File | Description |
|-----------|------|-------------|
| malloc | fy-allocator-malloc.c | Standard malloc wrapper with tracking |
| linear | fy-allocator-linear.c | Linear arena allocator (bump pointer) |
| mremap | fy-allocator-mremap.c | mremap-based allocator |
| dedup | fy-allocator-dedup.c | Deduplication allocator |
| auto | fy-allocator-auto.c | Auto-selecting allocator |

**Current Status:** The allocator infrastructure is complete but **NOT** used for document/node allocation.

---

## Memory Allocation Patterns

### Core Structure Definitions

**File: src/lib/fy-doc.h**

#### fy_document Structure (Lines 104-122)

```c
struct fy_document {
    struct list_head node;
    struct fy_anchor_list anchors;
    struct fy_accel *axl;         // anchor->name accelerator (optional)
    struct fy_accel *naxl;        // node->anchor accelerator (optional)
    struct fy_document_state *fyds;
    struct fy_diag *diag;
    struct fy_parse_cfg parse_cfg;  // <-- Contains configuration
    struct fy_node *root;
    bool parse_error : 1;
    struct fy_document *parent;
    struct fy_document_list children;
    fy_node_meta_clear_fn meta_clear_fn;
    void *meta_user;
    struct fy_path_expr_document_data *pxdd;
};
```

**Size:** ~440 bytes
**Frequency:** 1 per document (low)

#### fy_node Structure (Lines 57-99)

```c
struct fy_node {
    struct list_head node;
    struct fy_token *tag;
    enum fy_node_style style;
    struct fy_node *parent;
    struct fy_document *fyd;      // <-- Reference to document
    unsigned int marks;
    unsigned int type : 2;
    bool has_meta : 1;
    bool attached : 1;
    bool synthetic : 1;
    bool key_root : 1;
    void *meta;
    struct fy_accel *xl;          // mapping accelerator (optional)
    struct fy_path_expr_node_data *pxnd;
    union {
        struct fy_token *scalar;
        struct fy_node_list sequence;
        struct fy_node_pair_list mapping;
    };
    // ... tokens for collection boundaries
};
```

**Size:** ~128 bytes
**Frequency:** Per YAML node (HIGH - thousands in large documents)

#### fy_node_pair Structure (Lines 42-50)

```c
struct fy_node_pair {
    struct list_head node;
    struct fy_node *key;
    struct fy_node *value;
    struct fy_document *fyd;      // <-- Reference to document
    struct fy_node *parent;
};
```

**Size:** ~48 bytes
**Frequency:** Per key-value pair in mappings (HIGH)

### Allocation Sites Summary

**File: src/lib/fy-doc.c (48 malloc/calloc/realloc calls total)**

#### Priority 1: Core Document/Node Allocations

| Structure | Create Function | Line | Free Function | Line | Frequency |
|-----------|-----------------|------|---------------|------|-----------|
| fy_document | `fy_document_create()` | 3115 | `fy_parse_document_destroy()` | 358 | Per document |
| fy_document | `fy_parse_document_create()` | 377 | (same as above) | 358 | Per document |
| fy_document.axl | (both creates) | 3134, 388 | (via cleanup) | - | Conditional |
| fy_document.naxl | (both creates) | 3143, 397 | (via cleanup) | - | Conditional |
| fy_node | `fy_node_alloc()` | 834 | `fy_node_free()` | 810 | **HIGH** |
| fy_node.xl | `fy_node_alloc()` | 855 | `fy_node_free()` | 805 | Conditional |
| fy_node_pair | `fy_node_pair_alloc()` | 712 | `fy_node_pair_free()` | 693 | **HIGH** |

#### Priority 2: Related Structures

| Structure | Create Function | Line | Size | Notes |
|-----------|-----------------|------|------|-------|
| fy_anchor | `fy_anchor_create()` | 103 | ~40 bytes | Per anchor |
| fy_ptr_node | `fy_ptr_node_create()` | 6738 | ~32 bytes | Temporary references |
| fy_document_iterator | `fy_document_iterator_create_cfg()` | 7098 | ~896 bytes | Iterator creation |
| iterator.stack | `fy_document_iterator_ensure_space()` | 7382, 7388 | dynamic | Growth via realloc |

**File: src/lib/fy-docbuilder.c (3 malloc/realloc calls)**

| Structure | Function | Line | Notes |
|-----------|----------|------|-------|
| fy_document_builder | `fy_document_builder_create()` | 75 | Main structure |
| builder.stack | (same) | 87 | Initial allocation |
| builder.stack | `fy_db_stack_increase()` | 454 | Growth via realloc |

**File: src/lib/fy-composer.c (1 malloc call)**

| Structure | Function | Line | Notes |
|-----------|----------|------|-------|
| fy_composer | `fy_composer_create()` | 37 | Main structure |

#### Priority 3: Dynamic Buffers (Low Frequency)

- Anchor string copies (fy-doc.c:187)
- Path memory (fy-doc.c:4930)
- Mapping sort arrays (fy-doc.c:5915)
- Synthetic scalar buffers (fy-doc.c:3241)

### Allocation Patterns

#### Pattern 1: Cascading Allocation Chain

```
fy_document_create()
  ├─ malloc(fy_document)              [Line 3115]
  ├─ fy_diag_create()                 [creates diag object]
  ├─ (conditional) malloc(fy_accel)   [Line 3134: axl]
  │   └─ fy_accel_setup(axl)          [hashtable init]
  ├─ (conditional) malloc(fy_accel)   [Line 3143: naxl]
  │   └─ fy_accel_setup(naxl)         [hashtable init]
  └─ fy_document_state_default()      [state initialization]
```

#### Pattern 2: Node Creation with Accelerators

```
fy_node_alloc(FYNT_MAPPING)
  ├─ malloc(fy_node)                  [Line 834]
  ├─ fy_node_pair_list_init()
  └─ (if accelerated document)
      └─ malloc(fy_accel)             [Line 855: xl]
         └─ fy_accel_setup(xl)        [mapping key hashtable]
```

#### Pattern 3: Document Hierarchy

- Documents can have parent-child relationships
- Child documents stored in `parent->children` list
- Destruction is recursive: parent destruction frees children
- **Implication:** Allocator lifetime must span document tree

#### Pattern 4: Iterator Stack Management

```
fy_document_iterator_create()
  ├─ malloc(fy_document_iterator)
  ├─ Uses in-place array for depths ≤ 32
  └─ On depth overflow:
      ├─ malloc(stack)              [Line 7382: first time]
      └─ realloc(stack)             [Line 7388: subsequent growth]
```

---

## Proposed Integration Architecture

### Phase 1: Allocator Threading (Minimal API Impact)

#### 1.1 Extend `fy_parse_cfg` Structure

**File:** include/libfyaml.h:423

**Current structure:**
```c
struct fy_parse_cfg {
    const char *search_path;
    enum fy_parse_cfg_flags flags;
    void *userdata;
    struct fy_diag *diag;
};
```

**Proposed addition:**
```c
struct fy_parse_cfg {
    const char *search_path;
    enum fy_parse_cfg_flags flags;
    void *userdata;
    struct fy_diag *diag;
    struct fy_allocator *allocator;  // NEW: Custom allocator (NULL = malloc)
};
```

**Rationale:**
- ✅ `fy_document_create()` takes `fy_parse_cfg *cfg` (fy-doc.c:3115)
- ✅ `fy_parse_document_create()` copies `fyp->cfg` to `fyd->parse_cfg` (fy-doc.c:384)
- ✅ `fy_node_alloc()` has access to `fyd->parse_cfg.allocator` via `fyd` parameter
- ✅ All document/node operations can access allocator through document reference
- ✅ NULL allocator provides backward compatibility (falls back to malloc)

#### 1.2 Create Allocation Wrapper Functions

**File:** src/lib/fy-doc.c (new internal functions)

```c
/**
 * Document allocation wrappers - use allocator from fy_document
 */

// Wrapper for malloc using document's allocator
static inline void *fy_doc_malloc(struct fy_document *fyd, int tag, size_t size)
{
    struct fy_allocator *a = fyd ? fyd->parse_cfg.allocator : NULL;
    if (a && a->ops && a->ops->alloc)
        return a->ops->alloc(a, tag, size, 0);
    return malloc(size);
}

// Wrapper for calloc (allocate + zero-initialize)
static inline void *fy_doc_calloc(struct fy_document *fyd, int tag, size_t size)
{
    void *ptr = fy_doc_malloc(fyd, tag, size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}

// Wrapper for free using document's allocator
static inline void fy_doc_free(struct fy_document *fyd, int tag, void *ptr)
{
    struct fy_allocator *a = fyd ? fyd->parse_cfg.allocator : NULL;
    if (a && a->ops && a->ops->free)
        a->ops->free(a, tag, ptr);
    else
        free(ptr);
}

/**
 * Configuration allocation wrappers - use allocator from fy_parse_cfg
 * Used when document doesn't exist yet (during document creation)
 */

static inline void *fy_cfg_malloc(const struct fy_parse_cfg *cfg, int tag, size_t size)
{
    struct fy_allocator *a = cfg ? cfg->allocator : NULL;
    if (a && a->ops && a->ops->alloc)
        return a->ops->alloc(a, tag, size, 0);
    return malloc(size);
}

static inline void *fy_cfg_calloc(const struct fy_parse_cfg *cfg, int tag, size_t size)
{
    void *ptr = fy_cfg_malloc(cfg, tag, size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}

static inline void fy_cfg_free(const struct fy_parse_cfg *cfg, int tag, void *ptr)
{
    struct fy_allocator *a = cfg ? cfg->allocator : NULL;
    if (a && a->ops && a->ops->free)
        a->ops->free(a, tag, ptr);
    else
        free(ptr);
}
```

**Design Notes:**
- Inline functions minimize overhead
- NULL checks allow graceful fallback to malloc
- Tag parameter enables per-type tracking and statistics
- Separate `fy_cfg_*` functions for bootstrap phase (document creation)

### Phase 2: Allocation Tags (for tracking/stats)

Define tags for different allocation types:

```c
/**
 * Allocation tags for document subsystem
 * Used for statistics tracking and allocator strategy selection
 */
enum fy_doc_alloc_tag {
    FYDAT_DOCUMENT = 1,      // fy_document structure
    FYDAT_NODE,              // fy_node structure
    FYDAT_NODE_PAIR,         // fy_node_pair structure
    FYDAT_ANCHOR,            // fy_anchor structure
    FYDAT_ACCEL,             // fy_accel structures (hashtables)
    FYDAT_ITERATOR,          // fy_document_iterator
    FYDAT_BUILDER,           // fy_document_builder
    FYDAT_COMPOSER,          // fy_composer
    FYDAT_BUFFER,            // Dynamic buffers/arrays
};
```

**Benefits:**
- Allocator implementations can track per-type statistics
- Different strategies per type (e.g., arena for nodes, dedup for scalars)
- Detailed memory usage reporting
- Debugging and profiling capabilities

### Phase 3: Replace Allocation Sites

#### 3.1 Core Document/Node Allocations (Priority 1)

**File: src/lib/fy-doc.c**

| Function | Line | Current | Replace With |
|----------|------|---------|-------------|
| `fy_document_create()` | 3115 | `malloc(sizeof(*fyd))` | `fy_cfg_malloc(cfg, FYDAT_DOCUMENT, sizeof(*fyd))` |
| `fy_parse_document_create()` | 377 | `malloc(sizeof(*fyd))` | `fy_cfg_malloc(&fyp->cfg, FYDAT_DOCUMENT, sizeof(*fyd))` |
| `fy_parse_document_destroy()` | 358 | `free(fyd)` | `fy_cfg_free(&fyd->parse_cfg, FYDAT_DOCUMENT, fyd)` |
| `fy_node_alloc()` | 834 | `malloc(sizeof(*fyn))` | `fy_doc_malloc(fyd, FYDAT_NODE, sizeof(*fyn))` |
| `fy_node_free()` | 810 | `free(fyn)` | `fy_doc_free(fyn->fyd, FYDAT_NODE, fyn)` |
| `fy_node_pair_alloc()` | 712 | `malloc(sizeof(*fynp))` | `fy_doc_malloc(fyd, FYDAT_NODE_PAIR, sizeof(*fynp))` |
| `fy_node_pair_free()` | 693 | `free(fynp)` | `fy_doc_free(fynp->fyd, FYDAT_NODE_PAIR, fynp)` |
| `fy_node_pair_detach_and_free()` | 705 | `free(fynp)` | `fy_doc_free(fynp->fyd, FYDAT_NODE_PAIR, fynp)` |

**Accelerator allocations:**

| Function | Line | Current | Replace With |
|----------|------|---------|-------------|
| `fy_document_create()` (axl) | 3134 | `malloc(sizeof(*fyd->axl))` | `fy_doc_malloc(fyd, FYDAT_ACCEL, sizeof(*fyd->axl))` |
| `fy_document_create()` (naxl) | 3143 | `malloc(sizeof(*fyd->naxl))` | `fy_doc_malloc(fyd, FYDAT_ACCEL, sizeof(*fyd->naxl))` |
| `fy_parse_document_create()` (axl) | 388 | `malloc(sizeof(*fyd->axl))` | `fy_doc_malloc(fyd, FYDAT_ACCEL, sizeof(*fyd->axl))` |
| `fy_parse_document_create()` (naxl) | 397 | `malloc(sizeof(*fyd->naxl))` | `fy_doc_malloc(fyd, FYDAT_ACCEL, sizeof(*fyd->naxl))` |
| `fy_node_alloc()` (mapping xl) | 855 | `malloc(sizeof(*fyn->xl))` | `fy_doc_malloc(fyd, FYDAT_ACCEL, sizeof(*fyn->xl))` |

**Estimated Impact:** ~15 allocation/free call replacements

#### 3.2 Related Structures (Priority 2)

**File: src/lib/fy-doc.c**

| Structure | Create Function | Line | Notes |
|-----------|----------------|------|-------|
| fy_anchor | `fy_anchor_create()` | 103 | 1 malloc + destroy at line 95 |
| fy_ptr_node | `fy_ptr_node_create()` | 6738 | 1 malloc + destroy at line 6748 |
| fy_document_iterator | `fy_document_iterator_create_cfg()` | 7098 | 1 malloc + destroy at line 7155 |
| iterator.stack | `fy_document_iterator_ensure_space()` | 7382, 7388 | malloc/realloc + free at 7076 |

**File: src/lib/fy-docbuilder.c**

| Structure | Function | Line | Notes |
|-----------|----------|------|-------|
| fy_document_builder | `fy_document_builder_create()` | 75 | Main structure |
| builder.stack | (same) | 87 | Initial stack allocation |
| Stack growth | `fy_db_stack_increase()` | 454 | realloc for dynamic growth |

**File: src/lib/fy-composer.c**

| Structure | Function | Line | Notes |
|-----------|----------|------|-------|
| fy_composer | `fy_composer_create()` | 37 | Single malloc |

**Estimated Impact:** ~10 additional allocation sites

#### 3.3 Dynamic Buffers (Priority 3)

Less frequent but should be included for completeness:

- Anchor string copies (fy-doc.c:187)
- Path memory (fy-doc.c:4930)
- Mapping sort arrays (fy-doc.c:5915)
- Synthetic scalar buffers (fy-doc.c:3241)

**Estimated Impact:** ~6 additional allocation sites

---

## Implementation Strategy

### Option A: Minimalist (Recommended for Initial Implementation)

**Scope:** Only core document/node structures
**Files:** `fy-doc.c` only
**Changes:** ~20 allocation sites

**Pros:**
- ✅ Smallest change surface area
- ✅ Focuses on high-frequency allocations (nodes/pairs)
- ✅ Easy to test and validate
- ✅ Provides immediate performance benefits for large documents

**Cons:**
- ⚠️ Doesn't cover builder, iterator, composer
- ⚠️ Mixed allocation strategies in codebase (malloc + custom)

**Recommendation:** Start here to validate approach and measure benefits.

### Option B: Comprehensive (Recommended for Production)

**Scope:** All document-related allocations
**Files:** `fy-doc.c`, `fy-docbuilder.c`, `fy-composer.c`
**Changes:** ~31 allocation sites

**Pros:**
- ✅ Complete control over document subsystem memory
- ✅ Consistent allocation strategy throughout
- ✅ Better for memory tracking/profiling
- ✅ Cleaner architecture (no mixed approaches)

**Cons:**
- ⚠️ Larger change surface area
- ⚠️ More testing required
- ⚠️ Affects more subsystems

**Recommendation:** Upgrade to this after Option A proves successful.

---

## Threading Path

### Path 1: Parser-Driven Document Creation

```
User creates parser with custom allocator
    ↓
fy_parser_create(&cfg) where cfg.allocator != NULL
    ↓
Parser stores cfg (including allocator) in fyp->cfg
    ↓
Parser encounters document during parse
    ↓
fy_parse_document_create(parser, event)
  → Allocates fyd using fyp->cfg.allocator
  → Copies fyp->cfg to fyd->parse_cfg
  → fyd->parse_cfg.allocator now available to all operations
    ↓
Document creates nodes during parsing
    ↓
fy_node_alloc(fyd, type)
  → Uses fyd->parse_cfg.allocator via fy_doc_malloc()
  → For MAPPING nodes, allocates xl accelerator with same allocator
    ↓
Node creates pairs for mappings
    ↓
fy_node_pair_alloc(fyd)
  → Uses fyd->parse_cfg.allocator via fy_doc_malloc()
```

### Path 2: Direct Document Creation

```
User creates document directly
    ↓
fy_document_create(&cfg) where cfg.allocator != NULL
  → Uses cfg.allocator for fyd allocation via fy_cfg_malloc()
  → Copies cfg (including allocator) to fyd->parse_cfg
  → fyd->parse_cfg.allocator now available
    ↓
User creates nodes programmatically
    ↓
fy_node_create(fyd, ...)
  → Internally calls fy_node_alloc(fyd, type)
  → Uses fyd->parse_cfg.allocator
    ↓
[Same as Path 1 from here]
```

### Path 3: Document Builder

```
User creates document builder
    ↓
fy_document_builder_create(&cfg) where cfg.allocator != NULL
  → Creates fyd using cfg.allocator
  → fyd->parse_cfg.allocator set
    ↓
Builder constructs document from events
    ↓
[Uses same allocation path as above]
```

**Key Insight:** All paths converge on `fyd->parse_cfg.allocator` being available for document/node operations.

---

## Backward Compatibility

### API Compatibility: ✅ MAINTAINED

Adding a field to `fy_parse_cfg` is **backward compatible** because:

1. **Zero-initialization works:** Users who initialize via `{0}` or `memset` get `allocator = NULL`
2. **NULL is safe:** All wrapper functions check for NULL allocator and fall back to `malloc`
3. **Existing code unaffected:** Programs that don't set allocator continue using malloc
4. **Opt-in feature:** Only users who explicitly set `cfg.allocator` get custom allocation

**Example - Existing Code (still works):**
```c
struct fy_parse_cfg cfg = {
    .flags = FYPCF_DEFAULT,
    .diag = NULL
    // .allocator implicitly NULL - uses malloc
};
struct fy_document *fyd = fy_document_create(&cfg);
```

**Example - New Code (opts into custom allocator):**
```c
struct fy_allocator *alloc = fy_allocator_create("linear", NULL);
struct fy_parse_cfg cfg = {
    .flags = FYPCF_DEFAULT,
    .diag = NULL,
    .allocator = alloc  // Custom allocator
};
struct fy_document *fyd = fy_document_create(&cfg);
```

### ABI Compatibility: ⚠️ BROKEN (requires new major version)

Adding a field changes structure size, breaking ABI for:
- Shared library users who compile against old headers but run with new library
- Structure size checks in version negotiation
- Binary plugins that allocate fy_parse_cfg on stack

**Solutions:**

#### Solution 1: Accept ABI Break (Recommended)
- Bump library soname (e.g., libfyaml.so.0 → libfyaml.so.1)
- Document in changelog as breaking change
- Schedule for next major release

#### Solution 2: Opaque Handle (More Invasive)
```c
// Public header - opaque handle
typedef struct fy_parse_cfg_opaque *fy_parse_cfg_t;

// Getters/setters
void fy_parse_cfg_set_allocator(fy_parse_cfg_t cfg, struct fy_allocator *a);
```

**Pros:** ABI stable
**Cons:** Requires rewriting all cfg initialization code

#### Solution 3: Extended Structure (Compatibility Shim)
```c
// Original structure (unchanged)
struct fy_parse_cfg {
    const char *search_path;
    enum fy_parse_cfg_flags flags;
    void *userdata;
    struct fy_diag *diag;
};

// Extended structure
struct fy_parse_cfg_ex {
    struct fy_parse_cfg base;
    struct fy_allocator *allocator;
};
```

**Pros:** Preserves ABI for old structure
**Cons:** Requires separate API functions, complex migration path

**Recommendation:** Solution 1 (accept ABI break) for cleaner design and next major version.

---

## Testing Strategy

### Unit Tests

#### Test 1: Allocator Substitution
**Goal:** Verify all allocations go through custom allocator

```c
// Custom allocator that tracks all calls
struct tracking_allocator {
    int alloc_count;
    int free_count;
    size_t total_allocated;
    void **allocations;
};

void test_allocator_substitution() {
    struct tracking_allocator tracker = {0};
    struct fy_allocator *alloc = create_tracking_allocator(&tracker);

    struct fy_parse_cfg cfg = {
        .allocator = alloc
    };

    struct fy_parser *fyp = fy_parser_create(&cfg);
    fy_parser_set_string(fyp, "foo: bar\nbaz: [1, 2, 3]", FY_NT);

    struct fy_document *fyd = fy_parse_load_document(fyp);

    // Verify allocations went through custom allocator
    assert(tracker.alloc_count > 0);
    assert(tracker.total_allocated > 0);

    int allocs_before_destroy = tracker.alloc_count;
    fy_document_destroy(fyd);

    // Verify all allocations freed
    assert(tracker.free_count == allocs_before_destroy);
    assert(tracker.total_allocated == 0);  // Assuming tracking_allocator decrements
}
```

#### Test 2: Tag Validation
**Goal:** Verify correct tags used for each allocation type

```c
void test_allocation_tags() {
    struct tag_tracking_allocator tracker = {0};
    struct fy_allocator *alloc = create_tag_tracking_allocator(&tracker);

    struct fy_parse_cfg cfg = { .allocator = alloc };
    struct fy_document *fyd = fy_document_create(&cfg);

    // Verify FYDAT_DOCUMENT tag used
    assert(tracker.tags[FYDAT_DOCUMENT] == 1);

    struct fy_node *scalar = fy_node_build_from_string(fyd, "test", FY_NT);
    assert(tracker.tags[FYDAT_NODE] == 1);

    struct fy_node *mapping = fy_node_create_mapping(fyd);
    assert(tracker.tags[FYDAT_NODE] == 2);
    // If accelerated, FYDAT_ACCEL should be 1

    fy_document_destroy(fyd);
}
```

#### Test 3: NULL Allocator Fallback
**Goal:** Verify backward compatibility with NULL allocator

```c
void test_null_allocator_fallback() {
    struct fy_parse_cfg cfg = {
        .allocator = NULL  // Explicit NULL
    };

    struct fy_document *fyd = fy_document_create(&cfg);
    assert(fyd != NULL);

    struct fy_node *node = fy_node_build_from_string(fyd, "test", FY_NT);
    assert(node != NULL);

    fy_document_destroy(fyd);
    // Should use malloc/free internally
}
```

### Integration Tests

#### Test 4: Linear Allocator
**Goal:** Verify performance benefits of linear allocator

```c
void test_linear_allocator() {
    struct fy_allocator *linear = fy_allocator_create("linear", NULL);
    struct fy_parse_cfg cfg = { .allocator = linear };

    struct fy_parser *fyp = fy_parser_create(&cfg);
    fy_parser_set_input_file(fyp, "large_document.yaml");

    clock_t start = clock();
    struct fy_document *fyd = fy_parse_load_document(fyp);
    clock_t end = clock();

    double time_linear = (double)(end - start) / CLOCKS_PER_SEC;

    // Compare with malloc
    struct fy_parse_cfg cfg_malloc = { .allocator = NULL };
    struct fy_parser *fyp2 = fy_parser_create(&cfg_malloc);
    fy_parser_set_input_file(fyp2, "large_document.yaml");

    start = clock();
    struct fy_document *fyd2 = fy_parse_load_document(fyp2);
    end = clock();

    double time_malloc = (double)(end - start) / CLOCKS_PER_SEC;

    printf("Linear: %.3fs, Malloc: %.3fs, Speedup: %.2fx\n",
           time_linear, time_malloc, time_malloc / time_linear);

    // Expect 1.2-1.5x speedup for large documents
    assert(time_linear < time_malloc);
}
```

#### Test 5: Dedup Allocator
**Goal:** Verify deduplication working

```c
void test_dedup_allocator() {
    struct fy_allocator *dedup = fy_allocator_create("dedup", NULL);
    struct fy_parse_cfg cfg = { .allocator = dedup };

    const char *yaml =
        "anchors:\n"
        "  - &ref \"duplicated string\"\n"
        "  - *ref\n"
        "  - *ref\n";

    struct fy_parser *fyp = fy_parser_create(&cfg);
    fy_parser_set_string(fyp, yaml, FY_NT);
    struct fy_document *fyd = fy_parse_load_document(fyp);

    // Check dedup stats
    struct fy_allocator_stats stats = {0};
    fy_allocator_update_stats(dedup, -1, &stats);

    assert(stats.dup_stores > 0);  // Should have detected duplicates
    assert(stats.dup_saved > 0);   // Should have saved memory
}
```

#### Test 6: Mixed Mode (Parent/Child Documents)
**Goal:** Verify child documents inherit allocator

```c
void test_mixed_mode_documents() {
    struct fy_allocator *parent_alloc = fy_allocator_create("linear", NULL);
    struct fy_parse_cfg cfg = { .allocator = parent_alloc };

    struct fy_document *parent = fy_document_create(&cfg);

    // Create child document - should inherit parent's allocator
    struct fy_document *child = fy_document_create_child(parent);

    // Verify child uses same allocator
    assert(child->parse_cfg.allocator == parent_alloc);

    fy_document_destroy(parent);  // Should also clean up child
}
```

### Performance Tests

#### Test 7: Baseline Comparison
**Goal:** Measure allocator overhead

```c
void benchmark_allocator_overhead() {
    // Test with malloc allocator wrapper
    struct fy_allocator *malloc_wrapper = fy_allocator_create("malloc", NULL);
    struct fy_parse_cfg cfg1 = { .allocator = malloc_wrapper };

    clock_t start = clock();
    for (int i = 0; i < 1000; i++) {
        struct fy_document *fyd = fy_document_create(&cfg1);
        fy_node_build_from_string(fyd, "test", FY_NT);
        fy_document_destroy(fyd);
    }
    clock_t end = clock();
    double time_wrapped = (double)(end - start) / CLOCKS_PER_SEC;

    // Test with NULL (direct malloc)
    struct fy_parse_cfg cfg2 = { .allocator = NULL };

    start = clock();
    for (int i = 0; i < 1000; i++) {
        struct fy_document *fyd = fy_document_create(&cfg2);
        fy_node_build_from_string(fyd, "test", FY_NT);
        fy_document_destroy(fyd);
    }
    end = clock();
    double time_direct = (double)(end - start) / CLOCKS_PER_SEC;

    double overhead = (time_wrapped - time_direct) / time_direct * 100;
    printf("Allocator overhead: %.2f%%\n", overhead);

    // Expect < 5% overhead
    assert(overhead < 5.0);
}
```

#### Test 8: Large Document Performance
**Goal:** Measure benefits on realistic workloads

```c
void benchmark_large_documents() {
    const char *test_files[] = {
        "test/data/small.yaml",    // ~100 nodes
        "test/data/medium.yaml",   // ~1000 nodes
        "test/data/large.yaml"     // ~10000 nodes
    };

    for (int i = 0; i < 3; i++) {
        printf("\nTesting %s:\n", test_files[i]);

        // Test each allocator type
        const char *allocators[] = {NULL, "malloc", "linear", "mremap"};

        for (int j = 0; j < 4; j++) {
            struct fy_allocator *alloc = allocators[j] ?
                fy_allocator_create(allocators[j], NULL) : NULL;
            struct fy_parse_cfg cfg = { .allocator = alloc };

            struct fy_parser *fyp = fy_parser_create(&cfg);
            fy_parser_set_input_file(fyp, test_files[i]);

            clock_t start = clock();
            struct fy_document *fyd = fy_parse_load_document(fyp);
            clock_t end = clock();

            double time = (double)(end - start) / CLOCKS_PER_SEC;
            printf("  %s: %.3fs\n", allocators[j] ? allocators[j] : "direct", time);

            fy_document_destroy(fyd);
            fy_parser_destroy(fyp);
            if (alloc) fy_allocator_destroy(alloc);
        }
    }
}
```

### Memory Tests

#### Test 9: Valgrind Leak Detection
```bash
# Test with malloc allocator
valgrind --leak-check=full --show-leak-kinds=all \
  ./test-allocator-integration malloc

# Test with linear allocator
valgrind --leak-check=full --show-leak-kinds=all \
  ./test-allocator-integration linear

# Test with NULL allocator (direct malloc)
valgrind --leak-check=full --show-leak-kinds=all \
  ./test-allocator-integration null
```

**Expected:** 0 leaks in all cases

#### Test 10: Address Sanitizer
```bash
# Compile with ASan
gcc -fsanitize=address -g test-allocator-integration.c -o test-asan

# Run tests
./test-asan malloc
./test-asan linear
./test-asan null
```

**Expected:** No memory errors, use-after-free, or double-free

---

## Potential Issues and Solutions

### Issue 1: Allocator Lifetime Management

**Problem:** Documents can outlive the allocator that created them.

**Scenario:**
```c
struct fy_allocator *alloc = fy_allocator_create("linear", NULL);
struct fy_parse_cfg cfg = { .allocator = alloc };
struct fy_document *fyd = fy_document_create(&cfg);

fy_allocator_destroy(alloc);  // Oops! Document still exists

// Later...
fy_document_destroy(fyd);     // Tries to free via destroyed allocator!
```

**Solutions:**

#### Solution 1A: Reference Counting (Recommended)
```c
// Add refcounting to fy_allocator
struct fy_allocator {
    const char *name;
    const struct fy_allocator_ops *ops;
    int refs;  // NEW
};

// In fy_document_create():
if (cfg->allocator)
    fy_allocator_ref(cfg->allocator);

// In fy_document_destroy():
if (fyd->parse_cfg.allocator)
    fy_allocator_unref(fyd->parse_cfg.allocator);
```

**Pros:** Safe, automatic cleanup
**Cons:** Requires allocator API changes

#### Solution 1B: Documentation (Minimal)
Document that allocator must outlive all documents using it.

```c
/**
 * fy_parse_cfg.allocator lifetime:
 * The allocator must remain valid for the entire lifetime of any
 * documents created with this configuration. It is the caller's
 * responsibility to ensure proper ordering of destruction:
 *   1. Destroy all documents
 *   2. Destroy the allocator
 */
```

**Pros:** No code changes
**Cons:** Error-prone, relies on user discipline

**Recommendation:** Implement Solution 1A (refcounting) for safety.

### Issue 2: Nested Documents with Different Allocators

**Problem:** Parent and child documents may have different allocators.

**Scenario:**
```c
struct fy_allocator *alloc1 = fy_allocator_create("linear", NULL);
struct fy_document *parent = fy_document_create(&(struct fy_parse_cfg){.allocator = alloc1});

struct fy_allocator *alloc2 = fy_allocator_create("malloc", NULL);
struct fy_document *child = fy_document_create(&(struct fy_parse_cfg){.allocator = alloc2});

// Link as parent-child
fy_document_add_child(parent, child);

// What happens on parent destruction?
fy_document_destroy(parent);  // Should it destroy child? Which allocator to use?
```

**Solutions:**

#### Solution 2A: Inherit Parent Allocator (Recommended)
```c
// In fy_document_add_child():
if (child->parse_cfg.allocator != parent->parse_cfg.allocator) {
    // Warn or error
    return -1;
}
```

**Pros:** Simple, consistent
**Cons:** Restricts flexibility

#### Solution 2B: Independent Allocators
Allow different allocators, but each document manages its own allocation.

```c
// In fy_document_destroy_with_children():
for (each child) {
    fy_document_destroy(child);  // Uses child's own allocator
}
fy_document_destroy(parent);  // Uses parent's allocator
```

**Pros:** Flexible
**Cons:** Complex lifetime management

**Recommendation:** Solution 2A (enforce same allocator) for simplicity.

### Issue 3: Node Movement Between Documents

**Problem:** Nodes can be detached from one document and attached to another with different allocators.

**Scenario:**
```c
struct fy_document *doc1 = fy_document_create(&(struct fy_parse_cfg){.allocator = alloc1});
struct fy_node *node = fy_node_build_from_string(doc1, "test", FY_NT);

struct fy_document *doc2 = fy_document_create(&(struct fy_parse_cfg){.allocator = alloc2});

// Move node from doc1 to doc2
fy_node_detach(node);
fy_document_set_root(doc2, node);

// Node allocated with alloc1, but now owned by doc2 with alloc2
// Which allocator should free it?
```

**Solutions:**

#### Solution 3A: Track Original Allocator in Node
```c
struct fy_node {
    // ... existing fields ...
    struct fy_allocator *alloc;  // NEW: allocator used to create this node
};

// In fy_node_alloc():
fyn->alloc = fyd->parse_cfg.allocator;

// In fy_node_free():
use fyn->alloc instead of fyn->fyd->parse_cfg.allocator
```

**Pros:** Correct freeing always
**Cons:** Increases node size by 8 bytes

#### Solution 3B: Prevent Cross-Document Movement
```c
// In fy_document_set_root():
if (node->fyd && node->fyd->parse_cfg.allocator != fyd->parse_cfg.allocator) {
    return -EINVAL;  // Reject cross-allocator movement
}
```

**Pros:** Simple, safe
**Cons:** Restricts functionality

#### Solution 3C: Reallocate on Movement (Copy-on-Attach)
```c
// In fy_document_set_root():
if (node->fyd && node->fyd->parse_cfg.allocator != fyd->parse_cfg.allocator) {
    // Deep copy node using new document's allocator
    node = fy_node_deep_copy(fyd, node);
}
```

**Pros:** Allows movement, safe
**Cons:** Performance cost, complex

**Recommendation:** Solution 3B (prevent movement) initially, document limitation. Consider 3A if cross-document movement is important use case.

### Issue 4: Diag Subsystem Allocation

**Problem:** `fy_diag_create()` currently uses malloc internally.

**Current code in fy_document_create():**
```c
diag = cfg->diag;
if (!diag) {
    diag = fy_diag_create(NULL);  // Uses malloc internally
    if (!diag)
        goto err_out;
}
```

**Solutions:**

#### Solution 4A: Pass Allocator to Diag
```c
// Modify fy_diag_create() to accept allocator
diag = fy_diag_create_with_allocator(NULL, cfg->allocator);
```

**Pros:** Consistent allocation
**Cons:** Requires parallel work on diag subsystem

#### Solution 4B: Accept Mixed Mode (Recommended for Phase 1)
Document that diag continues using malloc while document/node uses custom allocator.

**Pros:** Minimal scope, focus on document/node
**Cons:** Inconsistent (but acceptable for most use cases)

**Recommendation:** Solution 4B for initial implementation, Solution 4A for future enhancement.

---

## Rollout Plan

### Phase 1: Foundation (Week 1)

**Goals:**
- Establish allocator threading infrastructure
- Create wrapper functions
- Define allocation tags
- Unit tests

**Tasks:**
1. Add `allocator` field to `fy_parse_cfg` in include/libfyaml.h
2. Define `enum fy_doc_alloc_tag` in src/lib/fy-doc.c
3. Implement wrapper functions:
   - `fy_doc_malloc()`
   - `fy_doc_calloc()`
   - `fy_doc_free()`
   - `fy_cfg_malloc()`
   - `fy_cfg_calloc()`
   - `fy_cfg_free()`
4. Write unit tests for wrapper functions
5. Test NULL allocator fallback

**Deliverables:**
- Modified include/libfyaml.h
- Modified src/lib/fy-doc.c with wrapper functions
- New test/test-allocator-wrappers.c

**Success Criteria:**
- All unit tests pass
- NULL allocator works correctly
- No functionality changes (just infrastructure)

### Phase 2: Core Integration (Week 2)

**Goals:**
- Replace core document/node/pair allocations
- Validate with multiple allocator types
- Performance benchmarks

**Tasks:**
1. Replace allocations in `fy_document_create()` (line 3115)
2. Replace allocations in `fy_parse_document_create()` (line 377)
3. Replace allocations in `fy_node_alloc()` (line 834)
4. Replace allocations in `fy_node_pair_alloc()` (line 712)
5. Replace corresponding free calls
6. Replace accelerator allocations (lines 3134, 3143, 388, 397, 855)
7. Integration tests with malloc, linear, mremap allocators
8. Performance benchmarks

**Deliverables:**
- Modified src/lib/fy-doc.c with core replacements
- New test/test-allocator-integration.c
- Performance benchmark results

**Success Criteria:**
- All existing tests pass
- All allocator integration tests pass
- No memory leaks (valgrind clean)
- Performance regression < 5% with malloc allocator
- Performance improvement with linear allocator

### Phase 3: Extended Structures (Week 3)

**Goals:**
- Integrate remaining structures (anchor, iterator, builder, composer)
- Comprehensive coverage

**Tasks:**
1. Integrate anchor allocations (fy-doc.c:103)
2. Integrate ptr_node allocations (fy-doc.c:6738)
3. Integrate iterator allocations (fy-doc.c:7098, 7382, 7388)
4. Integrate builder allocations (fy-docbuilder.c:75, 87, 454)
5. Integrate composer allocations (fy-composer.c:37)
6. Integrate dynamic buffer allocations (4 sites)
7. Extended integration tests

**Deliverables:**
- Modified src/lib/fy-doc.c (extended sites)
- Modified src/lib/fy-docbuilder.c
- Modified src/lib/fy-composer.c
- Extended test coverage

**Success Criteria:**
- All allocation sites covered
- All tests pass
- Complete allocator control over document subsystem

### Phase 4: Validation & Documentation (Week 4)

**Goals:**
- Stress testing
- Performance validation
- Documentation updates

**Tasks:**
1. Stress tests with large documents (10K+ nodes)
2. Memory leak tests with all allocator types
3. Performance benchmarks vs baseline
4. Update API documentation
5. Update migration guide
6. Add examples to documentation

**Deliverables:**
- Stress test suite
- Performance report
- Updated API documentation
- Migration guide
- Example code

**Success Criteria:**
- No crashes or leaks in stress tests
- Performance goals met (linear allocator 20-40% faster)
- Documentation complete and clear

---

## Summary of Changes

### Files Modified

| File | Description | Lines Changed | Difficulty |
|------|-------------|---------------|------------|
| `include/libfyaml.h` | Add allocator field to fy_parse_cfg | +1 | Trivial |
| `src/lib/fy-doc.c` | Wrappers + core allocations | ~60 | Moderate |
| `src/lib/fy-docbuilder.c` | Builder allocations | ~10 | Easy |
| `src/lib/fy-composer.c` | Composer allocations | ~5 | Easy |

**Total:** ~76 lines changed

### Allocation Sites Modified

| Priority | Structure | Sites | Complexity |
|----------|-----------|-------|------------|
| 1 | Core (doc/node/pair) | 15 | Moderate |
| 2 | Related (anchor/iterator/builder) | 10 | Easy |
| 3 | Dynamic buffers | 6 | Easy |

**Total:** 31 allocation sites

### Estimated Effort

| Phase | Duration | Effort (dev-days) |
|-------|----------|-------------------|
| Foundation | Week 1 | 2-3 days |
| Core Integration | Week 2 | 3-4 days |
| Extended Structures | Week 3 | 2-3 days |
| Validation | Week 4 | 2-3 days |

**Total:** 9-13 developer-days over 4 weeks

### Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Lifetime management issues | Medium | High | Implement refcounting (Solution 1A) |
| Performance regression | Low | Medium | Benchmark early, optimize wrappers |
| ABI compatibility break | High | Medium | Plan for major version bump |
| Test coverage gaps | Low | Medium | Comprehensive test suite |

---

## Recommendations

### For Initial Implementation

1. ✅ **Start with Option A (Minimalist):** Focus on core document/node/pair allocations first
2. ✅ **Use Phase 1-2 approach:** Validate infrastructure before expanding
3. ✅ **Implement Solution 1A:** Add refcounting to allocators for safety
4. ✅ **Accept Solution 4B:** Keep diag using malloc initially (reduce scope)
5. ✅ **Plan for ABI break:** Schedule for next major version (2.0?)

### For Production Deployment

1. ✅ **Upgrade to Option B (Comprehensive):** Complete coverage after validation
2. ✅ **Comprehensive testing:** All allocator types, stress tests, valgrind
3. ✅ **Performance validation:** Ensure < 5% overhead with malloc, gains with linear
4. ✅ **Documentation:** Clear migration guide, API docs, examples
5. ✅ **Version planning:** Coordinate with release schedule

### Future Enhancements

1. 🔮 **Diag allocator integration** (Solution 4A)
2. 🔮 **Token/event allocator integration** (extend to parser structures)
3. 🔮 **Per-tag allocation strategies** (arena for nodes, dedup for scalars)
4. 🔮 **Memory profiling tools** (track allocation patterns)
5. 🔮 **Allocator pooling** (per-thread allocators for parallel parsing)

---

## Conclusion

The integration of custom allocators into the libfyaml document and node subsystem is **feasible and well-defined**. The proposed approach:

✅ **Minimal API impact:** Single field addition to existing structure
✅ **Backward compatible:** NULL allocator falls back to malloc
✅ **Clean threading:** Natural flow through parse_cfg → document → nodes
✅ **Limited scope:** ~31 allocation sites across 3 files
✅ **Proven pattern:** Follows parser's existing recycling mechanism design
✅ **Performance benefits:** 20-40% speedup possible with linear allocator
✅ **Incremental rollout:** Phases 1-4 allow validation at each step

The existing allocator infrastructure is robust and ready to use. The main work is mechanically replacing malloc/free calls with wrapper functions and managing allocator lifetime properly.

**Estimated total effort:** 2-3 weeks for full implementation and testing.

**Recommended next steps:**
1. Review and approve this plan
2. Create feature branch
3. Implement Phase 1 (Foundation)
4. Validate approach with Phase 2 (Core Integration)
5. Complete rollout with Phases 3-4

---

**Document Version:** 2.0
**Last Updated:** 2025-11-01
**Status:** ✅ **IMPLEMENTED** - Core integration complete, original analysis preserved for reference
