DFTRACER_ROOT ?= ../..
DFTRACER_INCLUDEDIR ?= $(abspath $(DFTRACER_ROOT))/include
DFTRACER_LIBDIR ?= $(abspath $(DFTRACER_ROOT))/lib64
CPP_LOGGER_INCLUDEDIR ?= $(DFTRACER_INCLUDEDIR)

CXX ?= g++
MPICXX ?= mpicxx
MPI_LAUNCH ?= mpirun -np 4

CXXFLAGS ?= -O2 -g -std=c++17 -Wall -Wextra -pedantic
CPPFLAGS += -I$(DFTRACER_INCLUDEDIR) -I$(CPP_LOGGER_INCLUDEDIR)
LDFLAGS += -L$(DFTRACER_LIBDIR) -Wl,-rpath,$(DFTRACER_LIBDIR)
LDLIBS += -ldftracer_core -lpapi -lm

TRACE_DIR ?= ./traces
PAPI_EVENTS ?= PAPI_TOT_CYC,PAPI_TOT_INS
PAPI_SAMPLE_INTERVAL_MS ?= 0
SINGLE_TRACE_PREFIX ?= $(TRACE_DIR)/papi-single
MPI_TRACE_PREFIX ?= $(TRACE_DIR)/papi-mpi

SINGLE_NODE_EXE := papi_single_node
MPI_EXE := papi_mpi

.PHONY: all clean syntax-check check-dftracer check-headers run-single run-mpi print-config

all: $(SINGLE_NODE_EXE) $(MPI_EXE)

print-config:
	@echo "DFTRACER_INCLUDEDIR=$(DFTRACER_INCLUDEDIR)"
	@echo "DFTRACER_LIBDIR=$(DFTRACER_LIBDIR)"
	@echo "CPP_LOGGER_INCLUDEDIR=$(CPP_LOGGER_INCLUDEDIR)"
	@echo "CXX=$(CXX)"
	@echo "MPICXX=$(MPICXX)"

check-headers:
	@test -f "$(DFTRACER_INCLUDEDIR)/dftracer/dftracer.h" || \
	 (echo "error: $(DFTRACER_INCLUDEDIR)/dftracer/dftracer.h not found" && \
	  echo "set DFTRACER_INCLUDEDIR to the include directory containing dftracer/dftracer.h" && \
	  exit 1)
	@test -f "$(CPP_LOGGER_INCLUDEDIR)/cpp-logger/logger.h" || \
	 (echo "error: $(CPP_LOGGER_INCLUDEDIR)/cpp-logger/logger.h not found" && \
	  echo "set CPP_LOGGER_INCLUDEDIR to the include directory containing cpp-logger/logger.h" && \
	  exit 1)

check-dftracer:
	@test -f "$(DFTRACER_LIBDIR)/libdftracer_core.so" || \
	 (echo "error: $(DFTRACER_LIBDIR)/libdftracer_core.so not found" && \
	  echo "set DFTRACER_LIBDIR to the directory containing libdftracer_core.so" && \
	  exit 1)

$(SINGLE_NODE_EXE): papi_single_node.cpp | check-headers check-dftracer
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

$(MPI_EXE): papi_mpi.cpp | check-headers check-dftracer
	$(MPICXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

syntax-check: check-headers
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsyntax-only papi_single_node.cpp
	$(MPICXX) $(CPPFLAGS) $(CXXFLAGS) -fsyntax-only papi_mpi.cpp

run-single: $(SINGLE_NODE_EXE)
	mkdir -p $(TRACE_DIR)
	DFTRACER_ENABLE=1 \
	DFTRACER_INC_METADATA=1 \
	DFTRACER_ENABLE_PAPI_TRACING=1 \
	DFTRACER_PAPI_EVENTS='$(PAPI_EVENTS)' \
	DFTRACER_PAPI_SAMPLE_INTERVAL_MS=$(PAPI_SAMPLE_INTERVAL_MS) \
	./$(SINGLE_NODE_EXE) $(SINGLE_TRACE_PREFIX)

run-mpi: $(MPI_EXE)
	mkdir -p $(TRACE_DIR)
	DFTRACER_ENABLE=1 \
	DFTRACER_INC_METADATA=1 \
	DFTRACER_ENABLE_PAPI_TRACING=1 \
	DFTRACER_PAPI_EVENTS='$(PAPI_EVENTS)' \
	DFTRACER_PAPI_SAMPLE_INTERVAL_MS=$(PAPI_SAMPLE_INTERVAL_MS) \
	$(MPI_LAUNCH) ./$(MPI_EXE) $(MPI_TRACE_PREFIX)

clean:
	/bin/rm -f $(SINGLE_NODE_EXE) $(MPI_EXE)
