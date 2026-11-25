
#pragma once

#include <samplerate.h>

#include <cstddef>
#include <memory>

namespace usb_independent
{

    class AsrcProcessor
    {
    public:
        explicit AsrcProcessor(
            int converter_type = SRC_SINC_FASTEST);

        ~AsrcProcessor();

        AsrcProcessor(const AsrcProcessor &) = delete;
        AsrcProcessor &operator=(const AsrcProcessor &) = delete;

        bool valid() const noexcept;

        bool process(
            const float *input,
            std::size_t input_frames,
            float *output,
            std::size_t output_capacity,
            double ratio,
            std::size_t &input_used,
            std::size_t &output_generated,
            bool end_of_input = false);

        void reset() noexcept;

    private:
        SRC_STATE *state_ = nullptr;
        int error_ = 0;
    };

}
