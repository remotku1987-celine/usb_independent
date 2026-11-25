
#include "asrc/asrc_processor.hpp"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

using namespace usb_independent;

int main()
{
    constexpr std::size_t INPUT_FRAMES = 4096;
    constexpr std::size_t OUTPUT_CAPACITY = 8192;

    std::vector<float> input(INPUT_FRAMES);
    std::vector<float> output_a(OUTPUT_CAPACITY);
    std::vector<float> output_b(OUTPUT_CAPACITY);

    for (std::size_t i = 0; i < INPUT_FRAMES; ++i)
    {
        input[i] = static_cast<float>(
            0.5 * std::sin(
                      2.0 * 3.141592653589793 *
                      1000.0 * i / 48000.0));
    }

    std::cout << "ASRC UNIT TEST\n";
    std::cout << "------------------------------\n";

    // Test 1: Create independent ASRC instances.
    AsrcProcessor asrc_a;
    AsrcProcessor asrc_b;

    if (!asrc_a.valid() || !asrc_b.valid())
    {
        std::cerr << "FAIL: ASRC initialization\n";
        return 1;
    }

    std::cout << "PASS: ASRC initialization\n";

    // Test 2: Process with different ratios.
    std::size_t input_used_a = 0;
    std::size_t output_generated_a = 0;

    std::size_t input_used_b = 0;
    std::size_t output_generated_b = 0;

    const bool result_a = asrc_a.process(
        input.data(),
        input.size(),
        output_a.data(),
        output_a.size(),
        1.05,
        input_used_a,
        output_generated_a);

    const bool result_b = asrc_b.process(
        input.data(),
        input.size(),
        output_b.data(),
        output_b.size(),
        0.95,
        input_used_b,
        output_generated_b);

    if (!result_a || !result_b)
    {
        std::cerr << "FAIL: ASRC processing\n";
        return 1;
    }

    if (input_used_a == 0 ||
        input_used_b == 0 ||
        output_generated_a == 0 ||
        output_generated_b == 0)
    {
        std::cerr << "FAIL: No audio frames processed\n";
        return 1;
    }

    std::cout << "PASS: ASRC processing\n";

    // Test 3: Verify independent conversion ratios.
    if (output_generated_a <= output_generated_b)
    {
        std::cerr << "FAIL: Independent ratios\n";
        return 1;
    }

    std::cout << "PASS: Independent ASRC ratios\n";

    // Test 4: Verify output contains finite samples.
    for (std::size_t i = 0; i < output_generated_a; ++i)
    {
        if (!std::isfinite(output_a[i]))
        {
            std::cerr << "FAIL: Invalid output A\n";
            return 1;
        }
    }

    for (std::size_t i = 0; i < output_generated_b; ++i)
    {
        if (!std::isfinite(output_b[i]))
        {
            std::cerr << "FAIL: Invalid output B\n";
            return 1;
        }
    }

    std::cout << "PASS: Finite audio output\n";

    // Test 5: Invalid ratio must be rejected.
    std::size_t invalid_input_used = 100;
    std::size_t invalid_output_generated = 100;

    const bool invalid_result = asrc_a.process(
        input.data(),
        input.size(),
        output_a.data(),
        output_a.size(),
        0.0,
        invalid_input_used,
        invalid_output_generated);

    if (invalid_result ||
        invalid_input_used != 0 ||
        invalid_output_generated != 0)
    {
        std::cerr << "FAIL: Invalid ratio validation\n";
        return 1;
    }

    std::cout << "PASS: Invalid ratio validation\n";

    // Test 6: Reset ASRC state.
    asrc_a.reset();
    asrc_b.reset();

    if (!asrc_a.valid() || !asrc_b.valid())
    {
        std::cerr << "FAIL: ASRC reset\n";
        return 1;
    }

    std::cout << "PASS: ASRC reset\n";

    // Test 7: Continuous streaming with 256-frame blocks.
    AsrcProcessor streaming_asrc;

    constexpr std::size_t BLOCK_SIZE = 256;
    constexpr std::size_t STREAM_BLOCKS = 100;
    constexpr std::size_t STREAM_OUTPUT_CAPACITY = 1024;

    std::vector<float> stream_input(BLOCK_SIZE);
    std::vector<float> stream_output(STREAM_OUTPUT_CAPACITY);

    std::size_t total_input_used = 0;
    std::size_t total_output_generated = 0;

    bool streaming_ok = true;

    for (std::size_t block = 0;
         block < STREAM_BLOCKS && streaming_ok;
         ++block)
    {
        for (std::size_t i = 0; i < BLOCK_SIZE; ++i)
        {
            const std::size_t sample_index =
                block * BLOCK_SIZE + i;

            stream_input[i] = static_cast<float>(
                0.5 * std::sin(
                          2.0 * 3.141592653589793 *
                          1000.0 * sample_index / 48000.0));
        }

        std::size_t block_input_offset = 0;

        while (block_input_offset < BLOCK_SIZE)
        {
            std::size_t input_used = 0;
            std::size_t output_generated = 0;

            const bool result = streaming_asrc.process(
                stream_input.data() + block_input_offset,
                BLOCK_SIZE - block_input_offset,
                stream_output.data(),
                stream_output.size(),
                1.0,
                input_used,
                output_generated);

            if (!result ||
                (input_used == 0 && output_generated == 0))
            {
                streaming_ok = false;
                break;
            }

            block_input_offset += input_used;

            total_input_used += input_used;
            total_output_generated += output_generated;

            for (std::size_t i = 0;
                 i < output_generated;
                 ++i)
            {
                if (!std::isfinite(stream_output[i]))
                {
                    streaming_ok = false;
                    break;
                }
            }
        }
    }

    if (!streaming_ok ||
        total_input_used != BLOCK_SIZE * STREAM_BLOCKS ||
        total_output_generated == 0)
    {
        std::cerr << "FAIL: ASRC streaming\n";
        return 1;
    }

    std::cout << "PASS: ASRC 256-frame streaming\n";

    std::cout << "Streaming input consumed: "
              << total_input_used << "\n";

    std::cout << "Streaming output generated: "
              << total_output_generated << "\n";

    // Test 8: Two independent ASRC streaming instances.
    AsrcProcessor independent_a;
    AsrcProcessor independent_b;

    std::size_t total_output_a = 0;
    std::size_t total_output_b = 0;

    bool independent_ok = true;

    for (std::size_t block = 0;
         block < STREAM_BLOCKS && independent_ok;
         ++block)
    {
        for (std::size_t i = 0; i < BLOCK_SIZE; ++i)
        {
            const std::size_t sample_index =
                block * BLOCK_SIZE + i;

            stream_input[i] = static_cast<float>(
                0.5 * std::sin(
                          2.0 * 3.141592653589793 *
                          1000.0 * sample_index / 48000.0));
        }

        std::size_t offset_a = 0;
        std::size_t offset_b = 0;

        while (offset_a < BLOCK_SIZE)
        {
            std::size_t used = 0;
            std::size_t generated = 0;

            if (!independent_a.process(
                    stream_input.data() + offset_a,
                    BLOCK_SIZE - offset_a,
                    stream_output.data(),
                    stream_output.size(),
                    1.01,
                    used,
                    generated) ||
                (used == 0 && generated == 0))
            {
                independent_ok = false;
                break;
            }

            offset_a += used;
            total_output_a += generated;
        }

        while (offset_b < BLOCK_SIZE && independent_ok)
        {
            std::size_t used = 0;
            std::size_t generated = 0;

            if (!independent_b.process(
                    stream_input.data() + offset_b,
                    BLOCK_SIZE - offset_b,
                    stream_output.data(),
                    stream_output.size(),
                    0.99,
                    used,
                    generated) ||
                (used == 0 && generated == 0))
            {
                independent_ok = false;
                break;
            }

            offset_b += used;
            total_output_b += generated;
        }
    }

    if (!independent_ok ||
        total_output_a <= total_output_b)
    {
        std::cerr << "FAIL: Independent streaming ratios\n";
        return 1;
    }

    std::cout << "PASS: Independent ASRC streaming ratios\n";

    std::cout << "ASRC A output (ratio 1.01): "
              << total_output_a << "\n";

    std::cout << "ASRC B output (ratio 0.99): "
              << total_output_b << "\n";

    // Test 9: Flush the final ASRC output.
    AsrcProcessor flush_asrc;

    std::vector<float> flush_input(BLOCK_SIZE, 0.25f);
    std::vector<float> flush_output(STREAM_OUTPUT_CAPACITY);

    std::size_t flush_input_used = 0;
    std::size_t flush_output_generated = 0;

    const bool final_input_ok = flush_asrc.process(
        flush_input.data(),
        flush_input.size(),
        flush_output.data(),
        flush_output.size(),
        1.0,
        flush_input_used,
        flush_output_generated,
        true);

    if (!final_input_ok ||
        flush_input_used != BLOCK_SIZE)
    {
        std::cerr << "FAIL: Final ASRC input\n";
        return 1;
    }

    std::size_t total_flush_output = flush_output_generated;
    bool flush_ok = true;
    bool flush_completed = false;

    constexpr std::size_t MAX_FLUSH_CALLS = 100;

    for (std::size_t attempt = 0;
         attempt < MAX_FLUSH_CALLS;
         ++attempt)
    {
        std::size_t input_used = 0;
        std::size_t output_generated = 0;

        const bool result = flush_asrc.process(
            nullptr,
            0,
            flush_output.data(),
            flush_output.size(),
            1.0,
            input_used,
            output_generated,
            true);

        if (!result || input_used != 0)
        {
            flush_ok = false;
            break;
        }

        total_flush_output += output_generated;

        if (output_generated == 0)
        {
            flush_completed = true;
            break;
        }
    }

    if (!flush_ok || !flush_completed ||
        total_flush_output == 0)
    {
        std::cerr << "FAIL: ASRC flush\n";
        return 1;
    }

    std::cout << "PASS: ASRC final input accepted\n";
    std::cout << "PASS: ASRC flush completed\n";
    std::cout << "Total output including flush: "
              << total_flush_output << "\n";

    std::cout << "\nALL ASRC TESTS PASSED\n";

    return 0;
}
