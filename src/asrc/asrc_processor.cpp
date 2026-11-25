
#include "asrc/asrc_processor.hpp"

#include <cmath>
#include <limits>

namespace usb_independent
{

    AsrcProcessor::AsrcProcessor(int converter_type)
    {
        state_ = src_new(
            converter_type,
            1,
            &error_);
    }

    AsrcProcessor::~AsrcProcessor()
    {
        if (state_ != nullptr)
        {
            src_delete(state_);
            state_ = nullptr;
        }
    }

    bool AsrcProcessor::valid() const noexcept
    {
        return state_ != nullptr;
    }

    bool AsrcProcessor::process(
        const float *input,
        std::size_t input_frames,
        float *output,
        std::size_t output_capacity,
        double ratio,
        std::size_t &input_used,
        std::size_t &output_generated,
        bool end_of_input)

    {
        input_used = 0;
        output_generated = 0;

        if (state_ == nullptr)
            return false;

        if (output == nullptr)
            return false;

        if (input_frames > 0 && input == nullptr)
            return false;

        if (input_frames == 0 && !end_of_input)
            return false;

        if (output_capacity == 0)
            return false;

        if (!std::isfinite(ratio) ||
            ratio < (1.0 / 256.0) ||
            ratio > 256.0)
        {
            return false;
        }

        if (input_frames >
                static_cast<std::size_t>(
                    std::numeric_limits<long>::max()) ||
            output_capacity >
                static_cast<std::size_t>(
                    std::numeric_limits<long>::max()))
        {
            return false;
        }

        SRC_DATA data{};

        static float empty_input = 0.0f;

        data.data_in = input_frames > 0
                           ? const_cast<float *>(input)
                           : &empty_input;

        data.data_out = output;

        data.input_frames =
            static_cast<long>(input_frames);

        data.output_frames =
            static_cast<long>(output_capacity);

        data.src_ratio = ratio;
        data.end_of_input = end_of_input ? 1 : 0;

        error_ = src_process(state_, &data);

        if (error_ != 0)
            return false;

        input_used =
            static_cast<std::size_t>(
                data.input_frames_used);

        output_generated =
            static_cast<std::size_t>(
                data.output_frames_gen);

        return true;
    }

    void AsrcProcessor::reset() noexcept
    {
        if (state_ != nullptr)
        {
            error_ = src_reset(state_);
        }
    }

}
