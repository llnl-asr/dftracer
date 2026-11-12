#!/usr/bin/env python3
"""Writes a file whose name holds JSON and shell special characters; see
test/check_odd_names_trace.py."""
import os
import sys

ODD = "odd (\"q\") 'x'|y\\z\tw.txt"

path = os.path.join(sys.argv[1], ODD)
fd = os.open(path, os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o644)
# In-process mode keeps the trace of a process with at least 5 events.
for _ in range(8):
    os.write(fd, b"x")
os.close(fd)
os.unlink(path)
