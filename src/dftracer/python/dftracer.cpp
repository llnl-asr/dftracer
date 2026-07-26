
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
            std::unordered_map<std::string, std::tuple<int, float>>());
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
  m.def("finalize", &dftracer::finalize, "finalize dftracer");
}
