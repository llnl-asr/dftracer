//
// CUDA (CUPTI) tracing test.
//
// Exercises the CUDA runtime API paths DFTracer's CUPTI backend traces --
// device allocation, host<->device copies, memset and synchronization -- inside
// a DFTracer region, then leaves the trace for check_cuda_trace.py to verify.
//
// Deliberately written against the CUDA runtime API only (no __global__
// kernels), so it compiles with the host C++ compiler and needs no nvcc.  GPU
// kernel activity is covered separately by test/py/cuda_test.py.
//
// Exits with code 77 (the CTest "skip" convention, see SKIP_RETURN_CODE) when
// no usable NVIDIA device is present, so the suite stays green on CPU-only
// machines.

#include <cuda_runtime.h>
#include <dftracer/dftracer.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#define DFT_TEST_SKIP 77

#define CUDA_CHECK(call)                                                   \
  do {                                                                     \
    cudaError_t _err = (call);                                             \
    if (_err != cudaSuccess) {                                             \
      fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, #call, \
              cudaGetErrorString(_err));                                   \
      return EXIT_FAILURE;                                                 \
    }                                                                      \
  } while (0)

static const size_t kElements = 1024 * 256;  // 1 MiB of floats

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  int device_count = 0;
  cudaError_t status = cudaGetDeviceCount(&device_count);
  if (status != cudaSuccess || device_count == 0) {
    fprintf(stderr, "No usable CUDA device (%s); skipping CUDA tracing test.\n",
            cudaGetErrorString(status));
    return DFT_TEST_SKIP;
  }

  CUDA_CHECK(cudaSetDevice(0));

  // DFTRACER_CPP_INIT (not the *_NO_BIND variant): the CUPTI backend is started
  // from the bind path of DFTracerCore::initialize, so a no-bind init would
  // leave GPU tracing switched off and the trace empty.
  DFTRACER_CPP_INIT(nullptr, nullptr, nullptr);

  // Everything below sits inside a DFTracer region. The trace checker asserts
  // that the CUPTI-sourced events land inside this region's [ts, ts+dur]
  // window, which is what proves the two clocks are aligned.
  DFTRACER_CPP_REGION_START(CUDA_WORKLOAD);

  const size_t bytes = kElements * sizeof(float);
  std::vector<float> host_in(kElements, 1.5f);
  std::vector<float> host_out(kElements, 0.0f);

  float *device_a = nullptr;
  float *device_b = nullptr;
  CUDA_CHECK(cudaMalloc(&device_a, bytes));
  CUDA_CHECK(cudaMalloc(&device_b, bytes));

  // Host -> device copy.
  CUDA_CHECK(
      cudaMemcpy(device_a, host_in.data(), bytes, cudaMemcpyHostToDevice));
  // Device -> device copy.
  CUDA_CHECK(cudaMemcpy(device_b, device_a, bytes, cudaMemcpyDeviceToDevice));
  // Memset on the device.
  CUDA_CHECK(cudaMemset(device_b, 0, bytes));
  // Async copy on an explicit stream, then a stream synchronize.
  cudaStream_t stream;
  CUDA_CHECK(cudaStreamCreate(&stream));
  CUDA_CHECK(cudaMemcpyAsync(device_b, device_a, bytes,
                             cudaMemcpyDeviceToDevice, stream));
  CUDA_CHECK(cudaStreamSynchronize(stream));
  CUDA_CHECK(cudaStreamDestroy(stream));
  // Device -> host copy.
  CUDA_CHECK(
      cudaMemcpy(host_out.data(), device_b, bytes, cudaMemcpyDeviceToHost));

  CUDA_CHECK(cudaDeviceSynchronize());
  CUDA_CHECK(cudaFree(device_a));
  CUDA_CHECK(cudaFree(device_b));

  DFTRACER_CPP_REGION_END(CUDA_WORKLOAD);

  if (host_out[0] != host_in[0]) {
    fprintf(stderr, "Data mismatch: expected %f got %f\n", host_in[0],
            host_out[0]);
    return EXIT_FAILURE;
  }

  printf("CUDA workload completed on device 0\n");

  // Flush the trace, including any CUPTI activity buffers still in flight.
  DFTRACER_CPP_FINI();
  return EXIT_SUCCESS;
}
