#pragma once

#include <cstdlib>

constexpr unsigned MALLOC_CAP_SPIRAM = 1;
constexpr unsigned MALLOC_CAP_8BIT = 2;
struct FakeHeap
{
  bool fail = false;
  size_t allocations = 0;
  size_t bytes = 0;
  unsigned capabilities = 0;
};
inline FakeHeap& fakeHeap()
{
  static FakeHeap heap;
  return heap;
}
inline void* heap_caps_malloc(size_t bytes, unsigned capabilities)
{
  fakeHeap().allocations++;
  fakeHeap().bytes = bytes;
  fakeHeap().capabilities = capabilities;
  return fakeHeap().fail ? nullptr : std::malloc(bytes);
}
inline void heap_caps_free(void* memory) { std::free(memory); }
