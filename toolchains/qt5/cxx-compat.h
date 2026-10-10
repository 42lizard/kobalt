#pragma once
#include <type_traits>

// GCC4.9's library headers lack this compile-time trait. The modern compiler
// supplies the intrinsic; no additional libstdc++ runtime symbol is needed.
namespace std {
template<class T>
struct is_trivially_copyable : integral_constant<bool, __is_trivially_copyable(T)> {};
template<class T>
struct is_trivially_copy_constructible : integral_constant<bool,
    __is_trivially_constructible(T, typename add_lvalue_reference<const T>::type)> {};
template<class T, class... Args>
struct is_trivially_constructible : integral_constant<bool, __is_trivially_constructible(T, Args...)> {};
template<class T>
struct is_trivially_copy_assignable : integral_constant<bool,
    __is_trivially_assignable(typename add_lvalue_reference<T>::type,
                             typename add_lvalue_reference<const T>::type)> {};
}
