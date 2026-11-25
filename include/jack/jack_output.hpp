#pragma once

#include "buffer/spsc_ring_buffer.hpp"

#include <jack/jack.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace usb_independent
{

    class JackOutput
    {
    public:
        JackOutput() = default;
        ~JackOutput();

        JackOutput(const JackOutput &) = delete;
        JackOutput &operator=(const JackOutput &) = delete;

        bool open(const std::string &client_name);

        bool addOutputPort(
            const std::string &port_name,
            AudioRingBuffer &ring_buffer);

        bool activate();

        void close() noexcept;

        bool isActive() const noexcept;
        bool serverShutdown() const noexcept;

        std::size_t portCount() const noexcept;
        std::uint64_t framesConsumed() const noexcept;

    private:
        struct OutputPort
        {
            jack_port_t *port = nullptr;
            AudioRingBuffer *ring_buffer = nullptr;
        };

        static int processCallback(
            jack_nframes_t nframes,
            void *arg) noexcept;

        static void shutdownCallback(void *arg) noexcept;

        int process(jack_nframes_t nframes) noexcept;

        jack_client_t *client_ = nullptr;

        // Daftar port dibuat sebelum JACK diaktifkan.
        // Tidak boleh dimodifikasi selama callback berjalan.
        std::vector<OutputPort> ports_;

        std::atomic<bool> active_{false};
        std::atomic<bool> server_shutdown_{false};
        std::atomic<std::uint64_t> frames_consumed_{0};
    };

}
