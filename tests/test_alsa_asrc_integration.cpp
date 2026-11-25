#include "alsa/alsa_device.hpp"
#include "alsa/alsa_capture_worker.hpp"
#include "asrc/asrc_worker.hpp"
#include "buffer/spsc_ring_buffer.hpp"
#include "jack/jack_output.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace usb_independent;

struct DevicePipeline
{
    std::string name;
    std::string id_path;

    // Buffers harus hidup lebih lama daripada worker.
    std::unique_ptr<AudioRingBuffer> input_buffer;
    std::unique_ptr<AudioRingBuffer> output_buffer;

    std::unique_ptr<AlsaCaptureWorker> capture_worker;
    std::unique_ptr<AsrcWorker> asrc_worker;

    // Snapshot penghitung underrun setelah fase startup.
    std::uint64_t output_underrun_baseline = 0;

    // Underrun selama 60 detik operasi normal.
    std::uint64_t runtime_output_underrun = 0;

    // Baseline dan jumlah kejadian underrun.
    std::uint64_t output_underrun_event_baseline = 0;
    std::uint64_t runtime_output_underrun_events = 0;
};

int main()
{
    constexpr int TEST_DURATION_SECONDS = 60;

    const std::vector<std::string> required_paths = {
        "pci-0000:00:14.0-usb-0:1:1.0",
        "pci-0000:00:14.0-usb-0:2:1.0"};

    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << " USB INDEPENDENT ALSA + ASRC + JACK TEST\n";
    std::cout << "========================================\n";

    AlsaDeviceDetector detector;
    std::vector<DevicePipeline> pipelines;

    // 1. Resolve both physical USB devices.
    for (std::size_t i = 0; i < required_paths.size(); ++i)
    {
        const auto device =
            detector.findByIdPath(required_paths[i]);

        if (device.card_index < 0)
        {
            std::cerr << "ERROR: Device not found:\n"
                      << required_paths[i] << "\n";
            return 1;
        }

        DevicePipeline entry;

        entry.name =
            "CM108_INPUT_0" + std::to_string(i + 1);

        entry.id_path = required_paths[i];

        entry.input_buffer =
            std::make_unique<AudioRingBuffer>();

        entry.output_buffer =
            std::make_unique<AudioRingBuffer>();

        entry.capture_worker =
            std::make_unique<AlsaCaptureWorker>(
                device.id_path,
                device.card_index,
                device.pcm_device,
                entry.name,
                *entry.input_buffer);

        entry.asrc_worker =
            std::make_unique<AsrcWorker>(
                *entry.input_buffer,
                *entry.output_buffer);

        std::cout << "\nDevice: " << entry.name << "\n";
        std::cout << "ALSA card: " << device.card_index << "\n";
        std::cout << "USB topology: " << device.usb_topology << "\n";
        std::cout << "ID_PATH: " << device.id_path << "\n";

        pipelines.push_back(std::move(entry));
    }

    // 2. Open JACK and register one output port per USB device.
    JackOutput jack_output;

    if (!jack_output.open("usb_independent"))
    {
        std::cerr << "ERROR: Cannot open JACK client\n";
        return 1;
    }

    for (auto &entry : pipelines)
    {
        if (!jack_output.addOutputPort(
                entry.name,
                *entry.output_buffer))
        {
            std::cerr << "ERROR: Cannot register JACK port: "
                      << entry.name << "\n";
            return 1;
        }

        std::cout << "Registered JACK port: "
                  << "usb_independent:" << entry.name << "\n";
    }

    // 3. Start independent ASRC workers first.
    std::cout << "\nSTARTING ASRC WORKERS\n";

    for (auto &entry : pipelines)
    {
        if (!entry.asrc_worker->start())
        {
            std::cerr << "ERROR: ASRC start failed: "
                      << entry.name << "\n";

            for (auto &pipeline : pipelines)
                pipeline.capture_worker->stop();

            for (auto &pipeline : pipelines)
                pipeline.asrc_worker->stop();

            jack_output.close();
            return 1;
        }

        std::cout << "Started ASRC: "
                  << entry.name << "\n";
    }

    // 4. Start ALSA capture workers.
    std::cout << "\nSTARTING ALSA CAPTURE\n";

    for (auto &entry : pipelines)
    {
        if (!entry.capture_worker->start())
        {
            std::cerr << "ERROR: ALSA start failed: "
                      << entry.name << "\n";

            for (auto &pipeline : pipelines)
                pipeline.capture_worker->stop();

            for (auto &pipeline : pipelines)
                pipeline.asrc_worker->stop();

            jack_output.close();
            return 1;
        }

        std::cout << "Started: "
                  << entry.name << "\n";
    }

    // 5. Wait until both output buffers reach 512 frames.
    constexpr std::size_t STARTUP_TARGET_FRAMES = 512;
    constexpr auto STARTUP_TIMEOUT =
        std::chrono::seconds(5);

    const auto startup_deadline =
        std::chrono::steady_clock::now() + STARTUP_TIMEOUT;

    bool startup_ready = false;

    std::cout
        << "\nWAITING FOR ASRC OUTPUT BUFFERS\n"
        << "Required occupancy: "
        << STARTUP_TARGET_FRAMES
        << " frames per device\n";

    while (std::chrono::steady_clock::now() < startup_deadline)
    {
        bool workers_healthy = true;
        bool all_buffers_ready = true;

        for (const auto &entry : pipelines)
        {
            if (!entry.capture_worker->isRunning())
            {
                std::cerr
                    << "ERROR: Capture worker stopped: "
                    << entry.name
                    << ", error: "
                    << entry.capture_worker->lastError()
                    << "\n";

                workers_healthy = false;
                break;
            }

            if (!entry.asrc_worker->isRunning() ||
                entry.asrc_worker->lastError() != 0)
            {
                std::cerr
                    << "ERROR: ASRC worker failed: "
                    << entry.name
                    << ", error: "
                    << entry.asrc_worker->lastError()
                    << "\n";

                workers_healthy = false;
                break;
            }

            if (entry.output_buffer->available() <
                STARTUP_TARGET_FRAMES)
            {
                all_buffers_ready = false;
            }
        }

        if (!workers_healthy)
            break;

        if (all_buffers_ready)
        {
            startup_ready = true;
            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    }

    if (!startup_ready)
    {
        std::cerr
            << "ERROR: ASRC startup timeout or worker failure\n";

        for (const auto &entry : pipelines)
        {
            std::cerr
                << entry.name
                << " output occupancy: "
                << entry.output_buffer->available()
                << " / "
                << STARTUP_TARGET_FRAMES
                << " frames\n";
        }

        for (auto &entry : pipelines)
            entry.capture_worker->stop();

        for (auto &entry : pipelines)
            entry.asrc_worker->stop();

        jack_output.close();
        return 1;
    }

    std::cout
        << "Both ASRC output buffers are ready.\n";

    // Snapshot underrun counters before JACK starts consuming.
    for (auto &entry : pipelines)
    {
        entry.output_underrun_baseline =
            entry.output_buffer->underrunCount();

        entry.output_underrun_event_baseline =
            entry.output_buffer->underrunEventCount();

        std::cout
            << entry.name
            << ": "
            << entry.output_buffer->available()
            << " frames ready\n";
    }

    // 6. Activate JACK only after both pipelines are ready.
    if (!jack_output.activate())
    {
        std::cerr << "ERROR: Cannot activate JACK client\n";

        for (auto &entry : pipelines)
            entry.capture_worker->stop();

        for (auto &entry : pipelines)
            entry.asrc_worker->stop();

        jack_output.close();
        return 1;
    }

    std::cout << "JACK callback activated\n";

    // 7. Integration test configuration.
    std::cout << "\nJACK AUDIO CONSUMER ACTIVE\n";
    std::cout << "Sample rate: 48000 Hz\n";
    std::cout << "Period: 128 frames\n";
    std::cout << "Output ports: "
              << jack_output.portCount() << "\n";
    std::cout << "Duration: "
              << TEST_DURATION_SECONDS << " seconds\n";

    std::cout << "\nINITIAL OUTPUT BUFFER OCCUPANCY\n";

    for (auto &entry : pipelines)
    {
        entry.output_underrun_baseline =
            entry.output_buffer->underrunCount();

        entry.output_underrun_event_baseline =
            entry.output_buffer->underrunEventCount();

        std::cout << entry.name << ": "
                  << entry.output_buffer->available()
                  << " frames, startup underrun baseline: "
                  << entry.output_underrun_baseline
                  << " frames\n";
    }

    // 6. JACK callback is the only output ring buffer consumer.
    std::cout << "\nJACK AUDIO CONSUMER ACTIVE\n";
    std::cout << "Sample rate: 48000 Hz\n";
    std::cout << "Period: 128 frames\n";
    std::cout << "Output ports: "
              << jack_output.portCount() << "\n";
    std::cout << "Duration: "
              << TEST_DURATION_SECONDS << " seconds\n";

    // 7. Run both physical USB capture pipelines.
    std::cout << "\nRUNNING AUDIO TEST\n";

    constexpr double REFERENCE_SAMPLE_RATE = 48000.0;

    const auto test_start =
        std::chrono::steady_clock::now();

    const auto test_end =
        test_start +
        std::chrono::seconds(TEST_DURATION_SECONDS);

    auto next_report =
        test_start + std::chrono::seconds(1);

    // Capture frame counter baseline.
    std::vector<std::uint64_t> capture_start_frames;
    std::vector<std::uint64_t> capture_previous_frames;

    capture_start_frames.reserve(pipelines.size());
    capture_previous_frames.reserve(pipelines.size());

    for (const auto &entry : pipelines)
    {
        const auto frames =
            entry.capture_worker->framesCaptured();

        capture_start_frames.push_back(frames);
        capture_previous_frames.push_back(frames);
    }

    auto previous_capture_report = test_start;

    std::cout << "\nCLOCK AND ASRC TELEMETRY (CSV)\n";

    std::cout
        << "time_s,device,"
        << "capture_interval_hz,"
        << "capture_interval_ppm,"
        << "capture_average_hz,"
        << "capture_average_ppm,"
        << "asrc_ratio,"
        << "asrc_correction_ppm,"
        << "output_occupancy\n";

    while (std::chrono::steady_clock::now() < test_end)
    {
        if (jack_output.serverShutdown())
        {
            std::cerr << "ERROR: JACK server shut down\n";
            break;
        }

        const auto now =
            std::chrono::steady_clock::now();

        if (now >= next_report)
        {
            const double elapsed_seconds =
                std::chrono::duration<double>(
                    now - test_start)
                    .count();

            const double interval_seconds =
                std::chrono::duration<double>(
                    now - previous_capture_report)
                    .count();

            std::vector<double> capture_rates;
            capture_rates.reserve(pipelines.size());

            for (std::size_t i = 0; i < pipelines.size(); ++i)
            {
                const auto &entry = pipelines[i];

                const std::uint64_t current_frames =
                    entry.capture_worker->framesCaptured();

                const std::uint64_t interval_frames =
                    current_frames - capture_previous_frames[i];

                const std::uint64_t total_frames =
                    current_frames - capture_start_frames[i];

                const double interval_rate =
                    interval_seconds > 0.0
                        ? static_cast<double>(interval_frames) /
                              interval_seconds
                        : 0.0;

                const double average_rate =
                    elapsed_seconds > 0.0
                        ? static_cast<double>(total_frames) /
                              elapsed_seconds
                        : 0.0;

                const double interval_ppm =
                    (interval_rate / REFERENCE_SAMPLE_RATE - 1.0) * 1000000.0;

                const double average_ppm =
                    (average_rate / REFERENCE_SAMPLE_RATE - 1.0) * 1000000.0;

                capture_rates.push_back(interval_rate);

                const AsrcTelemetry telemetry =
                    entry.asrc_worker->telemetry();

                std::cout
                    << std::fixed
                    << std::setprecision(2)
                    << elapsed_seconds << ","
                    << entry.name << ","
                    << std::setprecision(3)
                    << interval_rate << ","
                    << interval_ppm << ","
                    << average_rate << ","
                    << average_ppm << ","
                    << std::setprecision(9)
                    << telemetry.ratio << ","
                    << std::setprecision(3)
                    << telemetry.correction_ppm << ","
                    << telemetry.filtered_occupancy
                    << "\n";

                capture_previous_frames[i] = current_frames;
            }

            // Perbedaan laju capture antarperangkat.
            if (capture_rates.size() >= 2)
            {
                const double rate_1 = capture_rates[0];
                const double rate_2 = capture_rates[1];

                const double average_pair_rate =
                    (rate_1 + rate_2) / 2.0;

                const double relative_clock_ppm =
                    average_pair_rate > 0.0
                        ? ((rate_2 - rate_1) /
                           average_pair_rate) *
                              1000000.0
                        : 0.0;

                std::cout
                    << "CLOCK_DIFFERENCE,"
                    << std::fixed
                    << std::setprecision(3)
                    << elapsed_seconds << ","
                    << rate_1 << ","
                    << rate_2 << ","
                    << relative_clock_ppm
                    << " ppm\n";
            }

            previous_capture_report = now;
            // Hindari mencetak snapshot berulang jika
            // thread pengujian terlambat berjalan.
            do
            {
                next_report += std::chrono::seconds(1);
            } while (next_report <= now);
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(100));
    }

    // Snapshot underrun sebelum ALSA dihentikan dan ASRC di-drain.
    // Hanya underrun setelah startup baseline yang dihitung.
    for (auto &entry : pipelines)
    {
        const auto current_underrun =
            entry.output_buffer->underrunCount();

        entry.runtime_output_underrun =
            current_underrun >= entry.output_underrun_baseline
                ? current_underrun - entry.output_underrun_baseline
                : 0;

        const auto current_underrun_events =
            entry.output_buffer->underrunEventCount();

        entry.runtime_output_underrun_events =
            current_underrun_events >=
                    entry.output_underrun_event_baseline
                ? current_underrun_events -
                      entry.output_underrun_event_baseline
                : 0;
    }

    // 8. Stop ALSA producers first.
    std::cout << "\nSTOPPING ALSA CAPTURE\n";

    for (auto &entry : pipelines)
    {
        entry.capture_worker->stop();

        std::cout << "Stopped capture: "
                  << entry.name << "\n";
    }

    // 9. Drain ASRC while JACK callback remains active.
    std::cout << "\nDRAINING ASRC WORKERS\n";

    for (auto &entry : pipelines)
    {
        entry.asrc_worker->stop();

        std::cout << "ASRC drain completed: "
                  << entry.name << "\n";
    }

    // 10. Wait until JACK consumes all generated output.
    std::cout << "\nWAITING FOR JACK OUTPUT CONSUMPTION\n";

    const auto output_deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(3);

    bool output_drained = false;

    while (std::chrono::steady_clock::now() < output_deadline)
    {
        if (jack_output.serverShutdown())
        {
            std::cerr << "ERROR: JACK server shut down during drain\n";
            break;
        }

        output_drained = true;

        for (const auto &entry : pipelines)
        {
            if (entry.output_buffer->available() != 0)
            {
                output_drained = false;
                break;
            }
        }

        if (output_drained)
            break;

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    }

    const std::uint64_t jack_frames_consumed =
        jack_output.framesConsumed();

    // No more ring buffer reads after close.
    jack_output.close();

    std::cout << "JACK client closed\n";

    if (output_drained)
        std::cout << "PASS: All output ring buffers drained\n";
    else
        std::cout << "FAIL: Output ring buffers not fully drained\n";

    // 11. Print per-device results.
    std::cout << "\nINTEGRATION RESULTS\n";
    std::cout << "========================================\n";

    bool success = true;
    std::uint64_t total_asrc_generated = 0;

    for (const auto &entry : pipelines)
    {
        const auto captured =
            entry.capture_worker->framesCaptured();

        const auto asrc_consumed =
            entry.asrc_worker->framesConsumed();

        const auto asrc_generated =
            entry.asrc_worker->framesGenerated();

        const auto input_overrun =
            entry.input_buffer->overrunCount();

        const auto output_overrun =
            entry.output_buffer->overrunCount();

        const auto runtime_output_underrun =
            entry.runtime_output_underrun;

        const int capture_error =
            entry.capture_worker->lastError();

        const int asrc_error =
            entry.asrc_worker->lastError();

        total_asrc_generated += asrc_generated;

        std::cout << "\n"
                  << entry.name << "\n";

        std::cout << "Captured frames: "
                  << captured << "\n";

        std::cout << "ASRC input consumed: "
                  << asrc_consumed << "\n";

        std::cout << "ASRC output written: "
                  << asrc_generated << "\n";

        std::cout << "Input buffer remaining: "
                  << entry.input_buffer->available() << "\n";

        std::cout << "Output buffer remaining: "
                  << entry.output_buffer->available() << "\n";

        std::cout << "Input overrun: "
                  << input_overrun << "\n";

        std::cout << "Output overrun: "
                  << output_overrun << "\n";

        std::cout << "Runtime output underrun: "
                  << runtime_output_underrun
                  << " frames\n";

        std::cout << "Runtime output underrun events: "
                  << entry.runtime_output_underrun_events
                  << "\n";

        std::cout << "Capture error: "
                  << capture_error << "\n";

        std::cout << "ASRC error: "
                  << asrc_error << "\n";

        // Telemetry koreksi clock dari ASRC worker.
        const AsrcTelemetry telemetry =
            entry.asrc_worker->telemetry();

        const double frame_conversion_ppm =
            asrc_consumed > 0
                ? (1.0 -
                   static_cast<double>(asrc_generated) /
                       static_cast<double>(asrc_consumed)) *
                      1000000.0
                : 0.0;

        std::cout << "\nASRC CLOCK TELEMETRY\n";
        std::cout << std::fixed << std::setprecision(9);
        std::cout << "Current ASRC ratio: "
                  << telemetry.ratio << "\n";

        std::cout << std::setprecision(3);
        std::cout << "Controller correction: "
                  << telemetry.correction_ppm << " ppm\n";
        std::cout << "Filtered output occupancy: "
                  << telemetry.filtered_occupancy << " frames\n";
        std::cout << "Minimum output occupancy: "
                  << telemetry.min_occupancy << " frames\n";
        std::cout << "Maximum output occupancy: "
                  << telemetry.max_occupancy << " frames\n";
        std::cout << "Average output occupancy: "
                  << telemetry.average_occupancy << " frames\n";
        std::cout << "Controller updates: "
                  << telemetry.controller_updates << "\n";
        std::cout << "Frame conversion difference: "
                  << frame_conversion_ppm << " ppm\n";

        if (captured == 0 ||
            asrc_consumed == 0 ||
            asrc_generated == 0 ||
            asrc_consumed != captured ||
            entry.input_buffer->available() != 0 ||
            entry.output_buffer->available() != 0 ||
            input_overrun != 0 ||
            output_overrun != 0 ||
            runtime_output_underrun != 0 ||
            capture_error != 0 ||
            asrc_error != 0)
        {
            success = false;
        }
    }

    std::cout << "\nTotal ASRC output frames: "
              << total_asrc_generated << "\n";

    std::cout << "Total JACK consumed frames: "
              << jack_frames_consumed << "\n";

    if (jack_frames_consumed == 0 ||
        jack_frames_consumed != total_asrc_generated ||
        jack_output.serverShutdown() ||
        !output_drained)
    {
        success = false;
    }

    std::cout << "\n========================================\n";

    if (success)
        std::cout << "ALSA + ASRC + JACK INTEGRATION PASSED\n";
    else
        std::cout << "ALSA + ASRC + JACK INTEGRATION FAILED\n";

    return success ? 0 : 1;
}
