# What DFTracer's generated MPI tracers need from brahma's installed headers.
#
# brahma's package version does not change with its interfaces (two brahmas
# that both report 5.1.1 can have different MPI classes), so a version check
# alone accepts a brahma DFTracer's overrides do not match, and the build fails
# with "marked 'override', but does not override". Check for the interface
# features instead:
#   interface/mpi_compat.h                 common + per-family compat layers
#   BRAHMA_MPI_HAS_MPI1_REMOVED in mpi.h   MPI-3.0-removed functions guarded on
#                                          what the build's mpi.h declares
#
# dftracer_brahma_missing_features(<out-var> <include-dir>...)
# sets <out-var> to the list of missing features (empty when compatible).
function(dftracer_brahma_missing_features out)
    set(_missing "")
    set(_iface "")
    foreach(_d ${ARGN})
        if(EXISTS "${_d}/brahma/interface/mpi.h")
            set(_iface "${_d}/brahma/interface")
            break()
        endif()
    endforeach()
    if(NOT _iface)
        list(APPEND _missing "brahma/interface/mpi.h")
    else()
        if(NOT EXISTS "${_iface}/mpi_compat.h")
            list(APPEND _missing "the common/compat MPI interfaces (interface/mpi_compat.h)")
        endif()
        file(STRINGS "${_iface}/mpi.h" _guard REGEX "BRAHMA_MPI_HAS_MPI1_REMOVED" LIMIT_COUNT 1)
        if(NOT _guard)
            list(APPEND _missing "the BRAHMA_MPI_HAS_MPI1_REMOVED guard in interface/mpi.h")
        endif()
    endif()
    set(${out} "${_missing}" PARENT_SCOPE)
endfunction()
