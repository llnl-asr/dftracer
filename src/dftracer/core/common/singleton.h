//
// Created by haridev on 3/28/23.
//

#ifndef DFTRACER_SINGLETON_H
#define DFTRACER_SINGLETON_H

#include <dftracer/core/common/logging.h>

#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>

namespace dftracer {

template <typename T, typename = void>
struct has_no_arg_initialize : std::false_type {};

template <typename T>
struct has_no_arg_initialize<
    T, std::void_t<decltype(std::declval<T>().initialize())>> : std::true_type {
};

template <typename T>
class Singleton {
 public:
  template <typename... Args>
  static std::shared_ptr<T> get_instance(Args... args) {
    if (stop_creating_instances) return nullptr;
    if (instance == nullptr) {
      instance = std::make_shared<T>(std::forward<Args>(args)...);
    }
    return instance;
  }

  Singleton &operator=(const Singleton) = delete;

 public:
  Singleton(const Singleton &) = delete;
  static void finalize() {
    stop_creating_instances = true;
    if (instance == nullptr) return;
    instance->finalize();
  }

 protected:
  // All template classes should instantiate the static members
  static bool stop_creating_instances;
  static std::shared_ptr<T> instance;

  Singleton() {}
};

}  // namespace dftracer
#endif  // DFTRACER_SINGLETON_H
