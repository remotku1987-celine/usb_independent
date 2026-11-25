
#include "asrc/asrc_worker.hpp"

#include <algorithm>
#include <chrono>

namespace usb_independent
{

    AsrcWorker::AsrcWorker(
        AudioRingBuffer &input_buffer,
        AudioRingBuffer &output_buffer)
        : input_buffer_(input_buffer),
          output_buffer_(output_buffer)
    {
    }

    AsrcWorker::~AsrcWorker()
    {
        stop();
    }

    bool AsrcWorker::start()
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);

        // Thread sebelumnya harus sudah selesai dan di-join.
        if (thread_.joinable())
            return false;

        if (running_.load())
            return false;

        if (!processor_.valid())
        {
            last_error_.store(-1);
            return false;
        }

        drain_requested_.store(false);
        last_error_.store(0);

        // Reset hanya setelah thread sebelumnya selesai.
        processor_.reset();
        ratio_controller_.reset();

        telemetry_ratio_.store(1.0);
        telemetry_correction_ppm_.store(0.0);
        telemetry_filtered_occupancy_.store(0.0);

        telemetry_min_occupancy_.store(0);
        telemetry_max_occupancy_.store(0);
        telemetry_average_occupancy_.store(0.0);

        telemetry_controller_updates_.store(0);

        pending_input_frames_ = 0;
        pending_output_frames_ = 0;

        try
        {
            thread_ = std::thread(
                &AsrcWorker::processingLoop,
                this);
        }
        catch (...)
        {
            running_.store(false);
            last_error_.store(-2);
            return false;
        }

        // Status aktif ditetapkan setelah thread berhasil dibuat.
        running_.store(true);

        return true;
    }

    void AsrcWorker::stop()
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);

        if (thread_.joinable())
        {
            drain_requested_.store(true);
            thread_.join();
        }

        running_.store(false);
    }

    bool AsrcWorker::isRunning() const noexcept
    {
        return running_.load();
    }

    std::uint64_t AsrcWorker::framesConsumed() const noexcept
    {
        return frames_consumed_.load();
    }

    std::uint64_t AsrcWorker::framesGenerated() const noexcept
    {
        return frames_generated_.load();
    }

    AsrcTelemetry AsrcWorker::telemetry() const noexcept
    {
        AsrcTelemetry result;

        result.ratio =
            telemetry_ratio_.load();

        result.correction_ppm =
            telemetry_correction_ppm_.load();

        result.filtered_occupancy =
            telemetry_filtered_occupancy_.load();

        result.min_occupancy =
            telemetry_min_occupancy_.load();

        result.max_occupancy =
            telemetry_max_occupancy_.load();

        result.average_occupancy =
            telemetry_average_occupancy_.load();

        result.controller_updates =
            telemetry_controller_updates_.load();

        return result;
    }

    int AsrcWorker::lastError() const noexcept
    {
        return last_error_.load();
    }

    void AsrcWorker::processingLoop()
    {
        double asrc_ratio = ratio_controller_.ratio();

        std::size_t occupancy_min =
            static_cast<std::size_t>(-1);

        std::size_t occupancy_max = 0;

        double occupancy_average = 0.0;

        std::uint64_t occupancy_samples = 0;

        constexpr auto CONTROLLER_INTERVAL =
            std::chrono::milliseconds(10);

        auto last_controller_update =
            std::chrono::steady_clock::now();

        bool input_primed = false;
        bool draining = false;
        auto drain_deadline =
            std::chrono::steady_clock::time_point{};

        while (true)
        {
            // 1. Mulai drain ketika menerima permintaan stop.
            if (!draining && drain_requested_.load())
            {
                draining = true;

                drain_deadline =
                    std::chrono::steady_clock::now() +
                    DRAIN_TIMEOUT;
            }

            // 2. Timeout hanya berlaku selama drain.
            if (draining &&
                std::chrono::steady_clock::now() >= drain_deadline)
            {
                last_error_.store(-4);
                break;
            }

            // 3. Perbarui rasio setiap 20 ms selama operasi normal.
            //    Rasio dibekukan ketika proses drain dimulai.
            const auto controller_now =
                std::chrono::steady_clock::now();

            if (!draining &&
                !drain_requested_.load() &&
                input_primed &&
                controller_now - last_controller_update >=
                    CONTROLLER_INTERVAL)
            {
                const double elapsed_seconds =
                    std::chrono::duration<double>(
                        controller_now - last_controller_update)
                        .count();

                // Feedback berdasarkan antrean output ASRC menuju JACK.
                // Sertakan output yang masih tertunda di staging buffer.
                const std::size_t output_occupancy =
                    output_buffer_.available() +
                    pending_output_frames_;

                asrc_ratio = ratio_controller_.update(
                    output_occupancy,
                    elapsed_seconds);

                // Statistik occupancy selama operasi normal.
                occupancy_min = std::min(
                    occupancy_min,
                    output_occupancy);

                occupancy_max = std::max(
                    occupancy_max,
                    output_occupancy);

                ++occupancy_samples;

                occupancy_average +=
                    (static_cast<double>(output_occupancy) -
                     occupancy_average) /
                    static_cast<double>(occupancy_samples);
                    
                // Publikasikan telemetry untuk thread pembaca.
                telemetry_ratio_.store(asrc_ratio);

                telemetry_correction_ppm_.store(
                    ratio_controller_.correctionPpm());

                telemetry_filtered_occupancy_.store(
                    ratio_controller_.filteredOccupancy());

                telemetry_min_occupancy_.store(
                    occupancy_min);

                telemetry_max_occupancy_.store(
                    occupancy_max);

                telemetry_average_occupancy_.store(
                    occupancy_average);

                telemetry_controller_updates_.store(
                    occupancy_samples);

                last_controller_update = controller_now;
            }

            // 4. Kirim output tertunda terlebih dahulu.
            if (pending_output_frames_ > 0)
            {
                const std::size_t written =
                    output_buffer_.write(
                        output_staging_.data(),
                        pending_output_frames_);

                if (written > 0)
                {
                    pending_output_frames_ -= written;

                    if (pending_output_frames_ > 0)
                    {
                        std::move(
                            output_staging_.begin() + written,
                            output_staging_.begin() +
                                written + pending_output_frames_,
                            output_staging_.begin());
                    }

                    frames_generated_.fetch_add(written);
                }

                if (pending_output_frames_ > 0)
                {
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(500));

                    continue;
                }
            }

            // 5. Ambil input yang masih tersedia.
            if (pending_input_frames_ < BLOCK_SIZE)
            {
                const std::size_t free_space =
                    BLOCK_SIZE - pending_input_frames_;

                const std::size_t available =
                    input_buffer_.available();

                const std::size_t to_read =
                    std::min(free_space, available);

                if (to_read > 0)
                {
                    const std::size_t received =
                        input_buffer_.read(
                            input_staging_.data() +
                                pending_input_frames_,
                            to_read);

                    pending_input_frames_ += received;
                }
            }

            // 6. Prefill awal agar controller memiliki cadangan input.
            //    Drain tidak boleh tertahan oleh persyaratan prefill.
            if (draining)
            {
                input_primed = true;
            }
            else if (!input_primed)
            {
                const std::size_t total_input_occupancy =
                    input_buffer_.available() +
                    pending_input_frames_;

                if (total_input_occupancy <
                    INPUT_TARGET_OCCUPANCY)
                {
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(500));

                    continue;
                }

                input_primed = true;
            }

            // 7. Jika sedang drain dan seluruh input habis,
            //    lakukan flush libsamplerate.
            if (draining &&
                pending_input_frames_ == 0 &&
                input_buffer_.available() == 0)
            {
                std::size_t input_used = 0;
                std::size_t output_generated = 0;

                const bool result = processor_.process(
                    nullptr,
                    0,
                    output_staging_.data(),
                    output_staging_.size(),
                    asrc_ratio,
                    input_used,
                    output_generated,
                    true);

                if (!result || input_used != 0)
                {
                    last_error_.store(-3);
                    break;
                }

                pending_output_frames_ = output_generated;

                // Tidak ada output baru berarti flush selesai.
                if (output_generated == 0)
                    break;

                continue;
            }

            // 8. Belum ada input.
            if (pending_input_frames_ == 0)
            {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(500));

                continue;
            }

            // 9. Proses input.
            std::size_t input_used = 0;
            std::size_t output_generated = 0;

            // end_of_input hanya true jika:
            // - worker sedang drain
            // - tidak ada input lagi di ring buffer
            const bool final_input =
                draining &&
                input_buffer_.available() == 0;

            const bool result = processor_.process(
                input_staging_.data(),
                pending_input_frames_,
                output_staging_.data(),
                output_staging_.size(),
                asrc_ratio,
                input_used,
                output_generated,
                final_input);

            if (!result)
            {
                last_error_.store(-3);
                break;
            }

            // 10. Pertahankan input yang belum dikonsumsi.
            if (input_used > 0)
            {
                pending_input_frames_ -= input_used;

                if (pending_input_frames_ > 0)
                {
                    std::move(
                        input_staging_.begin() + input_used,
                        input_staging_.begin() +
                            input_used + pending_input_frames_,
                        input_staging_.begin());
                }

                frames_consumed_.fetch_add(input_used);
            }

            // 11. Pertahankan output yang dihasilkan.
            pending_output_frames_ = output_generated;

            if (input_used == 0 &&
                output_generated == 0)
            {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(500));
            }
        }

        running_.store(false);
    }

}
