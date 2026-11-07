//
// Created by haridev on 3/28/23.
//

#ifndef DFTRACER_SINGLETON_H
#define DFTRACER_SINGLETON_H

#include <dftracer/core/common/logging.h>

#include <iostream>
#include <memory>
#include <utility>
/**
 * Make a class singleton when used with the class. format for class name T
 * Singleton<T>::GetInstance()
 * @tparam T
 */
namespace dftracer {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-var-template"
#endif

template <typename T>
class Singleton {
 public:
  /**
   * Members of Singleton Class
   */
  /**
   * Uses unique pointer to build a static global instance of variable.
   * @tparam T
   * @return instance of T
   */
  template <typename... Args>
  static std::shared_ptr<T> get_instance(Args... args) {
    auto& stop_creating_instances = stop_flag();
    auto& instance = instance_ref();
    if (stop_creating_instances) return nullptr;
    if (instance == nullptr) {
      instance = std::make_shared<T>(std::forward<Args>(args)...);
    }

    return instance;
  }

  /**
   * The instance as a plain pointer, for hot paths: get_instance() returns
   * a shared_ptr copy, an atomic increment and decrement on a reference
   * count every thread shares. Safe because the instance is created once and
   * its storage (instance_ref) is never freed or reassigned.
   */
  template <typename... Args>
  static T* get_instance_ptr(Args... args) {
    if (stop_flag()) return nullptr;
    auto& instance = instance_ref();
    if (__builtin_expect(instance == nullptr, 0))
      return get_instance(std::forward<Args>(args)...).get();
    return instance.get();
  }

  /**
   * Operators
   */
  Singleton& operator=(const Singleton) = delete; /* deleting = operatos*/
 public:
  Singleton(const Singleton&) = delete; /* deleting copy constructor. */
  static void finalize() { stop_flag() = true; }

 private:
  // Keep singleton storage alive until process exit to avoid destruction-order
  // races across shared libraries.
  static std::shared_ptr<T>& instance_ref() {
    static auto* singleton_instance = new std::shared_ptr<T>();
    return *singleton_instance;
  }

  static bool& stop_flag() {
    static auto* stop = new bool(false);
    return *stop;
  }

 protected:
  // All template classes should instantiate the static members
  static bool stop_creating_instances;
  static std::shared_ptr<T> instance;

  Singleton() {} /* hidden default constructor. */
};

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

}  // namespace dftracer
#endif  // DFTRACER_SINGLETON_H
