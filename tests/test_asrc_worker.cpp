
#include "asrc/asrc_worker.hpp"
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <thread>

using namespace usb_independent;

int main()
{
    constexpr std::size_t TEST_FRAMES = 4096;
    constexpr double PI = 3.141592653589793;

    AudioRingBuffer input_a;
    AudioRingBuffer input_b;

    AudioRingBuffer output_a;
    AudioRingBuffer output_b;

    AsrcWorker worker_a(input_a, output_a);
    AsrcWorker worker_b(input_b, output_b);

    std::cout << "ASRC WORKER TEST\n";
    std::cout << "--------------------------------\n";

    // Test 1: Start two independent workers.
    if (!worker_a.start() || !worker_b.start())
    {
        std::cerr << "FAIL: Worker startup\n";
        worker_a.stop();
        worker_b.stop();
        return 1;
    }

    std::cout << "PASS: Two workers started\n";

    // Test 1B: Repeated start must be rejected.
    if (worker_a.start() || worker_b.start())
    {
        std::cerr << "FAIL: Repeated start was accepted\n";
        worker_a.stop();
        worker_b.stop();
        return 1;
    }

    std::cout << "PASS: Repeated start rejected\n";

    // Test 2: Generate independent input signals.
    std::array<float, TEST_FRAMES> signal_a{};
    std::array<float, TEST_FRAMES> signal_b{};

    for (std::size_t i = 0; i < TEST_FRAMES; ++i)
    {
        signal_a[i] = static_cast<float>(
            0.5 * std::sin(
                      2.0 * PI * 1000.0 * i / 48000.0));

        signal_b[i] = static_cast<float>(
            0.5 * std::sin(
                      2.0 * PI * 2000.0 * i / 48000.0));
    }

    const std::size_t written_a =
        input_a.write(signal_a.data(), TEST_FRAMES);

    const std::size_t written_b =
        input_b.write(signal_b.data(), TEST_FRAMES);

    if (written_a != TEST_FRAMES ||
        written_b != TEST_FRAMES)
    {
        std::cerr << "FAIL: Input ring buffer write\n";
        worker_a.stop();
        worker_b.stop();
        return 1;
    }

    std::cout << "PASS: Independent input data written\n";

    // Test 3: Wait for both workers to process the input.
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(3);

    while (std::chrono::steady_clock::now() < deadline)
    {
        if (worker_a.framesConsumed() >= TEST_FRAMES &&
            worker_b.framesConsumed() >= TEST_FRAMES &&
            worker_a.framesGenerated() > 0 &&
            worker_b.framesGenerated() > 0)
        {
            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    }

    const bool processing_ok =
        worker_a.framesConsumed() == TEST_FRAMES &&
        worker_b.framesConsumed() == TEST_FRAMES &&
        worker_a.framesGenerated() > 0 &&
        worker_b.framesGenerated() > 0;

    if (!processing_ok)
    {
        std::cerr << "FAIL: ASRC worker processing timeout\n";
        worker_a.stop();
        worker_b.stop();
        return 1;
    }

    std::cout << "PASS: Both workers processed input\n";

    // Test 4: Stop workers safely.
    worker_a.stop();
    worker_b.stop();

    if (worker_a.lastError() != 0 ||
        worker_b.lastError() != 0)
    {
        std::cerr << "FAIL: Worker processing error\n";
        return 1;
    }

    std::cout << "PASS: Worker shutdown\n";

    // Test 4B: Repeated stop must be safe.
    worker_a.stop();
    worker_b.stop();

    if (worker_a.isRunning() || worker_b.isRunning())
    {
        std::cerr << "FAIL: Worker still running after repeated stop\n";
        return 1;
    }

    std::cout << "PASS: Repeated stop\n";

    // Test 4C: Workers must be restartable.
    if (!worker_a.start() || !worker_b.start())
    {
        std::cerr << "FAIL: Worker restart\n";
        worker_a.stop();
        worker_b.stop();
        return 1;
    }

    if (!worker_a.isRunning() || !worker_b.isRunning())
    {
        std::cerr << "FAIL: Worker running state after restart\n";
        worker_a.stop();
        worker_b.stop();
        return 1;
    }

    std::cout << "PASS: Worker restart\n";

    // Repeated start must still be rejected after restart.
    if (worker_a.start() || worker_b.start())
    {
        std::cerr << "FAIL: Repeated start accepted after restart\n";
        worker_a.stop();
        worker_b.stop();
        return 1;
    }

    worker_a.stop();
    worker_b.stop();

    std::cout << "PASS: Restarted worker shutdown\n";

    // Test 5: Verify output data.
    std::array<float, 8192> result_a{};
    std::array<float, 8192> result_b{};

    const std::size_t output_frames_a =
        output_a.read(result_a.data(), result_a.size());

    const std::size_t output_frames_b =
        output_b.read(result_b.data(), result_b.size());

    if (output_frames_a == 0 ||
        output_frames_b == 0)
    {
        std::cerr << "FAIL: No output audio\n";
        return 1;
    }

    for (std::size_t i = 0; i < output_frames_a; ++i)
    {
        if (!std::isfinite(result_a[i]))
        {
            std::cerr << "FAIL: Invalid output A\n";
            return 1;
        }
    }

    for (std::size_t i = 0; i < output_frames_b; ++i)
    {
        if (!std::isfinite(result_b[i]))
        {
            std::cerr << "FAIL: Invalid output B\n";
            return 1;
        }
    }

    std::cout << "PASS: Finite output samples\n";

    // Test 6: Graceful drain with an active output consumer.
    {
        AudioRingBuffer drain_input;
        AudioRingBuffer drain_output;

        AsrcWorker drain_worker(drain_input, drain_output);

        std::array<float, TEST_FRAMES> drain_signal = signal_a;

        std::atomic<bool> consumer_running{true};
        std::atomic<std::uint64_t> consumer_frames{0};

        std::thread consumer([&]()
                             {
            std::array<float, 128> buffer{};

            while (consumer_running.load())
            {
                const std::size_t received =
                    drain_output.read(buffer.data(), buffer.size());

                consumer_frames.fetch_add(received);

                if (received == 0)
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1));
                }
            } });

        if (!drain_worker.start())
        {
            consumer_running.store(false);
            consumer.join();

            std::cerr << "FAIL: Normal drain worker startup\n";
            return 1;
        }

        if (drain_input.write(
                drain_signal.data(),
                TEST_FRAMES) != TEST_FRAMES)
        {
            drain_worker.stop();
            consumer_running.store(false);
            consumer.join();

            std::cerr << "FAIL: Normal drain input write\n";
            return 1;
        }

        const auto processing_deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(3);

        while (drain_worker.framesConsumed() < TEST_FRAMES &&
               std::chrono::steady_clock::now() < processing_deadline)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }

        if (drain_worker.framesConsumed() != TEST_FRAMES)
        {
            drain_worker.stop();
            consumer_running.store(false);
            consumer.join();

            std::cerr << "FAIL: Normal drain input processing\n";
            return 1;
        }

        // Capture producer sudah berhenti.
        // Consumer tetap aktif selama ASRC melakukan drain.
        drain_worker.stop();

        const auto output_deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(2);

        while (drain_output.available() > 0 &&
               std::chrono::steady_clock::now() < output_deadline)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }

        consumer_running.store(false);
        consumer.join();

        if (drain_worker.lastError() != 0 ||
            drain_worker.framesConsumed() != TEST_FRAMES ||
            drain_worker.framesGenerated() == 0 ||
            drain_output.available() != 0 ||
            consumer_frames.load() != drain_worker.framesGenerated())
        {
            std::cerr << "FAIL: Graceful drain\n";
            std::cerr << "Error: "
                      << drain_worker.lastError() << "\n";
            std::cerr << "Input consumed: "
                      << drain_worker.framesConsumed() << "\n";
            std::cerr << "Output generated: "
                      << drain_worker.framesGenerated() << "\n";
            std::cerr << "Output consumed: "
                      << consumer_frames.load() << "\n";
            return 1;
        }

        std::cout << "PASS: Graceful drain with active consumer\n";
        std::cout << "Drain input consumed: "
                  << drain_worker.framesConsumed() << "\n";
        std::cout << "Drain output generated: "
                  << drain_worker.framesGenerated() << "\n";
        std::cout << "Drain output consumed: "
                  << consumer_frames.load() << "\n";
    }

    // Test 7: Drain timeout when output ring is full.
    {
        AudioRingBuffer timeout_input;
        AudioRingBuffer timeout_output;

        AsrcWorker timeout_worker(timeout_input, timeout_output);

        std::array<float, 8192> filler{};

        if (timeout_output.write(
                filler.data(),
                filler.size()) != filler.size())
        {
            std::cerr << "FAIL: Timeout ring buffer preparation\n";
            return 1;
        }

        if (!timeout_worker.start())
        {
            std::cerr << "FAIL: Timeout worker startup\n";
            return 1;
        }

        if (timeout_input.write(
                signal_a.data(),
                TEST_FRAMES) != TEST_FRAMES)
        {
            timeout_worker.stop();
            std::cerr << "FAIL: Timeout input write\n";
            return 1;
        }

        const auto processing_deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(2);

        while (timeout_worker.framesConsumed() == 0 &&
               std::chrono::steady_clock::now() < processing_deadline)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }

        if (timeout_worker.framesConsumed() == 0)
        {
            timeout_worker.stop();
            std::cerr << "FAIL: Timeout worker did not process input\n";
            return 1;
        }

        const auto stop_start =
            std::chrono::steady_clock::now();

        timeout_worker.stop();

        const auto stop_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - stop_start);

        if (timeout_worker.lastError() != -4 ||
            stop_duration < std::chrono::milliseconds(1800) ||
            stop_duration > std::chrono::milliseconds(3500))
        {
            std::cerr << "FAIL: Drain timeout\n";
            std::cerr << "Error: "
                      << timeout_worker.lastError() << "\n";
            std::cerr << "Stop duration: "
                      << stop_duration.count() << " ms\n";
            return 1;
        }

        std::cout << "PASS: Drain timeout\n";
        std::cout << "Timeout duration: "
                  << stop_duration.count() << " ms\n";
    }

    std::cout << "\nWORKER A\n";
    std::cout << "Input consumed : "
              << worker_a.framesConsumed() << "\n";
    std::cout << "Output written : "
              << worker_a.framesGenerated() << "\n";

    std::cout << "\nWORKER B\n";
    std::cout << "Input consumed : "
              << worker_b.framesConsumed() << "\n";
    std::cout << "Output written : "
              << worker_b.framesGenerated() << "\n";

    std::cout << "\nALL ASRC WORKER TESTS PASSED\n";

    return 0;
}
