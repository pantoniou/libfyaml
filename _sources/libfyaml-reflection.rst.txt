Reflection API
==============

Introduction
------------

For an overview of the reflection workflow, read :doc:`reflection-guide`
first. It explains how reflection metadata, packed blobs, and
``fy_type_context`` fit together before the symbol-level reference below.

Reflection
----------

.. kernel-doc:: include/libfyaml/libfyaml-reflection.h
   :doc: Reflection — C type introspection and YAML schema support
   :no-header:

YAML Metadata Annotations
-------------------------

.. kernel-doc:: include/libfyaml/libfyaml-reflection.h
   :doc: YAML metadata annotations
   :no-header:

Meta-Type System
----------------

.. kernel-doc:: include/libfyaml/libfyaml-reflection.h
   :doc: Meta-type system — C type + annotation = typed serdes node
   :no-header:

.. kernel-doc:: include/libfyaml/libfyaml-reflection.h
   :internal:
