// lineage through its C++ API, with aggregation on, from two threads: ten
// activities, each traced in full, between work regions that are aggregated.
#include <lineage/lineage.hpp>
#include <string>
#include <thread>

static void run(int first, lineage::Id campaign) {
  for (int i = first; i < first + 5; ++i) {
    std::string in = "in" + std::to_string(i % 3);
    std::string out = "out" + std::to_string(i);
    std::string uri = "file:///data/" + out + ".h5";
    lineage::Id src = lineage::entity("dataset", in.c_str());
    lineage::Id dst = lineage::entity("dataset", out.c_str(), uri.c_str());
    {
      lineage::Activity activity("activity");
      activity.used(src);
      activity.generated(dst);
      activity.updated(campaign);
    }
    lineage::relate(LINEAGE_DERIVED_FROM, dst, src);
    lineage::relate(LINEAGE_CONTAINS, campaign, dst);
    DFTRACER_CPP_REGION(work);
  }
}

int main() {
  DFTRACER_CPP_INIT(nullptr, nullptr, nullptr);
  lineage::entity_type("dataset", "an input or output file set");
  lineage::entity_type("campaign", "a group of datasets");
  lineage::Id campaign = lineage::entity("campaign", "c0");
  std::thread a(run, 0, campaign), b(run, 5, campaign);
  a.join();
  b.join();
  DFTRACER_CPP_FINI();
  return 0;
}
