#!/usr/bin/env python3
"""Typed values, lists and metadata records through the Python native module.
Checked by test/check_args_trace.py."""
import dftracer.dftracer as native

native.initialize(None, None, -1)
t = native.get_time()
native.log_event(
    "typed",
    "PY_APP",
    t,
    1,
    int_args={"i64": (0, -9007199254740993), "flag": (0, 1)},
    float_args={"f64": (0, 2.5)},
)
native.log_event(
    "lists",
    "PY_APP",
    t,
    1,
    list_args={
        "files": ["a.h5", 'b "q".h5'],
        "ids": [1, -2],
        "links": {"in": ["x"], "out": ["y", "z"]},
    },
)
native.log_metadata_event(
    "SAMPLE", fields={"id": "s1", "n": 4, "ratio": 0.5, "tags": ["x", "y"]}
)
native.log_metadata_event("plain_key", "plain_value")
# In-process mode keeps the trace of a process with at least 5 events.
for _ in range(5):
    native.log_event("pad", "PY_APP", native.get_time(), 1)
native.finalize()
