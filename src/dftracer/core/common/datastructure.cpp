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
dftracer::Value Metadata::getTagValue(std::string_view tagKey) const {
  auto it = find(tagKey);
  if (it == data.end()) return dftracer::Value{std::string_view()};
  return std::visit(
      [](const auto& v) -> dftracer::Value {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_arithmetic_v<T>) {
          return dftracer::Value{std::to_string(v)};
        } else if constexpr (std::is_same_v<T, const char*>) {
          return dftracer::Value{v == nullptr ? std::string_view()
                                              : std::string_view(v)};
        } else if constexpr (std::is_same_v<T, std::string>) {
          return dftracer::Value{std::string_view(v)};
        } else {
          return dftracer::Value{std::string_view()};
        }
      },
      std::get<1>(it->second));
}
}  // namespace dftracer