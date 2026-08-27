# CUDA (CUPTI) tracing test driven through PyTorch.
#
# Companion to test/cuda/test_cuda.cpp: that one covers the CUDA runtime API,
# copies and memsets without needing nvcc; this one drives real GPU kernels
# through PyTorch so CONCURRENT_KERNEL activity records are exercised too.
#
# Mirrors test/py/hip_test.py, the AMD/rocprofiler equivalent.

import glob
import gzip
import os
import sys
from time import sleep

try:
    import torch
except ImportError:
    print("torch not installed; skipping CUDA tracing test")
    sys.exit(77)

if not torch.cuda.is_available() or torch.version.cuda is None:
    print("No CUDA device available; skipping CUDA tracing test")
    sys.exit(77)

from dftracer.python.dbg import dftracer, dft_fn as Profile

TRACE_FILE = os.environ.get("DFTRACER_CUDA_TEST_LOG", "cuda_data.pfw")

for _stale in glob.glob(os.path.splitext(TRACE_FILE)[0] + "*.pfw") + glob.glob(
    os.path.splitext(TRACE_FILE)[0] + "*.pfw.gz"
):
    os.remove(_stale)

log_inst = dftracer.initialize_log(logfile=TRACE_FILE, data_dir=None, process_id=-1)

comp_dft = Profile("COMPUTE")


def describe_device():
    print("PyTorch version:", torch.__version__)
    print("CUDA version:", torch.version.cuda)
    print("Number of GPUs:", torch.cuda.device_count())
    print("Current GPU:", torch.cuda.get_device_name(0))
    return torch.device("cuda")


@comp_dft.log
def basic_tensor_operations(device):
    """Drive host<->device copies and a few kernels."""
    a = torch.randn(1000, 1000).to(device)
    b = torch.randn(1000, 1000).to(device)

    c = a + b
    d = torch.matmul(a, b)
    e = torch.sum(a, dim=1)

    # Force the queued kernels to complete before the region closes.
    torch.cuda.synchronize()
    print(f"add={c.shape} matmul={d.shape} sum={e.shape}")
    return a, b, c, d


if __name__ == "__main__":
    device = describe_device()
    basic_tensor_operations(device)
    sleep(2)
    log_inst.finalize()

    # DFTracer does not write TRACE_FILE verbatim: it appends a hostname/exec
    # hash and an "-app" suffix, and gzips when compression is on. Discover the
    # files it actually produced rather than assuming the literal name.
    prefix = os.path.splitext(TRACE_FILE)[0]
    traces = sorted(glob.glob(prefix + "*.pfw")) + sorted(
        glob.glob(prefix + "*.pfw.gz")
    )
    assert traces, f"No trace files produced for prefix {prefix!r}"

    data = ""
    for path in traces:
        opener = gzip.open if path.endswith(".gz") else open
        with opener(path, "rt", errors="replace") as f:
            data += f.read()

    # TRACE_TYPE_CUDA is 14; categories come from the CUPTI backend.
    assert "CUDA_KERNEL" in data, "No CUDA kernel events found in trace"
    assert "CUDA_RUNTIME_API" in data, "No CUDA runtime API events found in trace"
    print(f"CUDA events found in {len(traces)} trace file(s): {traces}")
