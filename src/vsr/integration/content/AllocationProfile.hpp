// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstddef>
#include <future>
#include <memory>
#include <thread>

namespace vsr::integration::content::allocation_profile {

#if defined(__GLIBCXX__) && !defined(VSR_DISABLE_HDRI_ALLOCATION_PROFILE)
inline constexpr bool AVAILABLE = true;

template <typename Task>
constexpr size_t asyncBytes()
{
  using Async =
      std::__future_base::_Async_state_impl<std::thread::_Call_wrapper<Task>>;
  return sizeof(std::_Sp_counted_ptr_inplace<Async,
             std::allocator<void>,
             __gnu_cxx::_S_atomic>)
      + sizeof(std::__future_base::_Result<void>) + sizeof(void (Async::*)())
      + sizeof(Async *) + 2 * sizeof(std::max_align_t);
}

template <typename T, typename Allocator>
constexpr size_t sharedControlBytes()
{
  return sizeof(
      std::_Sp_counted_ptr_inplace<T, Allocator, __gnu_cxx::_S_atomic>);
}
#else
// Unknown layouts are unavailable, never replaced with an unproven allowance.
inline constexpr bool AVAILABLE = false;

template <typename Task>
constexpr size_t asyncBytes()
{
  return 0;
}

template <typename T, typename Allocator>
constexpr size_t sharedControlBytes()
{
  return 0;
}
#endif

} // namespace vsr::integration::content::allocation_profile
