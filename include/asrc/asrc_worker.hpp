
#pragma once

#include "asrc/asrc_processor.hpp"
#include "asrc/asrc_ratio_controller.hpp"
#include "buffer/spsc_ring_buffer.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

namespace usb_independent
{

    struct AsrcTelemetry
    {
        double ratio = 1.0;
        double correction_ppm = 0.0;
        double filtered_occupancy = 0.0;

        std::size_t min_occupancy = 0;
        std::size_t max_occupancy = 0;
        double average_occupancy = 0.0;

        std::uint64_t controller_updates = 0;
    };

    class AsrcWorker
    {
    public:
        AsrcWorker(
            AudioRingBuffer &input_buffer,
            AudioRingBuffer &output_buffer);

        ~AsrcWorker();

        AsrcWorker(const AsrcWorker &) = delete;
        AsrcWorker &operator=(const AsrcWorker &) = delete;

        bool start();
        void stop();

        bool isRunning() const noexcept;

        std::uint64_t framesConsumed() const noexcept;
        std::uint64_t framesGenerated() const noexcept;
        AsrcTelemetry telemetry() const noexcept;

        int lastError() const noexcept;

    private:
        void processingLoop();

        AudioRingBuffer &input_buffer_;
        AudioRingBuffer &output_buffer_;

        AsrcProcessor processor_;
        // Prefill input sebelum pemrosesan ASRC normal.
        static constexpr std::size_t INPUT_TARGET_OCCUPANCY = 512;

        // Target antrean output antara ASRC worker dan JACK.
        static constexpr std::size_t OUTPUT_TARGET_OCCUPANCY = 512;

        AsrcRatioController ratio_controller_{OUTPUT_TARGET_OCCUPANCY};

        static constexpr std::size_t BLOCK_SIZE = 256;
        static constexpr std::size_t OUTPUT_CAPACITY = 1024;
        static constexpr std::chrono::milliseconds DRAIN_TIMEOUT{2000};
        std::array<float, BLOCK_SIZE> input_staging_{};
        std::array<float, OUTPUT_CAPACITY> output_staging_{};

        std::size_t pending_input_frames_ = 0;
        std::size_t pending_output_frames_ = 0;

        std::thread thread_;

        // Melindungi operasi start/stop dan lifecycle thread.
        mutable std::mutex lifecycle_mutex_;

        std::atomic<bool> drain_requested_{false};
        std::atomic<bool> running_{false};

        std::atomic<std::uint64_t> frames_consumed_{0};
        std::atomic<std::uint64_t> frames_generated_{0};

        // Read-only telemetry, dipublikasikan oleh ASRC worker.
        std::atomic<double> telemetry_ratio_{1.0};
        std::atomic<double> telemetry_correction_ppm_{0.0};
        std::atomic<double> telemetry_filtered_occupancy_{0.0};

        std::atomic<std::size_t> telemetry_min_occupancy_{0};
        std::atomic<std::size_t> telemetry_max_occupancy_{0};
        std::atomic<double> telemetry_average_occupancy_{0.0};

        std::atomic<std::uint64_t> telemetry_controller_updates_{0};

        std::atomic<int> last_error_{0};
    };

}
