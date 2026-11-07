#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/utils/utils.h>
namespace dftracer {

void BaseAggregatedValue::update(BaseAggregatedValue* value) {
  if (!value) return;

  // Type mismatch check - both must be the same type
  if (value->_id != this->_id) {
    return;
  }

  auto id = _id;
  DFTRACER_FOR_EACH_NUMERIC_TYPE(DFTRACER_ANY_NUM_AGGREGATE_UPDATE_MACRO, value,
                                 {});
  DFTRACER_FOR_EACH_STRING_TYPE(DFTRACER_ANY_GENERAL_AGGREGATE_UPDATE_MACRO,
                                value, {});
}

BaseAggregatedValue* BaseAggregatedValue::get_value() { return _child; }
std::string Metadata::getTagValue(const std::string& tagKey) const {
  auto it = find(tagKey);
  if (it != data.end()) {
    DFTRACER_FOR_EACH_NUMERIC_TYPE(DFTRACER_ANY_CAST_MACRO,
                                   std::get<1>(it->second),
                                   { return std::to_string(res.value()); });
    DFTRACER_FOR_EACH_STRING_TYPE(DFTRACER_ANY_CAST_MACRO,
                                  std::get<1>(it->second),
                                  { return res.value(); });
  }
  return "";
}
}  // namespace dftracer