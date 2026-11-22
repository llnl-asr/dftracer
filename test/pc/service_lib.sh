# Sourced by the run_pc_*.sh scripts. DFTRACER_SERVICE_SHM must be exported.

# pc_service_start <service> [env args...]: `start` returns once the ring
# accepts events.
pc_service_start() {
  local service=$1
  shift
  env "$@" "$service" start
}

# pc_service_stop <service> <log file> [polls]: stop, then wait up to polls
# tenths of a second for the ring to go away.
pc_service_stop() {
  local service=$1 log=$2 polls=${3:-300}
  DFTRACER_ENABLE=1 DFTRACER_LOG_FILE="$log" "$service" stop
  for _ in $(seq "$polls"); do
    [ -e "/dev/shm$DFTRACER_SERVICE_SHM" ] || break
    sleep 0.1
  done
}
