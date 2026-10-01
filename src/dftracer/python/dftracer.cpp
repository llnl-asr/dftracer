
#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/dftracer_main.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/core/utils/utils.h>
#include <execinfo.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <tuple>

#include "dftracer.cpp.in"

PYBIND11_MODULE(dftracer, m) {
  m.doc() = "Python module for dftracer";  // optional module docstring
  m.def("initialize", &dftracer::initialize, "initialize dftracer",
        py::arg("log_file") = nullptr, py::arg("data_dirs") = nullptr,
        py::arg("process_id") = -1);
  m.def("get_time", &dftracer::get_time, "get time from profiler");
  m.def("get_config", &dftracer::get_config,
        "generic config lookup by DFTRACER_* env var key name (e.g. "
        "\"DFTRACER_TIME_METRIC\"), returns the resolved current value",
        py::arg("key"));
  m.def("enter_event", &dftracer::enter_event, "mark enter event");
  m.def("exit_event", &dftracer::exit_event, "mark exit event");
  m.def("log_event", &dftracer::log_event, "log event with args",
        py::arg("name"), py::arg("cat"), py::arg("start_time"),
        py::arg("duration"),
        py::arg("int_args") =
            std::unordered_map<std::string, std::tuple<int, int>>(),
        py::arg("string_args") =
            std::unordered_map<std::string, std::tuple<int, std::string>>(),
        py::arg("float_args") =
            std::unordered_map<std::string, std::tuple<int, float>>(),
        py::arg("relations") =
            std::unordered_map<EntityRelation, std::vector<EntityID>>());
  py::enum_<EntityStore>(m, "EntityStore", "Where an entity instance lives",
                        py::module_local())
      .value("MEMORY", DFT_STORE_MEMORY)
      .value("GPU_MEMORY", DFT_STORE_GPU_MEMORY)
      .value("LOCAL_DISK", DFT_STORE_LOCAL_DISK)
      .value("PARALLEL_FS", DFT_STORE_PARALLEL_FS)
      .value("BURST_BUFFER", DFT_STORE_BURST_BUFFER)
      .value("OBJECT_STORE", DFT_STORE_OBJECT_STORE)
      .value("DATABASE", DFT_STORE_DATABASE)
      .value("NETWORK", DFT_STORE_NETWORK)
      .value("OTHER", DFT_STORE_OTHER);
  py::enum_<EntityRole>(m, "EntityRole", "Role of an entity type",
                       py::module_local())
      .value("UNKNOWN", DFT_ROLE_UNKNOWN)
      .value("INPUT", DFT_ROLE_INPUT)
      .value("OUTPUT", DFT_ROLE_OUTPUT)
      .value("INTERMEDIATE", DFT_ROLE_INTERMEDIATE)
      .value("PARAMETER", DFT_ROLE_PARAMETER)
      .value("REFERENCE", DFT_ROLE_REFERENCE);
  py::enum_<EntityRelation>(m, "EntityRelation",
                            "Event->entity (< 16) and entity->entity relations",
                            py::module_local())
      .value("USED", DFT_REL_USED)
      .value("GENERATED", DFT_REL_GENERATED)
      .value("INVALIDATED", DFT_REL_INVALIDATED)
      .value("UPDATED", DFT_REL_UPDATED)
      .value("DERIVED_FROM", DFT_REL_DERIVED_FROM)
      .value("REVISION_OF", DFT_REL_REVISION_OF)
      .value("CONTAINS", DFT_REL_CONTAINS)
      .value("PART_OF", DFT_REL_PART_OF)
      .value("SPECIALIZATION_OF", DFT_REL_SPECIALIZATION_OF)
      .value("ALTERNATE_OF", DFT_REL_ALTERNATE_OF)
      .value("DEPENDS_ON", DFT_REL_DEPENDS_ON);
  m.def("declare_entity", &dftracer::declare_entity,
        "declare entity (type, key) once per process; returns its EntityID "
        "(int; 0 when tracing is off)",
        py::arg("type"), py::arg("key"), py::arg("store") = DFT_STORE_MEMORY,
        py::arg("uri") = "");
  m.def("declare_entity_type", &dftracer::declare_entity_type,
        "describe an entity type once per process: role and description",
        py::arg("type"), py::arg("role") = DFT_ROLE_UNKNOWN,
        py::arg("description") = "");
  m.def("relate_entities", &dftracer::relate_entities,
        "relate two entities (entity->entity relation, e.g. CONTAINS)",
        py::arg("relation"), py::arg("subject"), py::arg("object"));
  m.def("entity_hash", &dftracer::entity_hash,
        "EntityID of (type, key) without declaring it", py::arg("type"),
        py::arg("key"));
  m.def("entity_hex", &dftracer::entity_hex, "16-hex rendering of an EntityID",
        py::arg("id"));
  m.def("log_metadata_event", &dftracer::log_metadata_event,
        "log metadata event", py::arg("key"), py::arg("value"));
  m.def("set_app_metadata_int", &dftracer::set_app_metadata_int,
        "set process-global app metadata (int), folded into the trace's "
        "end event at finalize",
        py::arg("key"), py::arg("value"));
  m.def("set_app_metadata_string", &dftracer::set_app_metadata_string,
        "set process-global app metadata (string), folded into the trace's "
        "end event at finalize",
        py::arg("key"), py::arg("value"));
  m.def("mark_used", &dftracer::mark_used,
        "report that a named sub-layer/integration was exercised this run, "
        "folded into the trace's end event 'used' object",
        py::arg("name"));
  m.def("finalize", &dftracer::finalize, "finalize dftracer");
}
