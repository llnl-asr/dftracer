#!/bin/bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  run_dlio_training_with_service.sh \
    --trace-dir <path> \
        --service-submit-prefix "<flux submit ... --tasks-per-node=1 ...>" \
        --service-run-prefix "<flux run ... --tasks-per-node=1 ...>" \
                --train-run-prefix "<flux run ... --tasks-per-node=<gpus> ...>" \
        --train-payload "<training command payload>" \
    [--service-start-job-name <name>] \
        [--service-stop-job-name <name>] \
        [--train-job-name <name>]

Run this script inside flux alloc. It submits service start via flux submit,
runs service stop via flux run (blocking), and runs training via flux run.
All logs are kept under --trace-dir.
EOF
}

TRACE_DIR=""
SERVICE_SUBMIT_PREFIX=""
SERVICE_RUN_PREFIX=""
TRAIN_RUN_PREFIX=""
TRAIN_PAYLOAD=""
SERVICE_START_JOB_NAME=""
SERVICE_STOP_JOB_NAME=""
TRAIN_JOB_NAME=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --trace-dir)
            TRACE_DIR="$2"
            shift 2
            ;;
        --service-submit-prefix)
            SERVICE_SUBMIT_PREFIX="$2"
            shift 2
            ;;
        --service-run-prefix)
            SERVICE_RUN_PREFIX="$2"
            shift 2
            ;;
        --train-run-prefix|--train-submit-prefix)
            TRAIN_RUN_PREFIX="$2"
            shift 2
            ;;
        --train-payload)
            TRAIN_PAYLOAD="$2"
            shift 2
            ;;
        --service-start-job-name)
            SERVICE_START_JOB_NAME="$2"
            shift 2
            ;;
        --service-stop-job-name)
            SERVICE_STOP_JOB_NAME="$2"
            shift 2
            ;;
        --train-job-name)
            TRAIN_JOB_NAME="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 1
            ;;
    esac
done

# Fall back to deriving service-run-prefix from service-submit-prefix if not provided.
if [[ -z "$SERVICE_RUN_PREFIX" && -n "$SERVICE_SUBMIT_PREFIX" ]]; then
    SERVICE_RUN_PREFIX="${SERVICE_SUBMIT_PREFIX/flux submit/flux run}"
fi

if [[ -z "$TRACE_DIR" || -z "$SERVICE_SUBMIT_PREFIX" || -z "$SERVICE_RUN_PREFIX" || -z "$TRAIN_RUN_PREFIX" || -z "$TRAIN_PAYLOAD" ]]; then
    echo "Error: --trace-dir, --service-submit-prefix, --service-run-prefix, --train-run-prefix, and --train-payload are required." >&2
    usage
    exit 1
fi

mkdir -p "$TRACE_DIR"
RUNNER_LOG="$TRACE_DIR/service_training_runner.log"
exec > >(tee -a "$RUNNER_LOG") 2>&1

echo "[runner] trace dir: $TRACE_DIR"
echo "[runner] service submit prefix: $SERVICE_SUBMIT_PREFIX"
echo "[runner] service run prefix: $SERVICE_RUN_PREFIX"
echo "[runner] train run prefix: $TRAIN_RUN_PREFIX"

SERVICE_LOG_PREFIX="${DFTRACER_SERVICE_LOG_PREFIX:-${TRACE_DIR}/trace}"

service_running=0

run_service_action() {
    local action="$1"
    local submit_prefix="$2"
    local job_name="$3"

    local payload
    # The service keeps its .pid and .log next to <log_file>_<host>.pfw.gz,
    # one set per node.
    payload=$(printf 'set -euo pipefail; export DFTRACER_ENABLE=${DFTRACER_ENABLE:-1}; export DFTRACER_LOG_FILE=%q; export DFTRACER_TRACE_INTERVAL_MS=${DFTRACER_TRACE_INTERVAL_MS:-1000}; export DFTRACER_LIBUV_THREADS=${DFTRACER_LIBUV_THREADS:-1}; dftracer_service %s' "$SERVICE_LOG_PREFIX" "$action")

    local cmd="$submit_prefix"
    if [[ -n "$job_name" ]]; then
        cmd+=" --job-name $job_name"
    fi
    cmd+=" bash -lc $(printf '%q' "$payload")"

    echo "[runner] executing: $cmd" >&2
    local submit_output
    submit_output="$(eval "$cmd")"
    local job_id
    job_id="$(echo "$submit_output" | awk 'NF{last=$NF} END{print last}')"
    if [[ -z "$job_id" ]]; then
        echo "[runner] failed to parse flux job id from submit output: $submit_output" >&2
        return 1
    fi
    echo "$job_id"
}

run_train_action() {
    local run_prefix="$1"
    local job_name="$2"
    local payload="$3"

    # Wrap the payload to: cd into a temp rundir, enable core dumps, then run.
    local wrapped_payload
    wrapped_payload=$(cat <<'TRAIN_WRAPPER'
set -euo pipefail
RUN_DIR=$(mktemp -d)
echo "[train] temporary run dir: $RUN_DIR"
cd "$RUN_DIR"
ulimit -c unlimited
# Write core files as core.<pid> inside the run dir.
echo "$RUN_DIR/core.%e.%p" | sudo tee /proc/sys/kernel/core_pattern 2>/dev/null || \
    (ulimit -c unlimited; echo "[train] note: could not set core_pattern, using default")
TRAIN_WRAPPER
)
    wrapped_payload+=$'\n'"$payload"$'\n'
    wrapped_payload+=$(cat <<'TRAIN_CLEANUP'
TRAIN_EXIT_CODE=$?
# On failure, run gdb backtrace on every core file found in the run dir.
if [[ $TRAIN_EXIT_CODE -ne 0 ]]; then
    PYTHON_BIN=$(command -v python3 || command -v python || true)
    if [[ -n "$PYTHON_BIN" ]]; then
        shopt -s nullglob
        for corefile in "$RUN_DIR"/core.*; do
            echo "[train] === GDB backtrace for $corefile ==="
            gdb --batch \
                -ex "set pagination off" \
                -ex "thread apply all bt full" \
                -ex "quit" \
                "$PYTHON_BIN" "$corefile" 2>&1 || true
        done
        shopt -u nullglob
    else
        echo "[train] python not found, skipping gdb analysis"
    fi
fi
rm -rf "$RUN_DIR"
exit $TRAIN_EXIT_CODE
TRAIN_CLEANUP
)

    local cmd="$run_prefix"
    if [[ -n "$job_name" ]]; then
        cmd+=" --job-name $job_name"
    fi
    cmd+=" bash -lc $(printf '%q' "$wrapped_payload")"

    echo "[runner] executing: $cmd" >&2
    eval "$cmd"
}

cleanup() {
    if [[ "$service_running" -eq 1 ]]; then
        echo "[runner] stopping dftracer_service"
        set +e
        run_service_action stop "$SERVICE_RUN_PREFIX" "$SERVICE_STOP_JOB_NAME" >/dev/null
        local stop_rc=$?
        set -e
        if [[ $stop_rc -ne 0 ]]; then
            echo "[runner] warning: dftracer_service stop failed with rc=$stop_rc"
        fi
    fi
}

trap cleanup EXIT

echo "[runner] starting dftracer_service"
run_service_action start "$SERVICE_SUBMIT_PREFIX" "$SERVICE_START_JOB_NAME"
service_running=1

echo "[runner] submitting training job"
echo "[runner] training payload: $TRAIN_PAYLOAD"
set +e
run_train_action "$TRAIN_RUN_PREFIX" "$TRAIN_JOB_NAME" "$TRAIN_PAYLOAD"
train_rc=$?
set -e

echo "[runner] training command finished with rc=$train_rc"

echo "[runner] stopping dftracer_service after training completion"
run_service_action stop "$SERVICE_RUN_PREFIX" "$SERVICE_STOP_JOB_NAME"
service_running=0

if [[ $train_rc -ne 0 ]]; then
    echo "[runner] training job failed with rc=$train_rc"
    exit "$train_rc"
fi

echo "[runner] training completed"
