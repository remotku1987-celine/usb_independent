#pragma once

#include <cstddef>

namespace usb_independent
{
    class AsrcRatioController
    {
    public:
        explicit AsrcRatioController(
            std::size_t target_occupancy = 512) noexcept;

        AsrcRatioController(const AsrcRatioController &) = delete;
        AsrcRatioController &operator=(const AsrcRatioController &) = delete;

        double update(
            std::size_t input_occupancy,
            double elapsed_seconds) noexcept;

        void reset() noexcept;

        double ratio() const noexcept;
        double correctionPpm() const noexcept;
        double filteredOccupancy() const noexcept;

    private:
        static constexpr double KP_PPM_PER_FRAME = 0.5;
        static constexpr double KI_PPM_PER_FRAME_SECOND = 0.05;
        static constexpr double MAX_CORRECTION_PPM = 1000.0;
        static constexpr double MAX_SLEW_PPM = 10.0;
        static constexpr double FILTER_TIME_CONSTANT_SECONDS = 0.2;

        std::size_t target_occupancy_;

        double filtered_occupancy_ = 0.0;
        double integral_error_ = 0.0;
        double correction_ppm_ = 0.0;

        bool initialized_ = false;
    };
}
