/*
 * libfyaml-dociter.h - libfyaml document iterator API
 *
 * Copyright (c) 2023-2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LIBFYAML_DOCITER_H
#define LIBFYAML_DOCITER_H

#include <libfyaml/libfyaml-core.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOC: Document iterator — stack-free tree traversal and event replay
 *
 * This header provides ``struct fy_document_iterator``, which traverses a
 * document tree depth-first without using system stack recursion.  Two
 * usage modes are supported:
 *
 * **Node iteration** — visit every node in a subtree in document order::
 *
 *   struct fy_document_iterator *fydi = fy_document_iterator_create();
 *   fy_document_iterator_node_start(fydi, fy_document_root(fyd));
 *   struct fy_node *fyn;
 *   while ((fyn = fy_document_iterator_node_next(fydi)) != NULL)
 *       process(fyn);
 *   fy_document_iterator_destroy(fydi);
 *
 * **Event replay** — regenerate the YAML event stream that produced the
 * document, suitable for feeding into a parser, emitter, or composer::
 *
 *   struct fy_document_iterator *fydi =
 *       fy_document_iterator_create_on_document(fyd);
 *   struct fy_event *fye;
 *   while ((fye = fy_document_iterator_generate_next(fydi)) != NULL) {
 *       process_event(fye);
 *       fy_document_iterator_event_free(fydi, fye);
 *   }
 *   fy_document_iterator_destroy(fydi);
 *
 * The iterator can also be attached to a ``struct fy_parser`` via
 * ``fy_parser_set_document_iterator()``, making a parser transparently
 * replay the events of an existing document rather than parsing fresh input.
 *
 * Events emitted by the iterator are in the same order as those that
 * originally created the document, so round-trip fidelity is preserved.
 */

/* Shift amount of the want mode */
#define FYDICF_WANT_SHIFT		0
/* Mask of the WANT mode */
#define FYDICF_WANT_MASK			((1U << 2) - 1)
/* Build a WANT mode option */
#define FYDICF_WANT(x)			(((unsigned int)(x) & FYDICF_WANT_MASK) << FYDICF_WANT_SHIFT)

/**
 * enum fy_document_iterator_cfg_flags - Document iterator configuration flags
 *
 * These flags control the operation of the document iterator
 *
 * @FYDICF_WANT_BODY_EVENTS: Generate body events
 * @FYDICF_WANT_DOCUMENT_BODY_EVENTS: Generate document and body events
 * @FYDICF_WANT_STREAM_DOCUMENT_BODY_EVENTS: Generate stream, document and body events
 */
enum fy_document_iterator_cfg_flags {
	FYDICF_WANT_BODY_EVENTS			= FYDICF_WANT(0),
	FYDICF_WANT_DOCUMENT_BODY_EVENTS	= FYDICF_WANT(1),
	FYDICF_WANT_STREAM_DOCUMENT_BODY_EVENTS	= FYDICF_WANT(2),
};

/**
 * struct fy_document_iterator_cfg - document iterator configuration structure.
 *
 * Argument to the fy_document_iterator_create_cfg() method.
 *
 * @flags: The document iterator flags
 * @fyd: The document to iterate on (or NULL if iterate_root is set)
 * @iterate_root: The root of iteration (NULL when fyd is not NULL)
 */
struct fy_document_iterator_cfg {
	enum fy_document_iterator_cfg_flags flags;
	struct fy_document *fyd;
	struct fy_node *iterate_root;
};

/**
 * fy_document_iterator_create() - Create a document iterator
 *
 * Creates a document iterator, that can trawl through a document
 * without using recursion.
 *
 * Returns:
 * The newly created document iterator or NULL on error
 */
struct fy_document_iterator *
fy_document_iterator_create(void)
	FY_EXPORT;

/**
 * fy_document_iterator_create_cfg() - Create a document iterator using config
 *
 * Creates a document iterator, that can trawl through a document
 * without using recursion. The iterator will generate all the events
 * that created the given document starting at iterator root.
 *
 * @cfg: The document iterator to destroy
 *
 * Returns:
 * The newly created document iterator or NULL on error
 */
struct fy_document_iterator *
fy_document_iterator_create_cfg(const struct fy_document_iterator_cfg *cfg)
	FY_EXPORT;

/**
 * fy_document_iterator_create_on_document() - Create a document iterator on document
 *
 * Creates a document iterator, starting at the root of the given document.
 *
 * @fyd: The document to iterate on
 *
 * Returns:
 * The newly created document iterator or NULL on error
 */
struct fy_document_iterator *
fy_document_iterator_create_on_document(struct fy_document *fyd)
	FY_EXPORT;

/**
 * fy_document_iterator_create_on_node() - Create a document iterator on node
 *
 * Creates a document iterator, starting at the given node
 *
 * @fyn: The node to iterate on
 *
 * Returns:
 * The newly created document iterator or NULL on error
 */
struct fy_document_iterator *
fy_document_iterator_create_on_node(struct fy_node *fyn)
	FY_EXPORT;

/**
 * fy_document_iterator_destroy() - Destroy the given document iterator
 *
 * Destroy a document iterator created earlier via fy_document_iterator_create().
 *
 * @fydi: The document iterator to destroy
 */
void
fy_document_iterator_destroy(struct fy_document_iterator *fydi)
	FY_EXPORT;

/**
 * fy_document_iterator_event_free() - Free an event that was created by a document iterator
 *
 * Free (possibly recycling) an event that was created by a document iterator.
 *
 * @fydi: The document iterator that created the event
 * @fye: The event
 */
void
fy_document_iterator_event_free(struct fy_document_iterator *fydi, struct fy_event *fye)
	FY_EXPORT;

/**
 * fy_document_iterator_stream_start() - Create a stream start event using the iterator
 *
 * Creates a stream start event on the document iterator and advances the internal state
 * of it accordingly.
 *
 * @fydi: The document iterator to create the event
 *
 * Returns:
 * The newly created stream start event, or NULL on error.
 */
struct fy_event *
fy_document_iterator_stream_start(struct fy_document_iterator *fydi)
	FY_EXPORT;

/**
 * fy_document_iterator_stream_end() - Create a stream end event using the iterator
 *
 * Creates a stream end event on the document iterator and advances the internal state
 * of it accordingly.
 *
 * @fydi: The document iterator to create the event
 *
 * Returns:
 * The newly created stream end event, or NULL on error.
 */
struct fy_event *
fy_document_iterator_stream_end(struct fy_document_iterator *fydi)
	FY_EXPORT;

/**
 * fy_document_iterator_document_start() - Create a document start event using the iterator
 *
 * Creates a document start event on the document iterator and advances the internal state
 * of it accordingly. The document must not be released until an error, cleanup or a call
 * to fy_document_iterator_document_end().
 *
 * @fydi: The document iterator to create the event
 * @fyd: The document containing the document state that is used in the event
 *
 * Returns:
 * The newly created document start event, or NULL on error.
 */
struct fy_event *
fy_document_iterator_document_start(struct fy_document_iterator *fydi, struct fy_document *fyd)
	FY_EXPORT;

/**
 * fy_document_iterator_document_end() - Create a document end event using the iterator
 *
 * Creates a document end event on the document iterator and advances the internal state
 * of it accordingly. The document that was used earlier in the call of
 * fy_document_iterator_document_start() can now be released.
 *
 * @fydi: The document iterator to create the event
 *
 * Returns:
 * The newly created document end event, or NULL on error.
 */
struct fy_event *
fy_document_iterator_document_end(struct fy_document_iterator *fydi)
	FY_EXPORT;

/**
 * fy_document_iterator_body_next() - Create document body events until the end
 *
 * Creates the next document body, depth first until the end of the document.
 * The events are created depth first and are in same exact sequence that the
 * original events that created the document.
 *
 * That means that the finite event stream that generated the document is losslesly
 * preserved in such a way that the document tree representation is functionally
 * equivalent.
 *
 * Repeated calls to this function will generate a stream of SCALAR, ALIAS, SEQUENCE
 * START, SEQUENCE END, MAPPING START and MAPPING END events, returning NULL at the
 * end of the body event stream.
 *
 * @fydi: The document iterator to create the event
 *
 * Returns:
 * The newly created document body event or NULL at an error, or an end of the
 * event stream. Use fy_document_iterator_get_error() to check if an error occured.
 */
struct fy_event *
fy_document_iterator_body_next(struct fy_document_iterator *fydi)
	FY_EXPORT;

/**
 * fy_document_iterator_node_start() - Start a document node iteration run using a starting point
 *
 * Starts an iteration run starting at the given node.
 *
 * @fydi: The document iterator to run with
 * @fyn: The iterator root for the iteration
 */
void
fy_document_iterator_node_start(struct fy_document_iterator *fydi, struct fy_node *fyn)
	FY_EXPORT;

/**
 * fy_document_iterator_node_next() - Return the next node in the iteration sequence
 *
 * Returns a pointer to the next node iterating using as a start the node given
 * at fy_document_iterator_node_start(). The first node returned will be that,
 * followed by all the remaing nodes in the subtree.
 *
 * @fydi: The document iterator to use for the iteration
 *
 * Returns:
 * The next node in the iteration sequence or NULL at the end, or if an error occured.
 */
struct fy_node *
fy_document_iterator_node_next(struct fy_document_iterator *fydi)
	FY_EXPORT;

/**
 * fy_document_iterator_generate_next() - Create events from document iterator
 *
 * This is a method that will handle the complex state of generating
 * stream, document and body events on the given iterator.
 *
 * When generation is complete a NULL event will be generated.
 *
 * @fydi: The document iterator to create the event
 *
 * Returns:
 * The newly created event or NULL at an error, or an end of the
 * event stream. Use fy_document_iterator_get_error() to check if an error occured.
 */
struct fy_event *
fy_document_iterator_generate_next(struct fy_document_iterator *fydi)
	FY_EXPORT;

/**
 * fy_document_iterator_get_error() - Get the error state of the document iterator
 *
 * Returns the error state of the iterator. If it's in error state, return true
 * and reset the iterator to the state just after creation.
 *
 * @fydi: The document iterator to use for checking it's error state.
 *
 * Returns:
 * true if it was in an error state, false otherwise.
 */
bool
fy_document_iterator_get_error(struct fy_document_iterator *fydi)
	FY_EXPORT;

/**
 * enum fy_parser_event_generator_flags - The parser event generator flags
 *
 * @FYPEGF_GENERATE_DOCUMENT_EVENTS: generate document events
 * @FYPEGF_GENERATE_STREAM_EVENTS: generate stream events
 * @FYPEGF_GENERATE_ALL_EVENTS: generate all events
 */
enum fy_parser_event_generator_flags {
	FYPEGF_GENERATE_DOCUMENT_EVENTS	= FY_BIT(0),
	FYPEGF_GENERATE_STREAM_EVENTS	= FY_BIT(1),
	FYPEGF_GENERATE_ALL_EVENTS	= FYPEGF_GENERATE_STREAM_EVENTS | FYPEGF_GENERATE_DOCUMENT_EVENTS,
};

/**
 * fy_parser_set_document_iterator() - Associate a parser with a document iterator
 *
 * Associate a parser with a document iterator, that is instead of parsing the events
 * will be generated by the document iterator.
 *
 * @fyp: The parser
 * @flags: The event generation flags
 * @fydi: The document iterator to associate
 *
 * Returns:
 * 0 on success, -1 on error
 */
int
fy_parser_set_document_iterator(struct fy_parser *fyp, enum fy_parser_event_generator_flags flags,
				struct fy_document_iterator *fydi)
	FY_EXPORT;


#ifdef __cplusplus
}
#endif

#endif	// LIBFYAML_DOCITER_H
