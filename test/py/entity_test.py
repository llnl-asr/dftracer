"""Entity/relation Python API (pydftracer over the native module).

Same (type, key) pairs as test/c/test_entity.c and test/cpp/test_entity.cpp,
so all three traces must agree on entity ids. Checked by
test/check_entity_trace.py.
"""
from dftracer.python import (
    EntityRelation,
    EntityRole,
    EntityStore,
    dft_fn,
    dftracer,
)

log = dftracer.initialize_log(logfile=None, data_dir=None, process_id=-1)
log.declare_entity_type("raw_sample", EntityRole.INPUT, "An input sample")
log.declare_entity_type(
    "protein_structure", EntityRole.OUTPUT, 'A predicted "structure" | with bad chars'
)
for i in range(3):
    with dft_fn("PY_APP", name="transform") as f:
        f.uses("raw_sample", f"sample-{i}")
        f.generates("protein_structure", "MGYP0001")
with dft_fn("PY_APP", name="pack") as f:
    archive = f.generates("archive", "a.tar", EntityStore.PARALLEL_FS, "/p/out/a.tar")
    member = f.uses("protein_structure", "MGYP0001")
log.relate_entities(EntityRelation.CONTAINS, archive, member)
# An event relation is not an entity relation: ignored.
log.relate_entities(EntityRelation.USED, archive, member)
assert archive and member and archive != member
log.finalize()
