#!/bin/bash
# benchmark.yml -> IOR benchmark (module MPI instead of the ubuntu openmpi
# packages). The GitHub store-results job (pushes CSVs back to the PR branch)
# is GitHub-only and replaced by artifacts.
set -eo pipefail
cd "$CI_PROJECT_DIR"
export IOR_REPO="${IOR_REPO:-https://github.com/hpc/ior.git}"
export IOR_VERSION="${IOR_VERSION:-4.0.0}"
source .gitlab/flux-ci/toolchain.sh
# install dftracer from the checked-out revision (GitHub cloned the PR branch)
pip install .
ln -sfn "$CI_PROJECT_DIR" dftracer-src
rm -rf ior && git clone $IOR_REPO
cd ior && git checkout tags/$IOR_VERSION -b $IOR_VERSION && ./bootstrap && ./configure && make -j && cd ..
LIB_PATH=$(find venv -name 'libdftracer_preload.so' | head -n 1)
for mode in trace none profile selective; do
  for ts in 4 1024; do
    if [[ "$mode" == "trace" || "$mode" == "profile" ]]; then export LD_PRELOAD=$LIB_PATH; else unset LD_PRELOAD; fi
    if [[ "$mode" == "profile" ]]; then export DFTRACER_ENABLE_AGGREGATION=ON; else unset DFTRACER_ENABLE_AGGREGATION; fi
    if [[ "$mode" == "selective" ]]; then
      export DFTRACER_ENABLE_AGGREGATION=1
      export DFTRACER_AGGREGATION_TYPE=SELECTIVE
      export DFTRACER_AGGREGATION_FILE=dftracer-src/test/yaml/benchmark-rules.yaml
    else
      unset DFTRACER_ENABLE_AGGREGATION DFTRACER_AGGREGATION_TYPE DFTRACER_AGGREGATION_FILE
    fi
    cmd=(./ior/src/ior -w -r -i 5 -t ${ts}k -b $((ts * 16))k -o testfile.dftracer -O summaryFormat=CSV -O summaryFile=case-${mode}-${ts}.csv)
    echo "${cmd[@]}"
    "${cmd[@]}"
    unset LD_PRELOAD DFTRACER_ENABLE_AGGREGATION DFTRACER_AGGREGATION_TYPE DFTRACER_AGGREGATION_FILE
    awk 'BEGIN{FS=OFS=","} NR==1{$(NF+1)="mode"} NR>1{$(NF+1)="'$mode'"} 1' case-${mode}-${ts}.csv > tmp && mv tmp case-${mode}-${ts}.csv
  done
done
head -n 1 case-trace-4.csv > cases.csv
for f in case-*.csv; do tail -n +2 "$f" >> cases.csv; done
cat cases.csv
pip install pandas numpy
python dftracer-src/test/analysis_ior.py > overhead.csv
cat overhead.csv
