# Use relative paths for relocatable installations, including custom libdirs.
if(NOT CMAKE_INSTALL_BINDIR)
    set(CMAKE_INSTALL_BINDIR "bin")
endif()
if(NOT CMAKE_INSTALL_LIBDIR)
    set(CMAKE_INSTALL_LIBDIR "lib")
endif()
if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    set(DFTRACER_PRELOAD_HELPER_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
else()
    get_filename_component(_dftracer_helper_bindir "${CMAKE_INSTALL_BINDIR}"
        ABSOLUTE BASE_DIR "${CMAKE_INSTALL_PREFIX}")
    file(RELATIVE_PATH DFTRACER_PRELOAD_HELPER_LIBDIR
        "${_dftracer_helper_bindir}"
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
endif()
# Escape single quotes in the shell assignment.
string(REPLACE "'" "'\"'\"'" DFTRACER_PRELOAD_HELPER_LIBDIR "${DFTRACER_PRELOAD_HELPER_LIBDIR}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/../configure_files/dftracer-preload-library-path.in"
    "${CMAKE_CURRENT_BINARY_DIR}/dftracer-preload-library-path" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/dftracer-preload-library-path"
    DESTINATION "${CMAKE_INSTALL_BINDIR}")
