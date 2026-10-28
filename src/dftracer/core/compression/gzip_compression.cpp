#include <dftracer/core/common/singleton.h>
#include <dftracer/core/compression/gzip_compression.h>
namespace dftracer {
template <>
std::shared_ptr<GzipCompression> Singleton<GzipCompression>::instance = nullptr;
template <>
bool Singleton<GzipCompression>::stop_creating_instances = false;
}  // namespace dftracer