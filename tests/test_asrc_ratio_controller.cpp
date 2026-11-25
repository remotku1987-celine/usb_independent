#include "asrc/asrc_ratio_controller.hpp"

#include <cmath>
#include <iostream>

using usb_independent::AsrcRatioController;

namespace
{
    bool check(bool condition, const char *message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            return false;
        }

        std::cout << "PASS: " << message << '\n';
        return true;
    }
}

int main()
{
    bool passed = true;

    // 1. Rasio awal harus nominal.
    AsrcRatioController initial(512);

    passed &= check(
        std::abs(initial.ratio() - 1.0) < 1e-12,
        "Initial ratio is 1.0");

    // 2. Occupancy tinggi harus menurunkan rasio.
    AsrcRatioController high(512);

    for (int i = 0; i < 300; ++i)
        high.update(2048, 0.020);

    passed &= check(
        high.ratio() < 1.0,
        "High occupancy decreases ASRC ratio");

    passed &= check(
        high.correctionPpm() > 0.0,
        "High occupancy produces positive correction");

    // 3. Occupancy rendah harus menaikkan rasio.
    AsrcRatioController low(512);

    for (int i = 0; i < 300; ++i)
        low.update(0, 0.020);

    passed &= check(
        low.ratio() > 1.0,
        "Low occupancy increases ASRC ratio");

    passed &= check(
        low.correctionPpm() < 0.0,
        "Low occupancy produces negative correction");

    // 4. Slew maksimum 10 ppm per pembaruan.
    AsrcRatioController slew(512);

    slew.update(8192, 0.020);

    passed &= check(
        std::abs(slew.correctionPpm()) <= 10.0 + 1e-9,
        "Correction slew is limited to 10 ppm per update");

    // 5. Batas koreksi maksimum ±1000 ppm.
    AsrcRatioController upper_limit(512);
    AsrcRatioController lower_limit(512);

    for (int i = 0; i < 20000; ++i)
    {
        upper_limit.update(8192, 0.020);
        lower_limit.update(0, 0.020);
    }

    passed &= check(
        upper_limit.correctionPpm() <= 1000.0,
        "Positive correction does not exceed 1000 ppm");

    passed &= check(
        lower_limit.correctionPpm() >= -1000.0,
        "Negative correction does not exceed -1000 ppm");

    // 6. Dua controller tidak boleh berbagi state.
    AsrcRatioController independent_a(512);
    AsrcRatioController independent_b(512);

    for (int i = 0; i < 100; ++i)
        independent_a.update(2048, 0.020);

    passed &= check(
        std::abs(independent_b.correctionPpm()) < 1e-12,
        "Controller instances have independent state");

    // 7. Reset harus mengembalikan rasio nominal.
    high.reset();

    passed &= check(
        std::abs(high.ratio() - 1.0) < 1e-12 &&
        std::abs(high.correctionPpm()) < 1e-12,
        "Reset restores nominal ratio");

    // 8. Delta waktu tidak valid tidak boleh mengubah state.
    const double before = low.correctionPpm();

    low.update(4096, 0.0);

    passed &= check(
        std::abs(low.correctionPpm() - before) < 1e-12,
        "Invalid elapsed time does not change controller state");

    std::cout << '\n';

    if (!passed)
    {
        std::cerr << "ASRC ratio controller tests FAILED\n";
        return 1;
    }

    std::cout << "ASRC ratio controller tests PASSED\n";
    return 0;
}
