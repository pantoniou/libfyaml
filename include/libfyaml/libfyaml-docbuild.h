/*
 * libfyaml-docbuild.h - libfyaml document builder API
 *
 * Copyright (c) 2023-2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LIBFYAML_DOCBUILD_H
#define LIBFYAML_DOCBUILD_H

#include <libfyaml/libfyaml-core.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOC: Document builder — event-stream to document-tree conversion
 *
 * This header provides ``struct fy_document_builder``, which accumulates
 * YAML parser events and assembles them into a ``struct fy_document`` tree.
 *
 * The builder supports two operating modes:
 *
 * **Pull mode** — the builder drives a parser internally::
 *
 *   struct fy_document_builder *fydb =
 *       fy_document_builder_create_on_parser(fyp);
 *   struct fy_document *fyd;
 *   while ((fyd = fy_document_builder_load_document(fydb, fyp)) != NULL) {
 *       process(fyd);
 *       fy_document_destroy(fyd);
 *   }
 *   fy_document_builder_destroy(fydb);
 *
 * **Push mode** — the caller feeds events one at a time and takes
 * ownership when the document is complete::
 *
 *   fy_document_builder_set_in_stream(fydb);
 *   struct fy_event *fye;
 *   while ((fye = get_next_event()) != NULL) {
 *       fy_document_builder_process_event(fydb, fye);
 *       if (fy_document_builder_is_document_complete(fydb)) {
 *           struct fy_document *fyd =
 *               fy_document_builder_take_document(fydb);
 *           process(fyd);
 *           fy_document_destroy(fyd);
 *       }
 *   }
 *
 * Push mode is useful when events arrive asynchronously or from a source
 * other than a libfyaml parser, such as a document iterator or a network
 * stream.
 *
 * ``fy_parse_load_document_with_builder()`` provides the simplest interface:
 * a single call that returns the next complete document from a parser.
 */

/**
 * struct fy_document_builder_cfg - document builder configuration structure.
 *
 * Argument to the fy_document_builder_create() method
 *
 * @parse_cfg: Parser configuration
 * @userdata: Opaque user data pointer
 * @diag: Optional diagnostic interface to use
 */
struct fy_document_builder_cfg {
	struct fy_parse_cfg parse_cfg;
	void *userdata;
	struct fy_diag *diag;
};

/**
 * fy_document_builder_create() - Create a document builder
 *
 * Creates a document builder with its configuration @cfg
 * The document builder may be destroyed by a corresponding call to
 * fy_document_builder_destroy().
 *
 * @cfg: The configuration for the document builder
 *
 * Returns:
 * A pointer to the document builder or NULL in case of an error.
 */
struct fy_document_builder *
fy_document_builder_create(const struct fy_document_builder_cfg *cfg)
	FY_EXPORT;

/**
 * fy_document_builder_create_on_parser() - Create a document builder
 * 					    pulling state from the parser
 *
 * Creates a document builder pulling state from the given parser
 *
 * @fyp: The parser to associate with
 *
 * Returns:
 * A pointer to the document builder or NULL in case of an error.
 */
struct fy_document_builder *
fy_document_builder_create_on_parser(struct fy_parser *fyp)
	FY_EXPORT;

/**
 * fy_document_builder_reset() - Reset a document builder
 *
 * Resets a document builder without destroying it
 *
 * @fydb: The document builder
 */
void
fy_document_builder_reset(struct fy_document_builder *fydb)
	FY_EXPORT;

/**
 * fy_document_builder_destroy() - Destroy a document builder
 *
 * Destroy a document builder
 *
 * @fydb: The document builder
 */
void
fy_document_builder_destroy(struct fy_document_builder *fydb)
	FY_EXPORT;

/**
 * fy_document_builder_get_document() - Get the document of a builder
 *
 * Retrieve the document of a document builder. This document
 * may be incomplete. If you need to take ownership use
 * fy_document_builder_take_document().
 *
 * @fydb: The document builder
 *
 * Returns:
 * The document that the builder built, or NULL in case of an error
 */
struct fy_document *
fy_document_builder_get_document(struct fy_document_builder *fydb)
	FY_EXPORT;

/**
 * fy_document_builder_is_in_stream() - Test document builder in stream
 *
 * Find out if the document builder is in 'stream' state,
 * i.e. after stream start but before stream end events are generated.
 *
 * @fydb: The document builder
 *
 * Returns:
 * true if in stream, false otherwise
 */
bool
fy_document_builder_is_in_stream(struct fy_document_builder *fydb)
	FY_EXPORT;

/**
 * fy_document_builder_is_in_document() - Test document builder in document
 *
 * Find out if the document builder is in 'document' state,
 * i.e. after document start but before document end events are generated.
 *
 * @fydb: The document builder
 *
 * Returns:
 * true if in document, false otherwise
 */
bool
fy_document_builder_is_in_document(struct fy_document_builder *fydb)
	FY_EXPORT;

/**
 * fy_document_builder_is_document_complete() - Test document builder complete
 *
 * Find out if the document of the builder is complete.
 * If it is complete then a call to fy_document_builder_take_document() will
 * transfer ownership of the document to the caller.
 *
 * @fydb: The document builder
 *
 * Returns:
 * true if document complete, false otherwise
 */
bool
fy_document_builder_is_document_complete(struct fy_document_builder *fydb)
	FY_EXPORT;

/**
 * fy_document_builder_take_document() - Take ownership the document of a builder
 *
 * Take ownership of the document of a document builder.
 * The document builder's document must be complete.
 *
 * @fydb: The document builder
 *
 * Returns:
 * The document that the builder built, or NULL in case of an error
 */
struct fy_document *
fy_document_builder_take_document(struct fy_document_builder *fydb)
	FY_EXPORT;

/**
 * fy_document_builder_peek_document() - Peek at the document of a builder
 *
 * Peek at the document of a document builder.
 * Ownership still remains with the builder.
 *
 * @fydb: The document builder
 *
 * Returns:
 * A peek to the document that the builder built, or NULL in case of an error
 */
struct fy_document *
fy_document_builder_peek_document(struct fy_document_builder *fydb)
	FY_EXPORT;

/**
 * fy_document_builder_set_in_stream() - Set the builders state in 'stream'
 *
 * Set the document builders state to in 'stream'
 *
 * @fydb: The document builder
 */
void
fy_document_builder_set_in_stream(struct fy_document_builder *fydb)
	FY_EXPORT;

/**
 * fy_document_builder_set_in_document() - Set the builders state in 'document'
 *
 * Set the document builders state to in 'document'
 *
 * @fydb: The document builder
 * @fyds: The document state
 * @single: Single document mode
 *
 * Returns:
 * 0 on success, -1 on error
 */
int
fy_document_builder_set_in_document(struct fy_document_builder *fydb, struct fy_document_state *fyds, bool single)
	FY_EXPORT;

/**
 * fy_document_builder_load_document() - Create a document from parser events
 *
 * Load a document by pumping the parser for events and then processing them
 * with the builder.
 *
 * @fydb: The document builder
 * @fyp: The parser
 *
 * Returns:
 * The document that results from the parser, or NULL in case of an error (or EOF)
 */
struct fy_document *
fy_document_builder_load_document(struct fy_document_builder *fydb, struct fy_parser *fyp)
	FY_EXPORT;

/**
 * fy_parse_load_document_with_builder() - Parse a document via built-in builder
 *
 * Load a document by pumping the parser for events and then processing them
 * with the in-parser builder.
 *
 * @fyp: The parser
 *
 * Returns:
 * The document that results from the parser, or NULL in case of an error (or EOF)
 */
struct fy_document *
fy_parse_load_document_with_builder(struct fy_parser *fyp)
	FY_EXPORT;

/**
 * fy_document_builder_process_event() - Process an event with a builder
 *
 * Pump an event to a document builder for processing.
 *
 * @fydb: The document builder
 * @fye: The event
 *
 * Returns:
 * 0 on success, -1 on error
 */
int
fy_document_builder_process_event(struct fy_document_builder *fydb, struct fy_event *fye)
	FY_EXPORT;

#ifdef __cplusplus
}
#endif

#endif	// LIBFYAML_DOCBUILD_H
