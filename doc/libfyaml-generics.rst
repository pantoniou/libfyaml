Generic API
===========

Generic Storage Types
---------------------

.. c:type:: fy_generic_indirect

   Wrapper attaching YAML metadata to a generic value.

   An indirect is allocated out-of-place and pointed to by a tagged
   :c:type:`fy_generic` word with type tag ``FY_INDIRECT_V``. It stores the
   actual value plus optional metadata controlled by the ``FYGIF_*`` flag
   bits. An alias is encoded as an indirect with ``value`` set to
   ``fy_invalid`` and ``anchor`` holding the alias target name.

.. c:type:: fy_generic_sequence

   Out-of-place storage for a generic sequence.

   A contiguous block of ``count`` :c:type:`fy_generic` items follows the
   header. The allocation must remain 16-byte aligned; the layout is shared
   with :c:type:`fy_generic_collection` and is relied on by low-level generic
   helpers.

.. c:type:: fy_generic_mapping

   Out-of-place storage for a generic mapping.

   Stores ``count`` key/value pairs as a contiguous flexible array of
   :c:type:`fy_generic_map_pair` elements. The allocation must remain
   16-byte aligned.

.. c:type:: fy_generic_collection

   Generic view over a sequence or mapping buffer.

   Shares the same memory layout as :c:type:`fy_generic_sequence`; for
   mappings, ``count`` is the number of pairs and ``items`` contains
   ``2 * count`` interleaved key/value generics.

Generic Type System
-------------------

.. kernel-doc:: include/libfyaml/libfyaml-generic.h
   :doc: Generic runtime type system
   :no-header:

.. kernel-doc:: include/libfyaml/libfyaml-generic.h
   :internal:
