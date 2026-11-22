"""Read the JSON lines of dftracer traces, gzip or plain."""
import gzip
import json


def iter_lines(paths, objects_only=True):
    for path in paths:
        opener = gzip.open if path.endswith(".gz") else open
        with opener(path, "rt") as f:
            for line in f:
                line = line.strip().rstrip(",")
                if line.startswith("{") or (
                    not objects_only and line and line not in ("[", "]")
                ):
                    yield line


def iter_events(paths):
    for line in iter_lines(paths):
        yield json.loads(line)
