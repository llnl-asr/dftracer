/* lineage through its C API, with aggregation on: ten activities, each
 * traced in full, between work regions that are aggregated. */
#include <lineage/lineage.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
  int i;
  lineage_id campaign;
  (void)argc;
  (void)argv;
  DFTRACER_C_INIT(NULL, NULL, NULL);
  lineage_entity_type("dataset", "an input or output file set");
  lineage_entity_type("campaign", "a group of datasets");
  lineage_entity_type("dataset", "declared again: written once");
  campaign = lineage_entity("campaign", "c0", NULL);
  for (i = 0; i < 10; ++i) {
    char in[16], out[16], uri[32];
    lineage_id src, dst;
    struct lineage_activity* activity;
    snprintf(in, sizeof(in), "in%d", i % 3);
    snprintf(out, sizeof(out), "out%d", i);
    snprintf(uri, sizeof(uri), "file:///data/%s.h5", out);
    src = lineage_entity("dataset", in, NULL);
    dst = lineage_entity("dataset", out, uri);
    activity = lineage_activity_begin("activity");
    lineage_activity_relate(activity, LINEAGE_USED, src);
    lineage_activity_relate(activity, LINEAGE_GENERATED, dst);
    lineage_activity_relate(activity, LINEAGE_UPDATED, campaign);
    /* Not an activity relation: ignored. */
    lineage_activity_relate(activity, LINEAGE_CONTAINS, dst);
    lineage_activity_end(activity);
    lineage_relate(LINEAGE_DERIVED_FROM, dst, src);
    lineage_relate(LINEAGE_CONTAINS, campaign, dst);
    {
      DFTRACER_C_REGION_START(work);
      DFTRACER_C_REGION_END(work);
    }
  }
  DFTRACER_C_FINI();
  return 0;
}
