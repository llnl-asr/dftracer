// CpuPlacer picks the service's CPUs away from its clients' CPUs and their
// SMT siblings, and parses and prints CPU lists.
#include <dftracer/service/pipeline/cpu_placer.h>

#include <string>
#include <vector>

#include "check.h"

using dftracer::CpuPlacer;

namespace {

cpu_set_t cpus(const char* list) {
  cpu_set_t set;
  CPU_ZERO(&set);
  if (list[0] != '\0') DFT_CHECK(CpuPlacer::parse_cpu_list(list, &set));
  return set;
}

std::string pick(const char* base, const char* used,
                 const std::vector<cpu_set_t>& siblings) {
  cpu_set_t out;
  CpuPlacer::choose(cpus(base), cpus(used), siblings, &out);
  return CpuPlacer::format_cpu_list(out);
}

// 12 cores of 2 threads, siblings c and c + 12, as on a Ryzen 9 7900.
std::vector<cpu_set_t> two_way() {
  std::vector<cpu_set_t> siblings(24);
  for (int c = 0; c < 24; ++c) {
    const std::string pair =
        std::to_string(c % 12) + "," + std::to_string(c % 12 + 12);
    siblings[c] = cpus(pair.c_str());
  }
  return siblings;
}

void test_lists() {
  cpu_set_t set;
  DFT_CHECK(CpuPlacer::parse_cpu_list("0-3,8,10-11\n", &set));
  DFT_CHECK(CpuPlacer::format_cpu_list(set) == "0-3,8,10-11");
  DFT_CHECK(CpuPlacer::parse_cpu_list("5", &set));
  DFT_CHECK(CpuPlacer::format_cpu_list(set) == "5");
  for (const char* bad : {"", "x", "3-1", "-2", "1-", "99999"})
    DFT_CHECK(!CpuPlacer::parse_cpu_list(bad, &set));
}

void test_choose() {
  const std::vector<cpu_set_t> smt = two_way();
  // Clients on cores 0-7: the service takes cores 8-11, both threads.
  DFT_CHECK(pick("0-23", "0-7", smt) == "8-11,20-23");
  // Clients on one thread of every core: their siblings are all that is
  // left, which beats sharing a CPU.
  DFT_CHECK(pick("0-23", "0-11", smt) == "12-23");
  // Clients on every CPU: the service stays where it started.
  DFT_CHECK(pick("0-23", "0-23", smt) == "0-23");
  // No clients: likewise.
  DFT_CHECK(pick("0-23", "", smt) == "0-23");
  // Started on part of the node: only CPUs of that part.
  DFT_CHECK(pick("8-11,20-23", "0-8", smt) == "9-11,21-23");
  // Unknown siblings: only the clients' own CPUs are left out.
  DFT_CHECK(pick("0-7", "0-3", std::vector<cpu_set_t>()) == "4-7");
}

}  // namespace

int main() {
  test_lists();
  test_choose();
  return 0;
}
