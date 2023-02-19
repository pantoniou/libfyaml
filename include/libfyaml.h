/*
 * libfyaml.h - Main header file of the public interface
 *
 * Copyright (c) 2019-2021 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LIBFYAML_H
#define LIBFYAML_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOC: libfyaml public API — umbrella header
 *
 * This is the single top-level include for the entire libfyaml public API.
 * Including it pulls in all subsystem headers:
 *
 * - Core YAML parser, document tree, emitter, and diagnostics
 *   (``libfyaml/libfyaml-core.h``)
 * - General-purpose utilities and portability macros
 *   (``libfyaml/libfyaml-util.h``)
 * - YAML path expression parser and executor
 *   (``libfyaml/libfyaml-path-exec.h``)
 * - Document builder: event-stream to tree conversion
 *   (``libfyaml/libfyaml-docbuild.h``)
 * - Document iterator: tree traversal and event replay
 *   (``libfyaml/libfyaml-dociter.h``)
 * - Composer: callback-driven, path-aware event processing
 *   (``libfyaml/libfyaml-composer.h``)
 * - Pluggable memory allocators
 *   (``libfyaml/libfyaml-allocator.h``)
 * - Thread pool for parallel work
 *   (``libfyaml/libfyaml-thread.h``)
 * - BLAKE3 cryptographic hashing
 *   (``libfyaml/libfyaml-blake3.h``)
 * - Alignment macros and helpers
 *   (``libfyaml/libfyaml-align.h``)
 * - Portable endian detection
 *   (``libfyaml/libfyaml-endian.h``)
 * - Portable atomic operations
 *   (``libfyaml/libfyaml-atomics.h``)
 * - Variable-length size encoding
 *   (``libfyaml/libfyaml-vlsize.h``)
 * - Generic runtime type system
 *   (``libfyaml/libfyaml-generic.h``)
 * - C struct reflection and schema support
 *   (``libfyaml/libfyaml-reflection.h``)
 *
 * For faster compilation you may include only the subsystem headers you need.
 * All public symbols are prefixed with ``fy_`` (functions/types) or ``FY_``
 * (macros and constants).
 */

#include <libfyaml/libfyaml-util.h>
#include <libfyaml/libfyaml-core.h>
#include <libfyaml/libfyaml-path-exec.h>
#include <libfyaml/libfyaml-docbuild.h>
#include <libfyaml/libfyaml-dociter.h>
#include <libfyaml/libfyaml-composer.h>
#include <libfyaml/libfyaml-allocator.h>
#include <libfyaml/libfyaml-thread.h>
#include <libfyaml/libfyaml-blake3.h>
#include <libfyaml/libfyaml-align.h>
#include <libfyaml/libfyaml-endian.h>
#include <libfyaml/libfyaml-atomics.h>
#include <libfyaml/libfyaml-vlsize.h>
#include <libfyaml/libfyaml-generic.h>
#include <libfyaml/libfyaml-reflection.h>

#ifdef __cplusplus
}
#endif

#endif
