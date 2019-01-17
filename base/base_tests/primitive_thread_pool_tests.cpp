#include "testing/testing.hpp"

#include "base/primitive_thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
size_t const kTimes = 500;
}  // namespace

UNIT_TEST(PrimitiveThreadPool_SomeThreads)
{
  for (size_t t = 0; t < kTimes; ++t)
  {
    size_t const threadCount = 4;
    std::atomic<size_t> counter{0};
    {
      threads::PrimitiveThreadPool threadPool(threadCount);
      for (size_t i = 0; i < threadCount; ++i)
      {
        threadPool.Submit([&]() {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          ++counter;
        });
      }
    }

    TEST_EQUAL(threadCount, counter, ());
  }
}

UNIT_TEST(PrimitiveThreadPool_OneThread)
{
  for (size_t t = 0; t < kTimes; ++t)
  {
    size_t const threadCount = 1;
    std::atomic<size_t> counter{0};
    {
      threads::PrimitiveThreadPool threadPool(threadCount);
      for (size_t i = 0; i < threadCount; ++i)
      {
        threadPool.Submit([&]() {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          ++counter;
        });
      }
    }

    TEST_EQUAL(threadCount, counter, ());
  }
}

UNIT_TEST(PrimitiveThreadPool_ManyThread)
{
  for (size_t t = 0; t < kTimes; ++t)
  {
    size_t threadCount = std::thread::hardware_concurrency();
    CHECK_NOT_EQUAL(threadCount, 0, ());
    threadCount *= 2;
    std::atomic<size_t> counter{0};
    {
      threads::PrimitiveThreadPool threadPool(threadCount);
      for (size_t i = 0; i < threadCount; ++i)
      {
        threadPool.Submit([&]() {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          ++counter;
        });
      }
    }

    TEST_EQUAL(threadCount, counter, ());
  }
}

UNIT_TEST(PrimitiveThreadPool_ReturnValue)
{
  for (size_t t = 0; t < kTimes; ++t)
  {
    size_t const threadCount = 4;
    threads::PrimitiveThreadPool threadPool(threadCount);
    std::vector<std::future<size_t>> futures;
    for (size_t i = 0; i < threadCount; ++i)
    {
      auto f = threadPool.Submit([=]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return i;
      });

      futures.push_back(std::move(f));
    }

    for (size_t i = 0; i < threadCount; ++i)
      TEST_EQUAL(futures[i].get(), i, ());
  }
}

UNIT_TEST(PrimitiveThreadPool_ManyTasks)
{
  for (size_t t = 0; t < kTimes; ++t)
  {
    size_t taskCount = 11;
    std::atomic<size_t> counter{0};
    {
      threads::PrimitiveThreadPool threadPool(4);
      for (size_t i = 0; i < taskCount; ++i)
      {
        threadPool.Submit([&]() {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          ++counter;
        });
      }
    }

    TEST_EQUAL(taskCount, counter, ());
  }
}
