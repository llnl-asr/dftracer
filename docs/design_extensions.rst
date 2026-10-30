==================================================================
Design: flexible region and metadata APIs (provenance to moirai)
==================================================================

Goal
====

dftracer's develop branch has a provenance API (entities, entity types, entity
relations and event relations) built into the core, beside the general region
and metadata APIs, which could not express it.

This change widens the existing region and metadata APIs in C, C++ and Python
until provenance can be written with them, and removes the provenance API from
dftracer. No new API family is added. Provenance now lives in moirai, which
uses only these APIs.

Records go wherever dftracer events go: into the process's trace, or in
producer/consumer mode into the ring and ``dftracer_service`` (or the spill
file). The ring format and the service are unchanged.

What changed
============

More value types
----------------

* C++: ``DFTracer::update`` takes any number: 64-bit and unsigned integers,
  doubles, and ``bool`` (written as 0 or 1).
* C: ``update_metadata_int64``, ``update_metadata_uint64``,
  ``update_metadata_double`` and the ``DFTRACER_C_{FUNCTION,REGION}_UPDATE_
  {INT64,UINT64,DOUBLE}`` macros.
* Python: ``int_args`` carry 64-bit integers and ``float_args`` doubles (they
  were ``int`` and ``float``).

Lists
-----

``append`` adds a value to the list ``args.<key>``, or with ``sub`` to
``args.<key>.<sub>`` (an object of lists, e.g. ``"relations":{"used":[...]}``):

* C++: ``DFTracer::append(key, value)`` / ``append(key, sub, value)``, values
  strings or ``int64_t``; ``DFTRACER_CPP_{FUNCTION,REGION,REGION_DYN}_APPEND``.
* C: ``update_metadata_append_string`` / ``_int`` (``sub`` is ``NULL`` for a
  plain list); ``DFTRACER_C_{FUNCTION,REGION}_APPEND_{STR,INT}``.
* Python: ``log_event(..., list_args={"key": [...], "key2": {"sub": [...]}})``.

* ``append_id(key, sub, id)`` (C: ``update_metadata_append_id``) adds a 64-bit
  id written as a string of 16 hex digits, e.g. an entity id.

A list is a ``dftracer::ArgList`` in the event's ``Metadata`` under its key:
the values as given -- strings and sub names by pointer, integers and ids by
value -- in the order they were added. It is written as JSON only when the
event is written, straight into the trace line or the ring record; strings are
escaped then. The first ``append`` to a key decides whether it is a list or an
object of lists; appends of the other form to that key are ignored.

Nothing is copied
-----------------

As for ``update`` on a data event before this change, a region keeps what it
is given by pointer: strings passed to ``update`` (data events and records
alike) and to ``append``, and sub names, must outlive the region. Numbers and
ids are stored by value. The fields of an event or record are written once,
from its ``Metadata``, into the trace buffer (in-process) or the ring record
(producer/consumer mode), by one writer for both
(``serialization/metadata_json.h``). Records no longer go through a
``stringstream``. Python copies, as it always has: pybind11 converts its
strings.

Metadata records with fields
----------------------------

A ``DF_METADATA_EVENT`` region now accepts ``update`` and ``append``. With
fields, ending it writes one metadata record (``"ph":4``) named after the
region, whose ``args`` are the fields. Without fields it writes the key/value
event it always wrote, so ``DFTRACER_{C,CPP}_METADATA`` are unchanged.

* C++: ``DFTRACER_CPP_RECORD(name)`` and the region update/append macros.
* C: ``DFTRACER_C_RECORD_START(name)`` / ``DFTRACER_C_RECORD_END(name)``.
* Python: ``log_metadata_event(key, value="", fields={...})``; field values
  are int, float, str, or lists as in ``list_args``.

Fields are written from ``Metadata`` (``metadata_json::fields``), so their
order is the map's.

Event types
-----------

An event type is defined at compile time by the project whose events it
describes: its category, its layer (the ``type`` column), whether its events
are data events or metadata records, and whether they may be aggregated.

* C: ``struct DFTracerEventType {category, layer, kind, aggregatable}``,
  ``initialize_typed_region(name, &type)`` and
  ``DFTRACER_C_TYPED_{FUNCTION,REGION}_START``.
* C++: a struct derived from ``dftracer::EventType`` that overrides what
  differs (the defaults are an aggregatable ``CPP_APP`` data event),
  ``DFTracer(name, Type{})`` and ``DFTRACER_CPP_TYPED_{FUNCTION,REGION,
  REGION_START}``.
* Layers from ``DFTRACER_TRACE_TYPE_EXTERNAL`` (128) to 255 are for other
  projects; below are dftracer's own.

Aggregatability belongs to the layer, not to each event, so no logging
interface changes. A layer is aggregatable until a data-event type on it is
registered as not aggregatable -- when its first region is set up, or with
``dftracer_register_event_type`` -- and from then on every event on it is
traced in full, also with ``DFTRACER_ENABLE_AGGREGATION``. Metadata events
are never aggregated, so their types leave the layer as it is. In
producer/consumer mode the service has no registry: the producer marks such
events with ``SHM_EVENT_NO_AGGREGATE`` in ``EventHead::flags`` (the record
layout is unchanged).

Provenance moved to moirai
--------------------------

dftracer no longer has a provenance API. ``core/common/entity.h``, the
``Entity*`` typedefs, ``dftracer_declare_entity`` /
``dftracer_declare_entity_type`` / ``dftracer_relate_entities``,
``DFTracer::relate`` / ``uses`` / ``generates``, ``update_relation*``, the
``DFTRACER_{C,CPP}_*ENTITY*`` / ``*_RELATE`` / ``*_USES`` / ``*_GENERATES``
macros, the ``EH``/``ET``/``ER`` record names and the Python ``EntityStore`` /
``EntityRole`` / ``EntityRelation`` enums, ``declare_entity*``,
``relate_entities``, ``entity_hash``, ``entity_hex`` and the ``relations``
argument of ``log_event`` are gone.

moirai defines its entities, entity types and relations as metadata records
with fields, and its activities as an event type that is not aggregatable on
``DFTRACER_TRACE_TYPE_EXTERNAL``; once-per-process is a set kept by moirai.
What stays in dftracer is the ``relations`` arg convention
(``DFT_RELATIONS_ARG`` in ``enumeration.h``): an event's ``relations`` are
kept even without ``DFTRACER_INC_METADATA``.

pydftracer
----------

``dftracer.log_event(list_args=...)``, ``log_metadata_event(key, value,
fields=...)`` and ``dft_fn.append(key, value, sub=None)``. The native module
no longer has the entity API or a ``relations`` argument; relations go in
``list_args["relations"]``.

Tests
=====

* ``test_args_c``, ``test_args_cpp``, ``test_args_py`` with
  ``check_args_trace.py``: typed values, lists (plain, nested, escaping, mixed
  forms), records with fields (a field set twice keeps the last value), and a
  metadata region without fields; each with its ``pc_`` twin.
* ``test_event_types_c``, ``test_event_types_cpp`` with
  ``check_event_types_trace.py``, aggregation on: a non-aggregatable type on
  an external layer is traced in full with its relations, the app's own events
  are aggregated, and a metadata type does not change its layer.
* ``test_prov_style_c``, ``test_prov_style_cpp``: a provenance-style API
  defined in the test from the public API only. ``test_external_types_c``,
  ``test_external_types_cpp``: ``test/external_types``, a separate CMake
  project built against the dftracer package (``ExternalProject``), defining
  its own event types and a provenance-style library (``lineage``: C++ with a
  C connector, once-per-process records, entity and activity relations) and
  apps using it, one from two threads. Both checked by
  ``check_prov_style_trace.py``: records written once, no dangling ids,
  activities traced in full on an external layer, the rest aggregated.
* The provenance tests (``test_entity_*``, ``check_entity_trace.py``,
  ``unit_test_entity``) and the entity parts of the API coverage tests are
  removed with the API; moirai tests provenance.
* pydftracer: ``tests/test_args.py``.
