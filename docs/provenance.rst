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
   category, such as a C++ function, a Python region or an MPI call.

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

=========================== ==========================================================================
Type                        Meaning
=========================== ==========================================================================
``EntityID``                ``uint64_t``; 16 lowercase hex characters in traces
``ConstEntityTypeName``     type name, at most ``DFT_ENTITY_TYPE_LEN - 1`` (31)
``ConstEntityKey``          instance key, hashed into the id, not stored
``ConstEntityURI``          locator unique within the app, at most 255 characters
``ConstEntityDescription``  type description, at most 255 characters
``EntityStore``             where the instance lives (enum below)
``EntityRole``              role of an entity type (enum below)
``EntityRelation``          event and entity relations (enum below)
=========================== ==========================================================================

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

Every record is one JSON line.
The ``hhash`` field names the host, so records from different nodes stay apart.
An ``EH`` record says what an id stands for.
An ``ET`` record says what a type means and which role it plays.
An ``ER`` record is one edge between two entities, with the ``relation`` code from the table above.
An event record says which entities it used, generated, invalidated or updated.

Each process writes its own ``EH`` and ``ET`` records, once per entity and once per type.
A trace directory therefore holds the same entity many times, once per process that touched it.
Readers keep the first copy and drop the rest.
The ``store`` is a number from the ``EntityStore`` table and ``uri`` is empty when the application gave none.
Event relations hold only ids.
The entity behind an id is the ``EH`` record with the same ``id``, which may sit in another file.

A reader joins events to entities by id; ids from different processes and
languages join without coordination. ``dftracer-utils`` builds the graph
(``trace::provenance::extract_provenance_graph``, ``GET /api/prov/graph`` and
the *Provenance* tab of ``dftracer_server``).

----------------------------------------
Reading a trace
----------------------------------------

The traces of one run sit in a directory with one ``.pfw.gz`` file per process.
Read them with the ``dftracer-utils`` dataframe API, which scans all files in one pass and joins them by id.

.. code-block:: python

    from dftracer.utils import TraceViewer, col

    view = TraceViewer("traces/", "/tmp/traces-index").phase("any")

    # One scan feeds both reads. Entity records are metadata, so the
    # viewer must read every record family.
    with view.session() as s:
        eh = s.collect(view.filter('name == "EH"'))
        prov = s.collect(view.filter('cat == "PROV"'))

    entities = (
        eh.result()
        .select("args.id", "args.type")
        .rename({"args.id": "entity", "args.type": "type"})
        .distinct("entity")
    )

    # One row per (event, relation, entity), labeled with the entity type.
    events = prov.result()
    edges = (
        events.unpivot(
            id_vars=["name", "pid", "ts"],
            value_vars=[c for c in events.columns if c.startswith("args.relations.")],
        )
        .drop_nulls()
        .join(entities, left_on="value", right_on="entity")
    )
    edges = edges.assign(relation=edges["variable"].str.extract(r"relations\.(\w+)\."))

    made = edges.filter(col("relation") == "generated")
    print(made.group_by(["name", "type"]).agg("count:pid").sort(["name", "type"]).to_pandas())

The second argument of ``TraceViewer`` is where the index goes.
Without it the viewer writes a ``.dftindex`` directory next to the traces, which is not what you want in a shared trace directory.

Two details of the dataframe shape the code.
Metadata records have ``ph`` 4 and are left out of a default query, so ``phase("any")`` is required to see ``EH``, ``ET`` and ``ER``.
The session registers both reads first and runs them together, so the trace is decompressed and parsed once.
Nested fields become columns named by their path, and an array becomes one column per position, such as ``args.relations.used.0`` and ``args.relations.used.1``.
The ``unpivot`` call turns those columns back into one row per relation, and ``distinct`` keeps one ``EH`` record for each entity, since every process writes its own copy.

On a protein structure prediction run of 10 sequences the last line prints which step made which kind of entity, here cut to the first rows:

.. code-block:: text

                      name           type  count_pid
    0           a3m_to_sto  stockholm_msa         10
    1           filter_msa   filtered_msa         10
    2  load_and_merge_msas     merged_msa         10
    3          load_inputs       sequence         10
    4          msa_featgen   msa_features         10
    ...

The same query in duql is one text with no dataframe code:

.. code-block:: python

    from dftracer.utils import TraceViewer

    q = """
    let entities = from all
      | where name == "EH"
      | select entity = args.id, etype = args.type
      | distinct;
    where cat == "PROV"
    | unpivot args.relations.*.* as variable, entity
    | where entity is not null
    | derive etype = entity -> entities.etype
    | derive relation = extract(variable, "relations[.]([a-z]+)[.]", 1)
    | where relation == "generated"
    | group name, etype { n = count() }
    """
    print(TraceViewer("traces/", "/tmp/traces-index").duql(q).collect().to_pandas())

The ``let`` names a row set of the entity records, and ``from all`` reads them although they are metadata, so the viewer needs no ``phase("any")``.
The ``*.*`` pattern in ``unpivot`` expands to every ``args.relations.<name>.<position>`` field in the trace, which does the job of the column list in the dataframe code.
``entity -> entities.etype`` looks up the type of each entity id in the row set.
The result is the same 16 rows, one for each step and entity type.

The ``edges`` table answers the common questions with a filter.
``edges.filter(col("relation") == "used")`` lists what each step consumed, and ``"invalidated"`` lists what each step deleted.
Filtering on ``col("value")`` with one id gives every event that touched that entity, whichever process wrote it.
For a ``revision_of`` or ``contains`` edge, read the ``ER`` records the same way, with ``name == "ER"`` and the columns ``args.relation``, ``args.subject`` and ``args.object``.

----------------------------------------
Walking the graph
----------------------------------------

The lineage of an entity is every entity that the steps before it used, and its impact is every entity that the steps after it made.
Both are a walk over the ``edges`` table above, one join per hop.

.. code-block:: python

    from dftracer.utils import DataFrame

    ACT = ["pid", "ts"]  # an activity has no id; its process and start time name it
    generated = edges.filter(col("relation") == "generated").select(*ACT, "value")
    used = edges.filter(col("relation") == "used").select(*ACT, "value")


    def walk(start, backward=True):
        into, out = (generated, used) if backward else (used, generated)
        seen = DataFrame.from_dict({"value": [start]})
        frontier = seen
        while len(frontier):
            steps = frontier.join(into, on="value").select(*ACT).distinct()
            reached = steps.join(out, on=ACT).select("value").distinct()
            frontier = reached.join(seen, on="value", how="anti")
            seen = seen.concat(frontier)
        return seen.join(entities, left_on="value", right_on="entity")


    best = entities.filter(col("type") == "best_structure").sort("entity")["entity"].to_list()[0]
    lineage = walk(best)
    model = entities.filter(col("type") == "model_params")["entity"].to_list()[0]
    impact = walk(model, backward=False)
    print(len(lineage), len(impact.filter(col("type") == "best_structure")))

.. code-block:: text

    28 10

A backward hop finds the activities that generated the entities in hand and then the entities those activities used.
A forward hop goes the other way, and the anti join drops the entities already seen, so the walk ends when a hop finds nothing new.
On the run above, each best structure comes from 28 entities, from the input chunk and the sequence to the alignments, the reference databases, both model weights and the seed.
Each set of model weights reaches all 10 best structures, since every prediction runs both models and picks the better one.

An activity record has no id field, so the walk names an activity by its ``pid`` and ``ts``.
That is unique as long as one process does not start two activities in the same microsecond.
The ``ER`` edges, such as ``revision_of`` and ``contains``, are not part of the walk, since no activity reads or writes through them.
