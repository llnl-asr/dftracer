#include <dftracer/core/common/datastructure.h>

#include <iostream>
#include <typeinfo>

#include "check.h"

using namespace dftracer;

void test_number_aggregation_creation() {
  std::cout << "=== Test: NumberAggregationValue Creation ===\n" << std::endl;

  NumberAggregationValue<int> val1(10);
  NumberAggregationValue<int> val2(20);

  std::cout << "val1: min=" << val1.min << " max=" << val1.max
            << " sum=" << val1.sum << " count=" << val1.count << std::endl;
  std::cout << "val2: min=" << val2.min << " max=" << val2.max
            << " sum=" << val2.sum << " count=" << val2.count << std::endl;

  DFT_CHECK(val1.min == 10);
  DFT_CHECK(val1.max == 10);
  DFT_CHECK(val1.sum == 10);
  DFT_CHECK(val1.count == 1);

  std::cout << "✓ Creation test passed\n" << std::endl;
}

void test_type_info() {
  std::cout << "=== Test: Type Info Matching ===\n" << std::endl;

  NumberAggregationValue<int> val1(10);
  NumberAggregationValue<int> val2(20);

  std::cout << "val1._id: " << val1._id.name() << std::endl;
  std::cout << "val2._id: " << val2._id.name() << std::endl;
  std::cout << "typeid(int): " << typeid(int).name() << std::endl;

  DFT_CHECK(val1._id == val2._id);
  DFT_CHECK(val1._id == typeid(int));

  std::cout << "✓ Type info test passed\n" << std::endl;
}

void test_base_pointer_cast() {
  std::cout << "=== Test: Base Pointer Dynamic Cast ===\n" << std::endl;

  NumberAggregationValue<int> val1(10);
  NumberAggregationValue<int> val2(20);

  BaseAggregatedValue* base1 = &val1;
  BaseAggregatedValue* base2 = &val2;

  std::cout << "base1 ptr: " << base1 << std::endl;
  std::cout << "base2 ptr: " << base2 << std::endl;
  std::cout << "base1._id: " << base1->_id.name() << std::endl;
  std::cout << "base2._id: " << base2->_id.name() << std::endl;

  // Try dynamic_cast back to derived type
  auto* derived1 = dynamic_cast<NumberAggregationValue<int>*>(base1);
  auto* derived2 = dynamic_cast<NumberAggregationValue<int>*>(base2);

  std::cout << "derived1 ptr: " << derived1 << std::endl;
  std::cout << "derived2 ptr: " << derived2 << std::endl;

  if (!derived1) {
    std::cerr << "✗ ERROR: derived1 is NULL after dynamic_cast!" << std::endl;
    std::cerr << "  base1 type: " << typeid(*base1).name() << std::endl;
  }
  if (!derived2) {
    std::cerr << "✗ ERROR: derived2 is NULL after dynamic_cast!" << std::endl;
    std::cerr << "  base2 type: " << typeid(*base2).name() << std::endl;
  }

  DFT_CHECK(derived1 != nullptr);
  DFT_CHECK(derived2 != nullptr);

  std::cout << "✓ Dynamic cast test passed\n" << std::endl;
}

void test_direct_update() {
  std::cout << "=== Test: Direct Update (Derived Type) ===\n" << std::endl;

  NumberAggregationValue<int> val1(10);
  NumberAggregationValue<int> val2(20);

  std::cout << "Before update:" << std::endl;
  std::cout << "  val1: min=" << val1.min << " max=" << val1.max
            << " sum=" << val1.sum << " count=" << val1.count << std::endl;

  val1.update(&val2);

  std::cout << "After update:" << std::endl;
  std::cout << "  val1: min=" << val1.min << " max=" << val1.max
            << " sum=" << val1.sum << " count=" << val1.count << std::endl;

  DFT_CHECK(val1.min == 10);
  DFT_CHECK(val1.max == 20);
  DFT_CHECK(val1.sum == 30);
  DFT_CHECK(val1.count == 2);

  std::cout << "✓ Direct update test passed\n" << std::endl;
}

void test_base_update() {
  std::cout << "=== Test: Base Update (Polymorphic) ===\n" << std::endl;

  NumberAggregationValue<int> val1(10);
  NumberAggregationValue<int> val2(20);

  BaseAggregatedValue* base1 = &val1;
  BaseAggregatedValue* base2 = &val2;

  std::cout << "Before update:" << std::endl;
  std::cout << "  val1: min=" << val1.min << " max=" << val1.max
            << " sum=" << val1.sum << " count=" << val1.count << std::endl;
  std::cout << "  Type match: " << (base1->_id == base2->_id ? "YES" : "NO")
            << std::endl;

  base1->update(base2);

  std::cout << "After update:" << std::endl;
  std::cout << "  val1: min=" << val1.min << " max=" << val1.max
            << " sum=" << val1.sum << " count=" << val1.count << std::endl;

  DFT_CHECK(val1.min == 10);
  DFT_CHECK(val1.max == 20);
  DFT_CHECK(val1.sum == 30);
  DFT_CHECK(val1.count == 2);

  std::cout << "✓ Base update test passed\n" << std::endl;
}

void test_multiple_types() {
  std::cout << "=== Test: Multiple Numeric Types ===\n" << std::endl;

  NumberAggregationValue<int> int_val1(10);
  NumberAggregationValue<int> int_val2(20);

  NumberAggregationValue<double> double_val1(10.5);
  NumberAggregationValue<double> double_val2(20.5);

  BaseAggregatedValue* base_int1 = &int_val1;
  BaseAggregatedValue* base_int2 = &int_val2;
  BaseAggregatedValue* base_double1 = &double_val1;
  BaseAggregatedValue* base_double2 = &double_val2;

  // Int update
  base_int1->update(base_int2);
  DFT_CHECK(int_val1.sum == 30);
  DFT_CHECK(int_val1.count == 2);

  // Double update
  base_double1->update(base_double2);
  DFT_CHECK(double_val1.sum == 31.0);
  DFT_CHECK(double_val1.count == 2);

  // Cross-type update should be safe (no-op)
  base_int1->update(base_double1);
  DFT_CHECK(int_val1.sum == 30);   // Should not change
  DFT_CHECK(int_val1.count == 2);  // Should not change

  std::cout << "✓ Multiple types test passed\n" << std::endl;
}

void test_values_add_in_place() {
  AggregatedValues values;
  const MetadataKey size{StaticKey{"size"}};
  const MetadataKey file{std::string_view("file")};
  values.add_number<int64_t>(size, 10);
  values.add_number<int64_t>(size, 4);
  values.add_number<int64_t>(size, 30);
  values.add_number<double>(size, 99.0);
  values.add_general<const char*>(file, "a");
  values.add_general<const char*>(file, "b");
  values.add_number<int64_t>(file, 7);

  auto* number =
      dynamic_cast<NumberAggregationValue<int64_t>*>(values.values.at(size));
  DFT_CHECK(number != nullptr);
  DFT_CHECK(number->min == 4);
  DFT_CHECK(number->max == 30);
  DFT_CHECK(number->sum == 44);
  DFT_CHECK(number->count == 3);

  auto* general =
      dynamic_cast<AggregatedValue<const char*>*>(values.values.at(file));
  DFT_CHECK(general != nullptr);
  DFT_CHECK(general->_type == ValueType::VALUE_TYPE_STRING);
  DFT_CHECK(general->count == 2);
  for (auto& pair : values.values) delete pair.second;
}

int main() {
  try {
    test_number_aggregation_creation();
    test_type_info();
    test_base_pointer_cast();
    test_direct_update();
    test_base_update();
    test_multiple_types();
    test_values_add_in_place();

    std::cout << "\n✓✓✓ All AggregatedValue tests passed! ✓✓✓\n" << std::endl;
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "✗ Test failed with exception: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "✗ Test failed with unknown exception" << std::endl;
    return 1;
  }
}
