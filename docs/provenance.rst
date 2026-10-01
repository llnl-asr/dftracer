================================
Entities, Relations & Provenance
================================

DFTracer can record **what data an application works with and how each piece
of data came to be**, alongside its usual timing and I/O events. The same API
serves provenance (lineage of scientific artifacts) and performance analysis
(which events touched which data instances).

.. contents::
   :local:
   :depth: 2

----------------------------------------
What it captures
----------------------------------------

**Entity**
   A typed instance of something the application works with: an input file, a
   dataset shard, a checkpoint, a model, a predicted protein structure, a GPU
   buffer. Identified by ``(type, key)``: the *type* is an application-defined
   class name, the *key* is any application identifier for the instance (a
   path, a sample id, ``step=120``). The key is hashed into a 64-bit
   ``EntityID``; it is never stored, so keys may be long or sensitive.

**Entity type**
   Described once per process with a **role** (input, output, intermediate,
   parameter, reference) and a short description of what it represents.
   Roles let users ask "what went in", "what came out", "what happened
   between".

**Event relation**
   An event (a traced function or region) relates to entities: it **used**
   (consumed), **generated** (produced), **invalidated** (deleted or ended) or
   **updated** (modified in place) them. Any event may carry relations, in any
   category -- a C++ function, a Python region, an MPI call.

**Entity relation**
   Two entities relate directly: **derived_from**, **revision_of**,
   **contains** (has-a), **part_of** (is-in), **specialization_of**
   (inherits / is-a), **alternate_of**, **depends_on**.

Together these form a graph: entities and events as nodes, relations as edges.
Walking ``generated`` edges backwards from an artifact gives its lineage;
walking ``used`` edges forwards gives the impact of an input.

Design rules
   * Enums wherever possible; strings only where unavoidable, and then
     fixed-length and JSON-safe.
   * Identity is computed by the core (FNV-1a-64 of ``type`` 0x1f ``key``), so
     the same ``(type, key)`` gets the same ``EntityID`` in C, C++ and Python,
     in every process and on every node.
   * Each entity and each entity type is recorded **once per process**; later
     declarations cost a set lookup. The first declaration's store and uri win.
   * Relations are the event's content and are written even when
     ``DFTRACER_INC_METADATA`` is off (other optional event args still need it).

----------------------------------------
Types and enums
----------------------------------------

Defined in ``dftracer/core/common/entity.h`` (C-compatible) and
``dftracer/core/common/typedef.h``.

======================== ==================================================
Type                      Meaning
======================== ==================================================
``EntityID``              ``uint64_t``; 16 lowercase hex characters in traces
``ConstEntityTypeName``   type name, at most ``DFT_ENTITY_TYPE_LEN - 1`` (31)
``ConstEntityKey``        instance key, hashed into the id, not stored
``ConstEntityURI``        locator unique within the app, at most 255 characters
``ConstEntityDescription`` type description, at most 255 characters
``EntityStore``           where the instance lives (enum below)
``EntityRole``            role of an entity type (enum below)
``EntityRelation``        event and entity relations (enum below)
======================== ==================================================

Strings are truncated to their capacity and restricted to ``A-Z a-z 0-9 _ - .
: / @ + = , ~`` and space; any other character (quotes, backslash, ``|``,
control characters) becomes ``_``.

.. list-table::
   :header-rows: 1

   * - Enum
     - Values (as written in traces)
   * - ``EntityStore``
     - 0 memory, 1 gpu_memory, 2 local_disk, 3 parallel_fs, 4 burst_buffer,
       5 object_store, 6 database, 7 network, 8 other
   * - ``EntityRole``
     - 0 unknown, 1 input, 2 output, 3 intermediate, 4 parameter, 5 reference
   * - ``EntityRelation`` (event)
     - 0 used, 1 generated, 2 invalidated, 3 updated
   * - ``EntityRelation`` (entity)
     - 16 derived_from, 17 revision_of, 18 contains, 19 part_of,
       20 specialization_of, 21 alternate_of, 22 depends_on

Enum values are part of the trace format and are append-only.

----------------------------------------
C API
----------------------------------------

.. code-block:: c

    #include <dftracer/dftracer.h>

    DFTRACER_C_ENTITY_TYPE("sequence", DFT_ROLE_INPUT, "A query protein sequence");

    void fold(const char* seq_id, const char* out_path) {
      DFTRACER_C_FUNCTION_START();
      DFTRACER_C_FUNCTION_USES("sequence", seq_id);            /* store: memory */
      EntityID s = update_relation_entity(data_fn, DFT_REL_GENERATED,
                                          "protein_structure", seq_id,
                                          DFT_STORE_PARALLEL_FS, out_path);
      DFTRACER_C_FUNCTION_END();
    }

    EntityID tar = DFTRACER_C_ENTITY("structure_tar", seq_id,
                                     DFT_STORE_PARALLEL_FS, tar_path);
    DFTRACER_C_ENTITY_RELATE(DFT_REL_CONTAINS, tar, s);

Functions: ``dftracer_declare_entity``, ``dftracer_declare_entity_type``,
``dftracer_relate_entities``, ``update_relation``, ``update_relation_entity``.
Macros: ``DFTRACER_C_ENTITY``, ``DFTRACER_C_ENTITY_TYPE``,
``DFTRACER_C_ENTITY_RELATE``, ``DFTRACER_C_FUNCTION_RELATE``,
``DFTRACER_C_FUNCTION_USES``, ``DFTRACER_C_FUNCTION_GENERATES``,
``DFTRACER_C_REGION_RELATE``.

----------------------------------------
C++ API
----------------------------------------

.. code-block:: cpp

    #include <dftracer/dftracer.h>

    void relax(const std::string& seq, const std::string& path) {
      DFTRACER_CPP_FUNCTION();
      EntityID raw = profiler_dft_fn.uses("unrelaxed_structure", seq.c_str());
      EntityID out = profiler_dft_fn.generates("relaxed_structure", seq.c_str(),
                                               DFT_STORE_BURST_BUFFER, path.c_str());
      DFTRACER_CPP_ENTITY_RELATE(DFT_REL_REVISION_OF, out, raw);
    }

``DFTracer`` gains ``relate(EntityRelation, EntityID)``,
``relate(EntityRelation, type, key, store, uri)``, ``uses(...)`` and
``generates(...)``; macros ``DFTRACER_CPP_ENTITY``, ``DFTRACER_CPP_ENTITY_TYPE``,
``DFTRACER_CPP_ENTITY_RELATE``, ``DFTRACER_CPP_FUNCTION_RELATE``,
``DFTRACER_CPP_FUNCTION_USES``, ``DFTRACER_CPP_FUNCTION_GENERATES``,
``DFTRACER_CPP_REGION_RELATE``.

----------------------------------------
Python API
----------------------------------------

.. code-block:: python

    from dftracer.python import (dftracer, dft_fn, EntityStore, EntityRole,
                                 EntityRelation)

    log = dftracer.initialize_log(logfile=None, data_dir=None, process_id=-1)
    log.declare_entity_type("filtered_msa", EntityRole.OUTPUT,
                            "Diversity-filtered multiple sequence alignment")

    with dft_fn("APP", name="filter_msa") as f:
        merged = f.uses("merged_msa", seq_id, EntityStore.BURST_BUFFER, merged_path)
        filtered = f.generates("filtered_msa", seq_id, EntityStore.BURST_BUFFER, out_path)
        f.relate_entity(EntityRelation.INVALIDATED, "merged_msa", seq_id)  # deleted

    log.relate_entities(EntityRelation.DERIVED_FROM, filtered, merged)

``dftracer`` gains ``declare_entity`` (returns the id as an ``int``, 0 when
tracing is off), ``declare_entity_type`` and ``relate_entities``; ``dft_fn``
gains ``uses``, ``generates``, ``relate_entity`` and ``relate``. The native
module also exposes ``entity_hash(type, key)`` and ``entity_hex(id)`` to compute
ids without tracing.

----------------------------------------
Trace records
----------------------------------------

Entities, entity types and entity relations are metadata records
(``"ph":4``, ``"cat":"dftracer"``, ``"type":1``) with named fields:

.. code-block:: json

    {"name":"EH","cat":"dftracer","type":1,"pid":1,"tid":1,"ph":4,
     "args":{"hhash":"…","id":"8f0de68c04eea0bf","type":"protein_structure",
             "store":3,"uri":"/p/out/a.pdb"}}
    {"name":"ET","cat":"dftracer","type":1,"pid":1,"tid":1,"ph":4,
     "args":{"hhash":"…","type":"protein_structure","role":2,
             "description":"Predicted 3D structure"}}
    {"name":"ER","cat":"dftracer","type":1,"pid":1,"tid":1,"ph":4,
     "args":{"hhash":"…","relation":18,"subject":"6e6d650dd131e83b",
             "object":"8f0de68c04eea0bf"}}

An event's relations are one ``relations`` object in its ``args``: relation
name to an array of entity ids. Nesting them keeps relation names from
colliding with other event args (the lifecycle ``end`` event has its own
``used`` object, for example):

.. code-block:: json

    {"id":2,"name":"fold","cat":"CPP_APP","type":9,"pid":1,"tid":1,
     "ts":1790877632269434,"dur":32,"ph":1,
     "args":{"hhash":"…","relations":{"used":["d824b0bb91487885"],
                                       "generated":["8f0de68c04eea0bf"]}}}

A reader joins events to entities by id; ids from different processes and
languages join without coordination. ``dftracer-utils`` builds the graph
(``trace::provenance::extract_provenance_graph``, ``GET /api/prov/graph`` and
the *Provenance* tab of ``dftracer_server``).
