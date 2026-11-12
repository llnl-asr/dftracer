/*
 * MPI implementation and version detection.
 *
 * Implementation is identified via preprocessor macros (most specific first,
 * since several are layered on top of MPICH and also define MPICH_VERSION).
 *
 * Version is obtained from MPI_Get_library_version() (MPI 3.0+) which queries
 * the *actual loaded library*, making it more reliable than header macros that
 * reflect the version of the installed headers, which may differ from the
 * runtime library.  Falls back to header macros when the runtime call is
 * unavailable or returns an empty string.
 *
 * Outputs two lines to stdout:
 *   IMPL=<NAME>
 *   VERSION=<major.minor.patch>
 *
 * Known IMPL values: OPENMPI, INTELMPI, CRAYMPICH, MVAPICH, MPICH, UNKNOWN
 */
#include <mpi.h>
#include <stdio.h>
#include <string.h>

/* Parse the first "major.minor.patch" or "major.minor" triplet found in str. */
static void parse_version_str(const char *str, int *major, int *minor, int *patch) {
    const char *p = str;
    *major = 0; *minor = 0; *patch = 0;
    while (p && *p) {
        if (sscanf(p, "%d.%d.%d", major, minor, patch) == 3) return;
        if (sscanf(p, "%d.%d", major, minor) == 2) return;
        p++;
    }
}

int main(void) {
    /* Detect implementation from preprocessor macros.  Order matters: more
     * specific implementations must be checked before generic MPICH because
     * Intel MPI, Cray MPICH, and MVAPICH2 all define MPICH_VERSION too. */
    const char *impl = "UNKNOWN";
#if defined(OMPI_MAJOR_VERSION)
    impl = "OPENMPI";
#elif defined(I_MPI_VERSION)
    impl = "INTELMPI";
#elif defined(CRAY_MPICH_VERSION)
    impl = "CRAYMPICH";
#elif defined(MVAPICH2_VERSION)
    impl = "MVAPICH";
#elif defined(MPICH_VERSION)
    impl = "MPICH";
#endif

    int major = 0, minor = 0, patch = 0;

#if defined(CRAY_MPICH_VERSION)
    /* Cray MPICH says its own version only in cray_version.h (e.g.
     * "#define CRAY_MPICH_VERSION 9.0.1.498"); its library reports the
     * MPICH it derives from. The header is also what a build against MPICH
     * plus that header (tools/ci/build-module.sh --cray) has. */
#define DFT_STR2(x) #x
#define DFT_STR(x) DFT_STR2(x)
    parse_version_str(DFT_STR(CRAY_MPICH_VERSION), &major, &minor, &patch);
#endif

#if MPI_VERSION >= 3
    if (major == 0) {
    /* MPI_Get_library_version() is available in MPI 3.0+.  It returns the
     * version of the *runtime library*, which is the authoritative source. */
    char lib_ver[MPI_MAX_LIBRARY_VERSION_STRING];
    int lib_ver_len = 0;
    lib_ver[0] = '\0';
    MPI_Get_library_version(lib_ver, &lib_ver_len);
    if (lib_ver_len > 0) {
        parse_version_str(lib_ver, &major, &minor, &patch);
    }
    }
#endif

    /* Fallback: use header-macro version when library call is unavailable or
     * returned an unparseable string. */
    if (major == 0) {
#if defined(OMPI_MAJOR_VERSION)
        major = OMPI_MAJOR_VERSION;
        minor = OMPI_MINOR_VERSION;
        patch = OMPI_RELEASE_VERSION;
#elif defined(MPICH_VERSION)
        parse_version_str(MPICH_VERSION, &major, &minor, &patch);
#endif
    }

    printf("IMPL=%s\nVERSION=%d.%d.%d\n", impl, major, minor, patch);
    return 0;
}
