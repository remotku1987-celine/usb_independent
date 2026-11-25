#include "asrc/asrc_ratio_controller.hpp"

#include <algorithm>
#include <cmath>

namespace usb_independent
{
    AsrcRatioController::AsrcRatioController(
        std::size_t target_occupancy) noexcept
        : target_occupancy_(
              target_occupancy == 0 ? 1 : target_occupancy)
    {
    }

    double AsrcRatioController::update(
        std::size_t input_occupancy,
        double elapsed_seconds) noexcept
    {
        if (!std::isfinite(elapsed_seconds) ||
            elapsed_seconds <= 0.0)
        {
            return ratio();
        }

        // Hindari lonjakan integrasi setelah worker terlambat berjalan.
        const double dt = std::min(elapsed_seconds, 0.1);

        if (!initialized_)
        {
            filtered_occupancy_ =
                static_cast<double>(input_occupancy);

            initialized_ = true;
        }
        else
        {
            const double alpha =
                dt / (FILTER_TIME_CONSTANT_SECONDS + dt);

            filtered_occupancy_ +=
                alpha *
                (static_cast<double>(input_occupancy) -
                 filtered_occupancy_);
        }

        // Error positif berarti input buffer terlalu penuh.
        const double error =
            filtered_occupancy_ -
            static_cast<double>(target_occupancy_);

        const double candidate_integral =
            integral_error_ + error * dt;

        double requested_correction =
            KP_PPM_PER_FRAME * error +
            KI_PPM_PER_FRAME_SECOND * candidate_integral;

        // Anti-windup: jangan menambah integral ketika sudah
        // melewati batas dan error mendorong lebih jauh.
        const bool pushing_upper_limit =
            requested_correction > MAX_CORRECTION_PPM &&
            error > 0.0;

        const bool pushing_lower_limit =
            requested_correction < -MAX_CORRECTION_PPM &&
            error < 0.0;

        if (!pushing_upper_limit && !pushing_lower_limit)
        {
            integral_error_ = candidate_integral;
        }

        requested_correction =
            KP_PPM_PER_FRAME * error +
            KI_PPM_PER_FRAME_SECOND * integral_error_;

        requested_correction = std::clamp(
            requested_correction,
            -MAX_CORRECTION_PPM,
            MAX_CORRECTION_PPM);

        // Batasi perubahan koreksi setiap pembaruan.
        const double delta =
            requested_correction - correction_ppm_;

        correction_ppm_ += std::clamp(
            delta,
            -MAX_SLEW_PPM,
            MAX_SLEW_PPM);

        // libsamplerate: ratio = output frames / input frames.
        // Occupancy tinggi -> ratio lebih rendah.
        return ratio();
    }

    void AsrcRatioController::reset() noexcept
    {
        filtered_occupancy_ = 0.0;
        integral_error_ = 0.0;
        correction_ppm_ = 0.0;
        initialized_ = false;
    }

    double AsrcRatioController::ratio() const noexcept
    {
        return 1.0 - correction_ppm_ / 1000000.0;
    }

    double AsrcRatioController::correctionPpm() const noexcept
    {
        return correction_ppm_;
    }

    double AsrcRatioController::filteredOccupancy() const noexcept
    {
        return filtered_occupancy_;
    }
}
