
#include "buffer/spsc_ring_buffer.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <algorithm>
using namespace usb_independent;

int main()
{
    // TEST 1: Explicit overrun accounting
    {
        AudioRingBuffer test_buffer;

        float data[8192] = {};

        const std::size_t written =
            test_buffer.write(data, 8192);

        if (written != 8192)
        {
            std::cerr << "FAIL: Initial buffer fill failed\n";
            return 1;
        }

        const std::size_t extra_written =
            test_buffer.write(data, 10);

        if (extra_written != 0 ||
            test_buffer.overrunCount() != 0)
        {
            std::cerr << "FAIL: write() counted overrun automatically\n";
            return 1;
        }

        test_buffer.recordOverrun(10);

        if (test_buffer.overrunCount() != 10)
        {
            std::cerr << "FAIL: recordOverrun() count incorrect\n";
            return 1;
        }

        std::cout << "PASS: Explicit overrun accounting\n";
    }

    std::cout << "\n";
    std::cout << "====================================\n";
    std::cout << "    SPSC CONCURRENT THREAD TEST     \n";
    std::cout << "====================================\n";

    AudioRingBuffer buffer;

    constexpr std::uint32_t TOTAL_SAMPLES = 1000000;
    constexpr std::size_t CHUNK_SIZE = 256;

    std::atomic<bool> producer_done{false};
    std::atomic<bool> test_failed{false};

    std::uint32_t consumed = 0;

    // PRODUCER THREAD
    std::thread producer([&]()
                         {
    float data[CHUNK_SIZE];

    std::uint32_t produced = 0;

    while (produced < TOTAL_SAMPLES)
    {
        const std::size_t free_space =
            buffer.freeSpace();

        if (free_space == 0)
        {
            std::this_thread::yield();
            continue;
        }

        const std::size_t remaining =
            TOTAL_SAMPLES - produced;

        const std::size_t count =
            std::min({
                remaining,
                CHUNK_SIZE,
                free_space
            });

        for (std::size_t i = 0; i < count; ++i)
        {
            data[i] =
                static_cast<float>(produced + i);
        }

        const std::size_t written =
            buffer.write(data, count);

        produced +=
            static_cast<std::uint32_t>(written);
    }

    producer_done.store(true, std::memory_order_release); });

    // CONSUMER THREAD

    std::thread consumer([&]()
                         {
    float data[CHUNK_SIZE];

    while (true)
    {
        const std::size_t available =
            buffer.available();

        if (available == 0)
        {
            if (producer_done.load(std::memory_order_acquire))
                break;

            std::this_thread::yield();
            continue;
        }

        const std::size_t count =
            std::min(available, CHUNK_SIZE);

        const std::size_t read =
            buffer.read(data, count);

        for (std::size_t i = 0; i < read; ++i)
        {
            const float expected =
                static_cast<float>(consumed);

            if (data[i] != expected)
            {
                test_failed.store(
                    true,
                    std::memory_order_relaxed);

                return;
            }

            ++consumed;
        }
    } });

    producer.join();
    consumer.join();

    std::cout << "\nTOTAL EXPECTED : "
              << TOTAL_SAMPLES << "\n";

    std::cout << "TOTAL CONSUMED : "
              << consumed << "\n";

    std::cout << "OVERRUN COUNT  : "
              << buffer.overrunCount() << "\n";

    std::cout << "UNDERRUN COUNT : "
              << buffer.underrunCount() << "\n";

    std::cout << "UNDERRUN EVENTS: "
              << buffer.underrunEventCount() << "\n";

    if (test_failed.load() || consumed != TOTAL_SAMPLES)
    {
        std::cerr << "\nFAIL: Concurrent data integrity error\n";
        return 1;
    }

    if (buffer.overrunCount() != 0)
    {
        std::cerr << "\nFAIL: Unexpected overrun\n";
        return 1;
    }

    if (buffer.underrunCount() != 0 ||
        buffer.underrunEventCount() != 0)
    {
        std::cerr << "\nFAIL: Unexpected underrun\n";
        return 1;
    }

    std::cout << "\nPASS: Concurrent producer-consumer\n";
    std::cout << "PASS: All samples received in order\n";
    std::cout << "\nALL CONCURRENT TESTS PASSED\n";

    return 0;
}
