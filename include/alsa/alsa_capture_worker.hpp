
#pragma once

#include <alsa/asoundlib.h>
#include "alsa/alsa_device.hpp"
#include "buffer/spsc_ring_buffer.hpp"
#include <atomic>
#include <cstdint>
#include <thread>
#include <string>
#include <vector>
#include <condition_variable>
#include <mutex>

namespace usb_independent
{

    class AlsaCaptureWorker
    {
    public:
        AlsaCaptureWorker(
            std::string id_path,
            int card_index,
            int pcm_device,
            std::string name,
            AudioRingBuffer &ring_buffer);

        ~AlsaCaptureWorker();

        AlsaCaptureWorker(const AlsaCaptureWorker &) = delete;
        AlsaCaptureWorker &operator=(const AlsaCaptureWorker &) = delete;

        bool start();
        void stop();

        bool isRunning() const;
        uint64_t framesCaptured() const;
        int lastError() const;

    private:
        int configurePcm();
        void captureLoop();

        bool resolveDevice();
        bool openPcm();

        AlsaDeviceDetector detector_;
        // Stable USB identity
        std::string id_path_;
        // Current ALSA device address
        int card_index_;
        int pcm_device_;
        AudioRingBuffer &ring_buffer_;

        std::mutex wait_mutex_;
        std::condition_variable wait_condition_;

        std::string name_;

        snd_pcm_t *pcm_ = nullptr;
        snd_pcm_uframes_t period_size_ = 128;

        std::thread thread_;

        std::atomic<bool> stop_requested_{false};
        std::atomic<bool> running_{false};

        std::atomic<uint64_t> frames_captured_{0};
        std::atomic<int> last_error_{0};
    };

}
