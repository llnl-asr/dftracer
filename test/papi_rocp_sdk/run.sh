#!/bin/bash
# Tests the PAPI that DFTracer builds (cmake/modules/DFTracerPapi.cmake, with
# cmake/modules/papi-patches) against a mock rocprofiler-sdk, so no GPU is
# needed: a hidden GPU must not stop the visible one from counting, and the
# exit must not corrupt the heap.
#
#   test/papi_rocp_sdk/run.sh <rocm root with rocprofiler-sdk headers> <work dir> [papi prefix]
#
# With a PAPI prefix, that PAPI is tested and none is built.
set -euo pipefail

rocm=${1:?usage: run.sh <rocm root> <work dir> [papi prefix]}
work=${2:?usage: run.sh <rocm root> <work dir> [papi prefix]}
papi=${3:-}
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
mkdir -p "$work"
work=$(cd "$work" && pwd)

if [ -z "$papi" ]; then
  papi=$work/papi
  (cd "$work" && cmake -DDFTRACER_PAPI_PREFIX="$papi" -DDFTRACER_PAPI_ROCM_ROOT="$rocm" \
    -P "$repo/cmake/modules/DFTracerPapi.cmake")
fi
lib=$papi/lib
[ -e "$lib/libpapi.so" ] || lib=$papi/lib64

c++ -std=c++17 -shared -fPIC -D__HIP_PLATFORM_AMD__ -I"$rocm/include" "$here/mock_rocprofiler_sdk.cpp" \
  -Wl,-soname,librocprofiler-sdk.so -o "$work/librocprofiler-sdk.so"
# libpapi before the mock, as libdftracer_core lists them: the mock's exit
# destructors then run after libpapi's, as the real library's do.
# --no-as-needed first, or a linker that drops unused libraries leaves the
# mock to PAPI's dlopen, which runs its destructors before libpapi's.
cc "$here/test_rocp_sdk.c" -I"$papi/include" -Wl,--no-as-needed -L"$lib" -lpapi \
  -L"$work" -lrocprofiler-sdk -Wl,--disable-new-dtags -Wl,-rpath,"$lib" -Wl,-rpath,"$work" \
  -o "$work/test_rocp_sdk"
needed=$(readelf -d "$work/test_rocp_sdk" | grep NEEDED)
case "$needed" in *libpapi*librocprofiler-sdk*) ;; *)
  echo "test_rocp_sdk must need libpapi and then librocprofiler-sdk:" >&2
  echo "$needed" >&2; exit 1 ;; esac
# RPATH, not RUNPATH: a site PAPI on LD_LIBRARY_PATH must not stand in.
ldd "$work/test_rocp_sdk" | grep -E "libpapi|librocprofiler-sdk"

# glibc aborts on the first free of memory that is not allocated.
PAPI_ROCP_SDK_LIB=$work/librocprofiler-sdk.so MALLOC_CHECK_=3 "$work/test_rocp_sdk"
echo "PAPI rocp_sdk mock test passed"
