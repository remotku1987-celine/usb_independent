
#include <alsa/asoundlib.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{

    constexpr unsigned SAMPLE_RATE = 48000;
    constexpr snd_pcm_uframes_t PERIOD_SIZE = 128;
    constexpr snd_pcm_uframes_t BUFFER_SIZE = 512;
    constexpr int TEST_SECONDS = 30;

    std::atomic<bool> stop_requested{false};

    struct StartGate
    {
        std::mutex mutex;
        std::condition_variable cv;
        int ready = 0;
        bool start = false;
    };

    struct Device
    {
        std::string name;
        std::string pcm_name;
        snd_pcm_t *pcm = nullptr;
        uint64_t captured_frames = 0;
        int error = 0;
    };

    void signal_handler(int)
    {
        stop_requested.store(true);
    }

    bool configure_device(Device &device)
    {
        snd_pcm_hw_params_t *params;
        snd_pcm_hw_params_alloca(&params);

        int rc = snd_pcm_open(
            &device.pcm,
            device.pcm_name.c_str(),
            SND_PCM_STREAM_CAPTURE,
            0);

        if (rc < 0)
        {
            std::cerr << device.name << ": open gagal: "
                      << snd_strerror(rc) << '\n';
            return false;
        }

        unsigned rate = SAMPLE_RATE;
        snd_pcm_uframes_t period = PERIOD_SIZE;
        snd_pcm_uframes_t buffer = BUFFER_SIZE;

        rc = snd_pcm_hw_params_any(device.pcm, params);

        if (rc < 0)
            goto fail;

        rc = snd_pcm_hw_params_set_access(
            device.pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
        if (rc < 0)
            goto fail;

        rc = snd_pcm_hw_params_set_format(
            device.pcm, params, SND_PCM_FORMAT_S16_LE);
        if (rc < 0)
            goto fail;

        rc = snd_pcm_hw_params_set_channels(device.pcm, params, 1);
        if (rc < 0)
            goto fail;

        rc = snd_pcm_hw_params_set_rate_near(
            device.pcm, params, &rate, nullptr);
        if (rc < 0)
            goto fail;

        if (rate != SAMPLE_RATE)
        {
            rc = -EINVAL;
            goto fail;
        }

        rc = snd_pcm_hw_params_set_period_size_near(
            device.pcm, params, &period, nullptr);
        if (rc < 0)
            goto fail;

        rc = snd_pcm_hw_params_set_buffer_size_near(
            device.pcm, params, &buffer);
        if (rc < 0)
            goto fail;

        rc = snd_pcm_hw_params(device.pcm, params);
        if (rc < 0)
            goto fail;

        std::cout << device.name
                  << ": siap, " << rate << " Hz"
                  << ", period=" << period
                  << ", buffer=" << buffer << '\n';

        return true;

    fail:
        std::cerr << device.name << ": konfigurasi gagal: "
                  << snd_strerror(rc) << '\n';
        snd_pcm_close(device.pcm);
        device.pcm = nullptr;
        return false;
    }

    void capture_worker(Device &device, StartGate &gate)
    {
        std::vector<int16_t> samples(PERIOD_SIZE);

        {
            std::unique_lock<std::mutex> lock(gate.mutex);
            ++gate.ready;
            gate.cv.notify_all();

            gate.cv.wait(lock, [&]
                         { return gate.start; });
        }

        // Start dilakukan sedekat mungkin setelah sinyal bersama.
        int rc = snd_pcm_start(device.pcm);
        if (rc < 0)
        {
            device.error = rc;
            std::cerr << device.name << ": start gagal: "
                      << snd_strerror(rc) << '\n';
            return;
        }

        while (!stop_requested.load())
        {
            snd_pcm_sframes_t frames = snd_pcm_readi(
                device.pcm,
                samples.data(),
                PERIOD_SIZE);

            if (frames == -EINTR)
                continue;

            if (frames < 0)
            {
                rc = snd_pcm_recover(device.pcm, frames, 1);
                if (rc < 0)
                {
                    device.error = rc;
                    std::cerr << device.name << ": capture error: "
                              << snd_strerror(rc) << '\n';
                    break;
                }
                continue;
            }

            device.captured_frames += static_cast<uint64_t>(frames);
        }
    }

} // namespace

int main()
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::array<Device, 2> devices{{{"CM108_INPUT_02", "hw:2,0"},
                                   {"CM108_INPUT_01", "hw:3,0"}}};

    std::cout << "CM108 USB SIMULTANEOUS CAPTURE TEST\n"
              << "===================================\n";

    for (auto &device : devices)
    {
        if (!configure_device(device))
        {
            for (auto &d : devices)
                if (d.pcm)
                    snd_pcm_close(d.pcm);
            return 1;
        }
    }

    StartGate gate;
    std::array<std::thread, 2> workers;

    for (size_t i = 0; i < devices.size(); ++i)
    {
        workers[i] = std::thread(
            capture_worker,
            std::ref(devices[i]),
            std::ref(gate));
    }

    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        gate.cv.wait(lock, [&]
                     { return gate.ready == static_cast<int>(devices.size()); });

        std::cout << "\nKedua perangkat siap.\n";
        std::cout << "START dalam 3 detik...\n";
        lock.unlock();

        std::this_thread::sleep_for(std::chrono::seconds(3));

        lock.lock();
        gate.start = true;
        gate.cv.notify_all();
    }

    std::cout << "CAPTURE DIMULAI. Amati LED kedua CM108.\n";
    std::cout << "Durasi pengujian: " << TEST_SECONDS << " detik.\n";

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(TEST_SECONDS);

    while (!stop_requested.load() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    stop_requested.store(true);

    for (auto &worker : workers)
        worker.join();

    std::cout << "\nHASIL PENGUJIAN\n"
              << "===============\n";

    for (auto &device : devices)
    {
        std::cout << device.name << '\n'
                  << "Captured frames: " << device.captured_frames << '\n'
                  << "Captured seconds: "
                  << static_cast<double>(device.captured_frames) / SAMPLE_RATE << '\n'
                  << "Error: "
                  << (device.error == 0
                          ? "tidak ada"
                          : snd_strerror(device.error))
                  << "\n\n";

        snd_pcm_drop(device.pcm);
        snd_pcm_close(device.pcm);
    }

    return 0;
}
