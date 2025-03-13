# DFTracer

[![Build and Test](https://github.com/LLNL/dftracer/actions/workflows/ci.yml/badge.svg)](https://github.com/LLNL/dftracer/actions/workflows/ci.yml)
[![Documentation Status](https://readthedocs.org/projects/dftracer/badge/?version=latest)](https://dftracer.readthedocs.io/en/latest/?badge=latest)
![PyPI - Version](https://img.shields.io/pypi/v/pydftracer?label=PyPI)
![PyPI - Wheel](https://img.shields.io/pypi/wheel/pydftracer?label=Wheel)
![PyPI - Python Version](https://img.shields.io/pypi/pyversions/pydftracer?label=Python)
![PyPI - License](https://img.shields.io/pypi/l/pydftracer?label=License)

## Overview

DFTracer is a tracing tool designed to capture both application-code and I/O-call level events from workflows. It provides a unified tracing interface, optimized trace format, and compression mechanism to enable efficient distributed analysis for large-scale AI-driven workloads.

## Prerequisites

Requirements for DFTracer

1. Python>=3.7
1. pybind11

Requirements for DFAnalyzer

1. bokeh>=2.4.2
1. dask>=2023.5.0
1. distributed
1. matplotlib>=3.7.3
1. numpy>=1.24.3
1. pandas>=2.0.3
1. pyarrow>=12.0.1
1. pybind11
1. python-intervals>=1.10.0.post1
1. rich>=13.6.0
1. seaborn>=0.13.2
1. [zindex_py](https://github.com/hariharan-devarajan/zindex.git)

## Installation

Users can easily install DFTracer using `pip`, the standard tool for installing Python packages. 
This method works for both native Python and Conda environments.

### From PyPI

```bash
pip install pydftracer
pip install pydftracer[dfanalyzer]
```

### From Github

```bash
DFTRACER_VERSION=develop
pip install git+https://github.com/LLNL/dftracer.git@${DFTRACER_VERSION}
pip install git+https://github.com/LLNL/dftracer.git@${DFTRACER_VERSION}#egg=pydftracer[dfanalyzer]
```

### From Source

```bash
git clone git@github.com:LLNL/dftracer.git
cd dftracer
# You can skip this for installing the dev branch.
# for latest stable version use master branch.
git checkout tags/<Release> -b <Release>
pip install .
```

For detailed build instructions, click [here](https://dftracer.readthedocs.io/en/latest/build.html).

## Usage

```python
from dftracer.logger import dftracer, dft_fn
log_inst = dftracer.initialize_log(logfile=None, data_dir=None, process_id=-1)
dft_fn = dft_fn("COMPUTE")

# Example of using function decorators
@dft_fn.log
def log_events(index):
    sleep(1)

# Example of function spawning and implicit I/O calls
def posix_calls(val):
    index, is_spawn = val
    path = f"{cwd}/data/demofile{index}.txt"
    f = open(path, "w+")
    f.write("Now the file has more content!")
    f.close()
    if is_spawn:
        print(f"Calling spawn on {index} with pid {os.getpid()}")
        log_inst.finalize() # This need to be called to correctly finalize DFTracer.
    else:
        print(f"Not calling spawn on {index} with pid {os.getpid()}")

# NPZ calls internally calls POSIX calls.
def npz_calls(index):
    path = f"{cwd}/data/demofile{index}.npz"
    if os.path.exists(path):
        os.remove(path)
    records = np.random.randint(255, size=(8, 8, 1024), dtype=np.uint8)
    record_labels = [0] * 1024
    np.savez(path, x=records, y=record_labels)

def main():
    log_events(0)
    npz_calls(1)
    with get_context('spawn').Pool(1, initializer=init) as pool:
        pool.map(posix_calls, ((2, True),))
    log_inst.finalize()

if __name__ == "__main__":
    main()
```

For this example, as the `dftracer.initialize_log` do not pass `logfile` or `data_dir`, we need to set `DFTRACER_LOG_FILE` and `DFTRACER_DATA_DIR`.
By default the DFTracer mode is set to `FUNCTION`.
Example of running this configurations are:

```bash
# The process id, app_name and .pfw will be appended by DFTracer for each app and process.
# The name of the final log file will be ~/log_file-<APP_NAME>-<PID>.pfw
DFTRACER_LOG_FILE=~/log_file
# Colon separated paths to include in the tracing
DFTRACER_DATA_DIR=/dev/shm/:/p/gpfs1/$USER/dataset:$PWD/data
# Enable DFTracer
DFTRACER_ENABLE=1
```

For more examples, click [here](https://dftracer.readthedocs.io/en/latest/examples.html).

## Documentation

* Building DFTracer: [https://dftracer.readthedocs.io/en/latest/build.html](https://dftracer.readthedocs.io/en/latest/build.html)
* Integrating DFTracer: [https://dftracer.readthedocs.io/en/latest/examples.html](https://dftracer.readthedocs.io/en/latest/examples.html)
* Visualizing DFTracer Traces: [https://dftracer.readthedocs.io/en/latest/perfetto.html](https://dftracer.readthedocs.io/en/latest/perfetto.html)
* Building DFAnalyzer: [https://dftracer.readthedocs.io/en/latest/dfanalyzer_build.html](https://dftracer.readthedocs.io/en/latest/dfanalyzer_build.html)

## Citation and Reference

The original SC'24 paper describes the design and implementation of the DFTracer code. Please cite this paper and the code if you use DFTracer in your research. 

```
@inproceedings{devarajan_dftracer_2024,
    address = {Atlanta, GA},
    title = {{DFTracer}: {An} {Analysis}-{Friendly} {Data} {Flow} {Tracer} for {AI}-{Driven} {Workflows}},
    shorttitle = {{DFTracer}},
    urldate = {2024-07-31},
    booktitle = {{SC24}: {International} {Conference} for {High} {Performance} {Computing}, {Networking}, {Storage} and {Analysis}},
    publisher = {IEEE},
    author = {Devarajan, Hariharan and Pottier, Loic and Velusamy, Kaushik and Zheng, Huihuo and Yildirim, Izzet and Kogiou, Olga and Yu, Weikuan and Kougkas, Anthony and Sun, Xian-He and Yeom, Jae Seung and Mohror, Kathryn},
    month = nov,
    year = {2024},
}

@misc{devarajan_dftracer_code_2024,
    type = {Github},
    title = {Github {DFTracer}},
    shorttitle = {{DFTracer}},
    url = {https://github.com/LLNL/dftracer.git},
    urldate = {2024-07-31},
    journal = {DFTracer: A multi-level dataflow tracer for capture I/O calls from worklows.},
    author = {Devarajan, Hariharan and Pottier, Loic and Velusamy, Kaushik and Zheng, Huihuo and Yildirim, Izzet and Kogiou, Olga and Yu, Weikuan and Kougkas, Anthony and Sun, Xian-He and Yeom, Jae Seung and Mohror, Kathryn},
    month = jun,
    year = {2024},
}
```

## Acknowledgments

This work was performed under the auspices of the U.S. Department of Energy by Lawrence Livermore National Laboratory under Contract DE-AC52-07NA27344; and under the auspices of the National Cancer Institute (NCI) by Frederick National Laboratory for Cancer Research (FNLCR) under Contract 75N91019D00024. This research used resources of the Argonne Leadership Computing Facility, a U.S. Department of Energy (DOE) Office of Science user facility at Argonne National Laboratory and is based on research supported by the U.S. DOE Office of Science-Advanced Scientific Computing Research Program, under Contract No. DE-AC02-06CH11357. Office of Advanced Scientific Computing Research under the DOE Early Career Research Program. Also, This material is based upon work partially supported by LLNL LDRD 23-ERD-045 and 24-SI-005. LLNL-CONF-857447.
