/*
 * libfyaml-composer.h - libfyaml composer API
 *
 * Copyright (c) 2023-2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LIBFYAML_COMPOSER_H
#define LIBFYAML_COMPOSER_H

#include <libfyaml/libfyaml-core.h>
#include <libfyaml/libfyaml-path-exec.h>
#include <libfyaml/libfyaml-docbuild.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOC: Composer — callback-driven, path-aware event processing
 *
 * This header provides two complementary interfaces for processing YAML
 * event streams with awareness of the current position in the document
 * hierarchy, without committing to building a full in-memory tree.
 *
 * **Simple callback interface** — ``fy_parse_compose()`` calls a user
 * function for each event, passing both the event and the current
 * ``struct fy_path`` so the callback can make intelligent decisions
 * without maintaining its own path stack::
 *
 *   enum fy_composer_return my_cb(struct fy_parser *fyp,
 *                                 struct fy_event *fye,
 *                                 struct fy_path *path,
 *                                 void *userdata)
 *   {
 *       if (fy_path_depth(path) > 3)
 *           return FYCR_OK_START_SKIP;
 *       process(fye);
 *       return FYCR_OK_CONTINUE;
 *   }
 *   fy_parse_compose(fyp, my_cb, ctx);
 *
 * **Object-based interface** — ``struct fy_composer`` wraps the same
 * mechanism in a reusable object with ops callbacks, optional document
 * builder integration, and helpers for navigating the path hierarchy.
 *
 * **Path inspection helpers** — functions such as ``fy_path_in_mapping()``,
 * ``fy_path_depth()``, ``fy_path_component_is_sequence()``, and the
 * user-data attach/retrieve pairs let callbacks annotate and query the
 * live path stack without any external bookkeeping.
 *
 * Callbacks control event processing by returning one of:
 *
 * - ``FYCR_OK_CONTINUE``    — consume the event and continue
 * - ``FYCR_OK_STOP``        — consume the event and stop
 * - ``FYCR_OK_START_SKIP``  — start skipping the current subtree
 * - ``FYCR_OK_STOP_SKIP``   — stop an active skip and resume processing
 * - ``FYCR_ERROR``          — abort with an error
 */

struct fy_composer;

/**
 * enum fy_composer_return - The returns of the composer callback
 *
 * @FYCR_OK_CONTINUE: continue processing, event processed
 * @FYCR_OK_STOP: stop processing, event processed
 * @FYCR_OK_START_SKIP: start skip object(s), event processed
 * @FYCR_OK_STOP_SKIP: stop skipping of objects, event processed
 * @FYCR_ERROR: error, stop processing
 */
enum fy_composer_return {
	FYCR_OK_CONTINUE = 0,
	FYCR_OK_STOP = 1,
	FYCR_OK_START_SKIP = 2,
	FYCR_OK_STOP_SKIP = 3,
	FYCR_ERROR = -1,
};

/**
 * fy_composer_return_is_ok() - Check if the return code is OK
 *
 * Convenience method for checking if it's OK to continue
 *
 * @ret: the composer return to check
 *
 * Returns:
 * true if non error or skip condition
 */
static inline bool
fy_composer_return_is_ok(enum fy_composer_return ret)
{
	return ret == FYCR_OK_CONTINUE || ret == FYCR_OK_STOP;
}

/**
 * typedef fy_parse_composer_cb - composer callback method
 *
 * This method is called by the fy_parse_compose() method
 * when an event must be processed.
 *
 * @fyp: The parser
 * @fye: The event
 * @path: The path that the parser is processing
 * @userdata: The user data of the fy_parse_compose() method
 *
 * Returns:
 * fy_composer_return code telling the parser what to do
 */
typedef enum fy_composer_return
(*fy_parse_composer_cb)(struct fy_parser *fyp, struct fy_event *fye,
			struct fy_path *path, void *userdata);

/**
 * fy_parse_compose() - Parse using a compose callback
 *
 * Alternative parsing method using a composer callback.
 *
 * The parser will construct a path argument that is used
 * by the callback to make intelligent decisions about
 * creating a document and/or DOM.
 *
 * @fyp: The parser
 * @cb: The callback that will be called
 * @userdata: user pointer to pass to the callback
 *
 * Returns:
 * 0 if no error occured
 * -1 on error
 */
int
fy_parse_compose(struct fy_parser *fyp, fy_parse_composer_cb cb,
		 void *userdata)
	FY_EXPORT;

/**
 * fy_path_component_is_mapping() - Check if the component is a mapping
 *
 * @fypc: The path component to check
 *
 * Returns:
 * true if the path component is a mapping, false otherwise
 */
bool
fy_path_component_is_mapping(struct fy_path_component *fypc)
	FY_EXPORT;

/**
 * fy_path_component_is_sequence() - Check if the component is a sequence
 *
 * @fypc: The path component to check
 *
 * Returns:
 * true if the path component is a sequence, false otherwise
 */
bool
fy_path_component_is_sequence(struct fy_path_component *fypc)
	FY_EXPORT;

/**
 * fy_path_component_sequence_get_index() - Get the index of sequence path component
 *
 * @fypc: The sequence path component to return it's index value
 *
 * Returns:
 * >= 0 the sequence index
 * -1 if the component is either not in the proper mode, or not a sequence
 */
int
fy_path_component_sequence_get_index(struct fy_path_component *fypc)
	FY_EXPORT;

/**
 * fy_path_component_mapping_get_scalar_key() - Get the scalar key of a mapping
 *
 * @fypc: The mapping path component to return it's scalar key
 *
 * Returns:
 * a non NULL scalar or alias token if the mapping contains a scalar key
 * NULL in case of an error, or if the component has a complex key
 */
struct fy_token *
fy_path_component_mapping_get_scalar_key(struct fy_path_component *fypc)
	FY_EXPORT;

/**
 * fy_path_component_mapping_get_scalar_key_tag() - Get the scalar key's tag of a mapping
 *
 * @fypc: The mapping path component to return it's scalar key's tag
 *
 * Returns:
 * a non NULL tag token if the mapping contains a scalar key with a tag or
 * NULL in case of an error, or if the component has a complex key
 */
struct fy_token *
fy_path_component_mapping_get_scalar_key_tag(struct fy_path_component *fypc)
	FY_EXPORT;

/**
 * fy_path_component_mapping_get_complex_key() - Get the complex key of a mapping
 *
 * @fypc: The mapping path component to return it's complex key
 *
 * Returns:
 * a non NULL document if the mapping contains a complex key
 * NULL in case of an error, or if the component has a simple key
 */
struct fy_document *
fy_path_component_mapping_get_complex_key(struct fy_path_component *fypc)
	FY_EXPORT;

/**
 * fy_path_component_get_text() - Get the textual representation of a path component
 *
 * Given a path component, return a malloc'ed string which contains
 * the textual representation of it.
 *
 * Note that this method will only return fully completed components and not
 * ones that are in the building process.
 *
 * @fypc: The path component to get it's textual representation
 *
 * Returns:
 * The textual representation of the path component, NULL on error, or
 * if the component has not been completely built during the composition
 * of a complex key.
 * The string must be free'ed using free.
 */
char *
fy_path_component_get_text(struct fy_path_component *fypc)
	FY_EXPORT;

#define fy_path_component_get_text_alloca(_fypc) \
	FY_ALLOCA_COPY_FREE(fy_path_component_get_text((_fypc)), FY_NT)
/**
 * fy_path_depth() - Get the depth of a path
 *
 * @fypp: The path to query
 *
 * Returns:
 * The depth of the path, or -1 on error
 */
int
fy_path_depth(struct fy_path *fypp)
	FY_EXPORT;

/**
 * fy_path_parent() - Get the parent of a path
 *
 * Paths may contain parents when traversing complex keys.
 * This method returns the immediate parent.
 *
 * @fypp: The path to return it's parent
 *
 * Returns:
 * The path parent or NULL on error, if it doesn't exist
 */
struct fy_path *
fy_path_parent(struct fy_path *fypp)
	FY_EXPORT;

/**
 * fy_path_get_text() - Get the textual representation of a path
 *
 * Given a path, return a malloc'ed string which contains
 * the textual representation of it.
 *
 * Note that during processing, complex key paths are simply
 * indicative and not to be used for addressing.
 *
 * @fypp: The path to get it's textual representation
 *
 * Returns:
 * The textual representation of the path, NULL on error.
 * The string must be free'ed using free.
 */
char *
fy_path_get_text(struct fy_path *fypp)
	FY_EXPORT;

#define fy_path_get_text_alloca(_fypp) \
	FY_ALLOCA_COPY_FREE(fy_path_get_text((_fypp)), FY_NT)

/**
 * fy_path_in_root() - Check if the path is in the root of the document
 *
 * @fypp: The path
 *
 * Returns:
 * true if the path is located within the root of the document
 */
bool
fy_path_in_root(struct fy_path *fypp)
	FY_EXPORT;

/**
 * fy_path_in_mapping() - Check if the path is in a mapping
 *
 * @fypp: The path
 *
 * Returns:
 * true if the path is located within a mapping
 */
bool
fy_path_in_mapping(struct fy_path *fypp)
	FY_EXPORT;

/**
 * fy_path_in_sequence() - Check if the path is in a sequence
 *
 * @fypp: The path
 *
 * Returns:
 * true if the path is located within a sequence
 */
bool
fy_path_in_sequence(struct fy_path *fypp)
	FY_EXPORT;

/**
 * fy_path_in_mapping_key() - Check if the path is in a mapping key state
 *
 * @fypp: The path
 *
 * Returns:
 * true if the path is located within a mapping key state
 */
bool
fy_path_in_mapping_key(struct fy_path *fypp)
	FY_EXPORT;

/**
 * fy_path_in_mapping_value() - Check if the path is in a mapping value state
 *
 * @fypp: The path
 *
 * Returns:
 * true if the path is located within a mapping value state
 */
bool
fy_path_in_mapping_value(struct fy_path *fypp)
	FY_EXPORT;

/**
 * fy_path_in_collection_root() - Check if the path is in a collection root
 *
 * A collection root state is when the path points to a sequence or mapping
 * but the state does not allow setting keys, values or adding items.
 *
 * This occurs on MAPPING/SEQUENCE START/END events.
 *
 * @fypp: The path
 *
 * Returns:
 * true if the path is located within a collectin root state
 */
bool
fy_path_in_collection_root(struct fy_path *fypp)
	FY_EXPORT;

/**
 * fy_path_get_root_user_data() - Return the userdata associated with the path root
 *
 * @fypp: The path
 *
 * Returns:
 * The user data associated with the root of the path, or NULL if no path
 */
void *
fy_path_get_root_user_data(struct fy_path *fypp)
	FY_EXPORT;

/**
 * fy_path_set_root_user_data() - Set the user data associated with the root
 *
 * Note, no error condition if not a path
 *
 * @fypp: The path
 * @data: The data to set as root data
 */
void
fy_path_set_root_user_data(struct fy_path *fypp, void *data)
	FY_EXPORT;

/**
 * fy_path_component_get_mapping_user_data() - Return the userdata associated with the mapping
 *
 * @fypc: The path component
 *
 * Returns:
 * The user data associated with the mapping, or NULL if not a mapping or the user data are NULL
 */
void *
fy_path_component_get_mapping_user_data(struct fy_path_component *fypc)
	FY_EXPORT;

/**
 * fy_path_component_get_mapping_key_user_data() - Return the userdata associated with the mapping key
 *
 * @fypc: The path component
 *
 * Returns:
 * The user data associated with the mapping key, or NULL if not a mapping or the user data are NULL
 */
void *
fy_path_component_get_mapping_key_user_data(struct fy_path_component *fypc)
	FY_EXPORT;

/**
 * fy_path_component_get_sequence_user_data() - Return the userdata associated with the sequence
 *
 * @fypc: The path component
 *
 * Returns:
 * The user data associated with the sequence, or NULL if not a sequence or the user data are NULL
 */
void *
fy_path_component_get_sequence_user_data(struct fy_path_component *fypc)
	FY_EXPORT;

/**
 * fy_path_component_set_mapping_user_data() - Set the user data associated with a mapping
 *
 * Note, no error condition if not a mapping
 *
 * @fypc: The path component
 * @data: The data to set as mapping data
 */
void
fy_path_component_set_mapping_user_data(struct fy_path_component *fypc, void *data)
	FY_EXPORT;

/**
 * fy_path_component_set_mapping_key_user_data() - Set the user data associated with a mapping key
 *
 * Note, no error condition if not in a mapping key state
 *
 * @fypc: The path component
 * @data: The data to set as mapping key data
 */
void
fy_path_component_set_mapping_key_user_data(struct fy_path_component *fypc, void *data)
	FY_EXPORT;

/**
 * fy_path_component_set_sequence_user_data() - Set the user data associated with a sequence
 *
 * Note, no error condition if not a sequence
 *
 * @fypc: The path component
 * @data: The data to set as sequence data
 */
void
fy_path_component_set_sequence_user_data(struct fy_path_component *fypc, void *data)
	FY_EXPORT;

/**
 * fy_path_get_parent_user_data() - Return the userdata of the parent collection
 *
 * @path: The path
 *
 * Returns:
 * The user data associated with the parent collection of the path, or NULL if no path
 */
void *
fy_path_get_parent_user_data(struct fy_path *path)
	FY_EXPORT;

/**
 * fy_path_set_parent_user_data() - Set the user data associated with the parent collection
 *
 * Note, no error condition if not a path
 *
 * @path: The path
 * @data: The data to set as parent collection data
 */
void
fy_path_set_parent_user_data(struct fy_path *path, void *data)
	FY_EXPORT;

/**
 * fy_path_get_last_user_data() - Return the userdata of the last collection
 *
 * @path: The path
 *
 * Returns:
 * The user data associated with the last collection of the path, or NULL if no path
 */
void *
fy_path_get_last_user_data(struct fy_path *path)
	FY_EXPORT;

/**
 * fy_path_set_last_user_data() - Set the user data associated with the last collection
 *
 * Note, no error condition if not a path
 *
 * @path: The path
 * @data: The data to set as last collection data
 */
void
fy_path_set_last_user_data(struct fy_path *path, void *data)
	FY_EXPORT;

/**
 * fy_path_last_component() - Get the very last component of a path
 *
 * Returns the last component of a path.
 *
 * @fypp: The path
 *
 * Returns:
 * The last path component (which may be a collection root component), or NULL
 * if it does not exist
 */
struct fy_path_component *
fy_path_last_component(struct fy_path *fypp)
	FY_EXPORT;

/**
 * fy_path_last_not_collection_root_component() - Get the last non collection root component of a path
 *
 * Returns the last non collection root component of a path. This may not be the
 * last component that is returned by fy_path_last_component().
 *
 * The difference is present on MAPPING/SEQUENCE START/END events where the
 * last component is present but not usuable as a object parent.
 *
 * @fypp: The path
 *
 * Returns:
 * The last non collection root component, or NULL if it does not exist
 */
struct fy_path_component *
fy_path_last_not_collection_root_component(struct fy_path *fypp)
	FY_EXPORT;

/**
 * struct fy_composer_ops - Composer operation callbacks
 *
 * Callbacks used by the composer to process events and create document builders.
 *
 * @process_event: Callback for processing a single YAML event with path context
 * @create_document_builder: Callback for creating a document builder instance
 */
struct fy_composer_ops {
	/* single process event callback */
	enum fy_composer_return (*process_event)(struct fy_composer *fyc, struct fy_path *path, struct fy_event *fye);
	struct fy_document_builder *(*create_document_builder)(struct fy_composer *fyc);
};

/**
 * struct fy_composer_cfg - Composer configuration structure
 *
 * Configuration structure for creating a composer instance.
 *
 * @ops: Pointer to composer operation callbacks
 * @userdata: Opaque user data pointer passed to callbacks
 * @diag: Optional diagnostic interface to use, NULL for default
 */
struct fy_composer_cfg {
	const struct fy_composer_ops *ops;
	void *userdata;
	struct fy_diag *diag;
};

/**
 * fy_composer_create() - Create a composer
 *
 * Creates a composer with the given configuration. The composer processes
 * YAML events using callback methods and maintains path information for
 * intelligent document composition. The composer may be destroyed by a
 * corresponding call to fy_composer_destroy().
 *
 * @cfg: The configuration for the composer
 *
 * Returns:
 * A pointer to the composer or NULL in case of an error.
 */
struct fy_composer *
fy_composer_create(struct fy_composer_cfg *cfg)
	FY_EXPORT;

/**
 * fy_composer_destroy() - Destroy the given composer
 *
 * Destroy a composer created earlier via fy_composer_create().
 *
 * @fyc: The composer to destroy
 */
void fy_composer_destroy(struct fy_composer *fyc)
	FY_EXPORT;

/**
 * fy_composer_process_event() - Process a YAML event through the composer
 *
 * Process a YAML event by calling the configured process_event callback
 * with path context. The composer maintains the current path and provides
 * it to the callback for intelligent processing decisions.
 *
 * @fyc: The composer
 * @fye: The event to process
 *
 * Returns:
 * A fy_composer_return code indicating how to proceed (continue, stop, skip, or error)
 */
enum fy_composer_return
fy_composer_process_event(struct fy_composer *fyc, struct fy_event *fye)
	FY_EXPORT;

/**
 * fy_composer_get_cfg() - Get the configuration of a composer
 *
 * @fyc: The composer
 *
 * Returns:
 * The configuration of the composer
 */
struct fy_composer_cfg *
fy_composer_get_cfg(struct fy_composer *fyc)
	FY_EXPORT;

/**
 * fy_composer_get_cfg_userdata() - Get the userdata from composer configuration
 *
 * Retrieve the opaque userdata pointer from the composer's configuration.
 *
 * @fyc: The composer
 *
 * Returns:
 * The userdata pointer from the configuration
 */
void *
fy_composer_get_cfg_userdata(struct fy_composer *fyc)
	FY_EXPORT;

/**
 * fy_composer_get_diag() - Get the diagnostic object of a composer
 *
 * Return a pointer to the diagnostic object of a composer object.
 * Note that the returned diag object has a reference taken so
 * you should fy_diag_unref() it when you're done with it.
 *
 * @fyc: The composer to get the diagnostic object
 *
 * Returns:
 * A pointer to a ref'ed diagnostic object or NULL in case of an error.
 */
struct fy_diag *
fy_composer_get_diag(struct fy_composer *fyc)
	FY_EXPORT;

/**
 * fy_composer_get_path() - Get the current path of the composer
 *
 * Retrieve the current path being processed by the composer.
 * The path represents the location in the YAML document structure
 * where the composer is currently positioned.
 *
 * @fyc: The composer
 *
 * Returns:
 * The current path, or NULL if no path is active
 */
struct fy_path *
fy_composer_get_path(struct fy_composer *fyc)
	FY_EXPORT;

/**
 * fy_composer_get_root_path() - Get the root path of the composer
 *
 * Retrieve the root path of the composer's path hierarchy.
 *
 * @fyc: The composer
 *
 * Returns:
 * The root path, or NULL if no root exists
 */
struct fy_path *
fy_composer_get_root_path(struct fy_composer *fyc)
	FY_EXPORT;

/**
 * fy_composer_get_next_path() - Get the next path in the composer's path list
 *
 * Iterate through the composer's path list. Pass NULL to get the first path,
 * or pass the previous path to get the next one.
 *
 * @fyc: The composer
 * @fypp: The previous path, or NULL to get the first path
 *
 * Returns:
 * The next path in the list, or NULL if no more paths exist
 */
struct fy_path *
fy_composer_get_next_path(struct fy_composer *fyc, struct fy_path *fypp)
	FY_EXPORT;

#ifdef __cplusplus
}
#endif

#endif	// LIBFYAML_COMPOSER_H
