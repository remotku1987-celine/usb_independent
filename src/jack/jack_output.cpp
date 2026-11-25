#include "jack/jack_output.hpp"

#include <algorithm>
#include <cstring>
#include <jack/types.h>

namespace usb_independent
{

    JackOutput::~JackOutput()
    {
        close();
    }

    bool JackOutput::open(const std::string &client_name)
    {
        if (client_ != nullptr || client_name.empty())
            return false;

        jack_status_t status{};

        client_ = jack_client_open(
            client_name.c_str(),
            JackNullOption,
            &status);

        if (client_ == nullptr)
            return false;

        server_shutdown_.store(false);

        if (jack_set_process_callback(
                client_,
                &JackOutput::processCallback,
                this) != 0)
        {
            close();
            return false;
        }

        jack_on_shutdown(
            client_,
            &JackOutput::shutdownCallback,
            this);

        return true;
    }

    bool JackOutput::addOutputPort(
        const std::string &port_name,
        AudioRingBuffer &ring_buffer)
    {
        if (client_ == nullptr ||
            active_.load() ||
            port_name.empty())
        {
            return false;
        }

        jack_port_t *port = jack_port_register(
            client_,
            port_name.c_str(),
            JACK_DEFAULT_AUDIO_TYPE,
            JackPortIsOutput,
            0);

        if (port == nullptr)
            return false;

        try
        {
            ports_.push_back({port, &ring_buffer});
        }
        catch (...)
        {
            jack_port_unregister(client_, port);
            return false;
        }

        return true;
    }

    bool JackOutput::activate()
    {
        if (client_ == nullptr ||
            active_.load() ||
            ports_.empty())
        {
            return false;
        }

        if (jack_activate(client_) != 0)
            return false;

        active_.store(true);

        return true;
    }

    void JackOutput::close() noexcept
    {
        if (client_ == nullptr)
            return;

        if (active_.exchange(false))
            jack_deactivate(client_);

        ports_.clear();

        jack_client_close(client_);
        client_ = nullptr;
    }

    bool JackOutput::isActive() const noexcept
    {
        return active_.load();
    }

    bool JackOutput::serverShutdown() const noexcept
    {
        return server_shutdown_.load();
    }

    std::size_t JackOutput::portCount() const noexcept
    {
        return ports_.size();
    }

    std::uint64_t JackOutput::framesConsumed() const noexcept
    {
        return frames_consumed_.load(std::memory_order_relaxed);
    }

    int JackOutput::processCallback(
        jack_nframes_t nframes,
        void *arg) noexcept
    {
        auto *self = static_cast<JackOutput *>(arg);

        return self->process(nframes);
    }

    void JackOutput::shutdownCallback(void *arg) noexcept
    {
        auto *self = static_cast<JackOutput *>(arg);

        self->server_shutdown_.store(true);
        self->active_.store(false);
    }

    int JackOutput::process(jack_nframes_t nframes) noexcept
    {
        const std::size_t frame_count =
            static_cast<std::size_t>(nframes);

        for (auto &entry : ports_)
        {
            auto *output = static_cast<float *>(
                jack_port_get_buffer(entry.port, nframes));

            if (output == nullptr)
                continue;

            const std::size_t received =
                entry.ring_buffer->read(
                    output,
                    frame_count);

            frames_consumed_.fetch_add(
                received,
                std::memory_order_relaxed);

            // JACK selalu menerima seluruh cycle.
            // Jika ring buffer kekurangan data, sisanya silence.
            if (received < frame_count)
            {
                std::fill(
                    output + received,
                    output + frame_count,
                    0.0f);
            }
        }

        return 0;
    }

}
