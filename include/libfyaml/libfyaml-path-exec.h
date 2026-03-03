/*
 * libfyaml-path-exec.h - libfyaml path exec API
 * Copyright (c) 2023-2025 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef LIBFYAML_PATH_EXEC_H
#define LIBFYAML_PATH_EXEC_H

#include <libfyaml/libfyaml-core.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOC: YAML path expression parser and executor
 *
 * This header provides the ypath subsystem — a JSONPath / XPath-like query
 * language for navigating YAML document trees.
 *
 * A ypath expression such as ``/servers/0/host`` is first *parsed* into a
 * compiled ``struct fy_path_expr`` and then *executed* against a starting
 * node in any ``struct fy_document`` to produce a result set of matching
 * nodes.  Filter predicates and wildcards are also supported.
 *
 * **Typical workflow**::
 *
 *   // 1. Create a path parser
 *   struct fy_path_parser *fypp = fy_path_parser_create(NULL);
 *
 *   // 2. Compile an expression
 *   struct fy_path_expr *expr =
 *       fy_path_parse_expr_from_string(fypp, "/servers/0/host", -1);
 *
 *   // 3. Create an executor and run the expression
 *   struct fy_path_exec *fypx = fy_path_exec_create(NULL);
 *   fy_path_exec_execute(fypx, expr, fy_document_root(fyd));
 *
 *   // 4. Iterate results
 *   void *iter = NULL;
 *   struct fy_node *fyn;
 *   while ((fyn = fy_path_exec_results_iterate(fypx, &iter)) != NULL)
 *       process(fyn);
 *
 *   // 5. Cleanup
 *   fy_path_exec_destroy(fypx);
 *   fy_path_expr_free(expr);
 *   fy_path_parser_destroy(fypp);
 *
 * For one-shot use, ``fy_path_expr_build_from_string()`` wraps steps 1 and 2
 * into a single call.
 *
 * The executor can be reset and re-used for multiple executions against the
 * same or different documents.  The compiled expression is independent of any
 * document and may be executed repeatedly.
 */

struct fy_path_parser;
struct fy_path_expr;
struct fy_path_exec;
struct fy_path_component;
struct fy_path;

/**
 * enum fy_path_parse_cfg_flags - Path parse configuration flags
 *
 * These flags control the operation of the path parse
 *
 * @FYPPCF_QUIET: Quiet, do not output any information messages
 * @FYPPCF_DISABLE_RECYCLING: Disable recycling optimization
 * @FYPPCF_DISABLE_ACCELERATORS: Disable use of access accelerators (saves memory)
 */
enum fy_path_parse_cfg_flags {
	FYPPCF_QUIET			= FY_BIT(0),
	FYPPCF_DISABLE_RECYCLING	= FY_BIT(1),
	FYPPCF_DISABLE_ACCELERATORS	= FY_BIT(2),
};

/**
 * struct fy_path_parse_cfg - path parser configuration structure.
 *
 * Argument to the fy_path_parser_create() method which
 * performs parsing of a ypath expression
 *
 * @flags: Configuration flags
 * @userdata: Opaque user data pointer
 * @diag: Optional diagnostic interface to use
 */
struct fy_path_parse_cfg {
	enum fy_path_parse_cfg_flags flags;
	void *userdata;
	struct fy_diag *diag;
};

/**
 * fy_path_parser_create() - Create a ypath parser.
 *
 * Creates a path parser with its configuration @cfg
 * The path parser may be destroyed by a corresponding call to
 * fy_path_parser_destroy().
 * If @cfg is NULL a default yaml parser is created.
 *
 * @cfg: The configuration for the path parser
 *
 * Returns:
 * A pointer to the path parser or NULL in case of an error.
 */
struct fy_path_parser *
fy_path_parser_create(const struct fy_path_parse_cfg *cfg)
	FY_EXPORT;

/**
 * fy_path_parser_destroy() - Destroy the given path parser
 *
 * Destroy a path parser created earlier via fy_path_parser_create().
 *
 * @fypp: The path parser to destroy
 */
void
fy_path_parser_destroy(struct fy_path_parser *fypp)
	FY_EXPORT;

/**
 * fy_path_parser_reset() - Reset a path parser completely
 *
 * Completely reset a path parser, including after an error
 * that caused a parser error to be emitted.
 *
 * @fypp: The path parser to reset
 *
 * Returns:
 * 0 if the reset was successful, -1 otherwise
 */
int
fy_path_parser_reset(struct fy_path_parser *fypp)
	FY_EXPORT;

/**
 * fy_path_parse_expr_from_string() - Parse an expression from a given string
 *
 * Create a path expression from a string using the provided path parser.
 *
 * @fypp: The path parser to use
 * @str: The ypath source to use.
 * @len: The length of the string (or -1 if '\0' terminated)
 *
 * Returns:
 * The created path expression or NULL on error.
 */
struct fy_path_expr *
fy_path_parse_expr_from_string(struct fy_path_parser *fypp,
			       const char *str, size_t len)
	FY_EXPORT;

/**
 * fy_path_expr_build_from_string() - Parse an expression from a given string
 *
 * Create a path expression from a string using the provided path parser
 * configuration.
 *
 * @pcfg: The path parser configuration to use, or NULL for default
 * @str: The ypath source to use.
 * @len: The length of the string (or -1 if '\0' terminated)
 *
 * Returns:
 * The created path expression or NULL on error.
 */
struct fy_path_expr *
fy_path_expr_build_from_string(const struct fy_path_parse_cfg *pcfg,
			       const char *str, size_t len)
	FY_EXPORT;

/**
 * fy_path_expr_free() - Free a path expression
 *
 * Free a previously returned expression from any of the path parser
 * methods like fy_path_expr_build_from_string()
 *
 * @expr: The expression to free (may be NULL)
 */
void
fy_path_expr_free(struct fy_path_expr *expr)
	FY_EXPORT;

/**
 * fy_path_expr_dump() - Dump the contents of a path expression to
 * 			 the diagnostic object
 *
 * Dumps the expression using the provided error level.
 *
 * @expr: The expression to dump
 * @diag: The diagnostic object to use
 * @errlevel: The error level which the diagnostic will use
 * @level: The nest level; should be set to 0
 * @banner: The banner to display on level 0
 */
void
fy_path_expr_dump(struct fy_path_expr *expr, struct fy_diag *diag,
		  enum fy_error_type errlevel, int level, const char *banner)
	FY_EXPORT;

/**
 * fy_path_expr_to_document() - Converts the path expression to a YAML document
 *
 * Converts the expression to a YAML document which is useful for
 * understanding what the expression evaluates to.
 *
 * @expr: The expression to convert to a document
 *
 * Returns:
 * The document of the expression or NULL on error.
 */
struct fy_document *
fy_path_expr_to_document(struct fy_path_expr *expr)
	FY_EXPORT;

/**
 * enum fy_path_exec_cfg_flags - Path executor configuration flags
 *
 * These flags control the operation of the path expression executor
 *
 * @FYPXCF_QUIET: Quiet, do not output any information messages
 * @FYPXCF_DISABLE_RECYCLING: Disable recycling optimization
 * @FYPXCF_DISABLE_ACCELERATORS: Disable use of access accelerators (saves memory)
 */
enum fy_path_exec_cfg_flags {
	FYPXCF_QUIET			= FY_BIT(0),
	FYPXCF_DISABLE_RECYCLING	= FY_BIT(1),
	FYPXCF_DISABLE_ACCELERATORS	= FY_BIT(2),
};

/**
 * struct fy_path_exec_cfg - path expression executor configuration structure.
 *
 * Argument to the fy_path_exec_create() method which
 * performs execution of a ypath expression
 *
 * @flags: Configuration flags
 * @userdata: Opaque user data pointer
 * @diag: Optional diagnostic interface to use
 */
struct fy_path_exec_cfg {
	enum fy_path_exec_cfg_flags flags;
	void *userdata;
	struct fy_diag *diag;
};

/**
 * fy_path_exec_create() - Create a ypath expression executor.
 *
 * Creates a ypath expression parser with its configuration @cfg
 * The executor may be destroyed by a corresponding call to
 * fy_path_exec_destroy().
 *
 * @xcfg: The configuration for the executor
 *
 * Returns:
 * A pointer to the executor or NULL in case of an error.
 */
struct fy_path_exec *
fy_path_exec_create(const struct fy_path_exec_cfg *xcfg)
	FY_EXPORT;

/**
 * fy_path_exec_destroy() - Destroy the given path expression executor
 *
 * Destroy ane executor  created earlier via fy_path_exec_create().
 *
 * @fypx: The path parser to destroy
 */
void
fy_path_exec_destroy(struct fy_path_exec *fypx)
	FY_EXPORT;

/**
 * fy_path_exec_reset() - Reset an executor
 *
 * Completely reset an executor without releasing it.
 *
 * @fypx: The executor to reset
 *
 * Returns:
 * 0 if the reset was successful, -1 otherwise
 */
int
fy_path_exec_reset(struct fy_path_exec *fypx)
	FY_EXPORT;

/**
 * fy_path_exec_execute() - Execute a path expression starting at
 *                          the given start node
 *
 * Execute the expression starting at fyn_start. If execution
 * is successful the results are available via fy_path_exec_results_iterate()
 *
 * Note that it is illegal to modify the state of the document that the
 * results reside between this call and the results collection.
 *
 * @fypx: The executor to use
 * @expr: The expression to use
 * @fyn_start: The node on which the expression will begin.
 *
 * Returns:
 * 0 if the execution was successful, -1 otherwise
 *
 * Note that the execution may be successful but no results were
 * produced, in which case the iterator will return NULL.
 */
int
fy_path_exec_execute(struct fy_path_exec *fypx, struct fy_path_expr *expr,
		     struct fy_node *fyn_start)
	FY_EXPORT;

/**
 * fy_path_exec_results_iterate() - Iterate over the results of execution
 *
 * This method iterates over all the results in the executor.
 * The start of the iteration is signalled by a NULL in \*prevp.
 *
 * @fypx: The executor
 * @prevp: The previous result iterator
 *
 * Returns:
 * The next node in the result set or NULL at the end of the results.
 */
struct fy_node *
fy_path_exec_results_iterate(struct fy_path_exec *fypx, void **prevp)
	FY_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYAML_PATH_EXEC_H */
