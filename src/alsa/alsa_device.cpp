
#include "alsa/alsa_device.hpp"

#include <alsa/asoundlib.h>
#include <libudev.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

namespace usb_independent
{

    bool AlsaDeviceDetector::hasCaptureDevice(
        int card,
        int &pcm_device)
    {
        snd_ctl_t *ctl = nullptr;

        const std::string name = "hw:" + std::to_string(card);

        if (snd_ctl_open(&ctl, name.c_str(), 0) < 0)
        {
            return false;
        }

        int device = -1;
        bool found = false;

        while (snd_ctl_pcm_next_device(ctl, &device) >= 0 && device >= 0)
        {

            snd_pcm_info_t *info;
            snd_pcm_info_alloca(&info);

            snd_pcm_info_set_device(info, device);
            snd_pcm_info_set_subdevice(info, 0);
            snd_pcm_info_set_stream(info, SND_PCM_STREAM_CAPTURE);

            if (snd_ctl_pcm_info(ctl, info) >= 0)
            {
                pcm_device = device;
                found = true;
                break;
            }
        }

        snd_ctl_close(ctl);

        return found;
    }

    std::string AlsaDeviceDetector::getSysfsPath(int card)
    {
        const std::string path =
            "/sys/class/sound/card" + std::to_string(card) + "/device";

        std::error_code ec;

        auto resolved = std::filesystem::canonical(path, ec);

        if (ec)
        {
            return {};
        }

        return resolved.string();
    }

    std::string AlsaDeviceDetector::getUsbTopology(
        const std::string &path)
    {
        if (path.empty())
        {
            return {};
        }

        std::filesystem::path current(path);

        // Example USB interface: 3-1:1.0
        const std::string interface_name =
            current.filename().string();

        const auto colon = interface_name.find(':');

        if (colon == std::string::npos)
        {
            return {};
        }

        // Extract USB device name: 3-1
        const std::string usb_name =
            interface_name.substr(0, colon);

        // Parent directory must also be 3-1
        const std::string parent_name =
            current.parent_path().filename().string();

        if (usb_name.empty() || usb_name != parent_name)
        {
            return {};
        }

        // USB topology must contain a hyphen
        if (usb_name.find('-') == std::string::npos)
        {
            return {};
        }

        return usb_name;
    }

    bool AlsaDeviceDetector::readUdevProperties(
        const std::string &sysfs_path,
        AlsaDevice &device)
    {
        if (sysfs_path.empty())
        {
            return false;
        }

        udev *udev_context = udev_new();

        if (udev_context == nullptr)
        {
            return false;
        }

        udev_device *interface_device =
            udev_device_new_from_syspath(
                udev_context,
                sysfs_path.c_str());

        if (interface_device == nullptr)
        {
            udev_unref(udev_context);
            return false;
        }

        udev_device *usb_device =
            udev_device_get_parent_with_subsystem_devtype(
                interface_device,
                "usb",
                "usb_device");

        auto get_property = [](
                                udev_device *dev,
                                const char *key) -> std::string
        {
            if (dev == nullptr)
                return {};

            const char *value =
                udev_device_get_property_value(dev, key);

            return value != nullptr ? value : "";
        };

        auto get_sysattr = [](
                               udev_device *dev,
                               const char *key) -> std::string
        {
            if (dev == nullptr)
                return {};

            const char *value =
                udev_device_get_sysattr_value(dev, key);

            return value != nullptr ? value : "";
        };

        device.id_path =
            get_property(interface_device, "ID_PATH");

        if (device.id_path.empty())
            device.id_path = get_property(usb_device, "ID_PATH");

        device.vendor_id =
            get_property(usb_device, "ID_VENDOR_ID");

        if (device.vendor_id.empty())
            device.vendor_id = get_sysattr(usb_device, "idVendor");

        device.model_id =
            get_property(usb_device, "ID_MODEL_ID");

        if (device.model_id.empty())
            device.model_id = get_sysattr(usb_device, "idProduct");

        device.serial =
            get_property(usb_device, "ID_SERIAL");

        if (device.serial.empty())
            device.serial = get_sysattr(usb_device, "serial");

        udev_device_unref(interface_device);
        udev_unref(udev_context);

        return !device.id_path.empty();
    }

    std::vector<AlsaDevice> AlsaDeviceDetector::scan()
    {
        std::vector<AlsaDevice> devices;

        int card = -1;

        if (snd_card_next(&card) < 0)
        {
            return devices;
        }

        while (card >= 0)
        {
            AlsaDevice device;
            device.card_index = card;

            snd_ctl_t *ctl = nullptr;

            const std::string control_name =
                "hw:" + std::to_string(card);

            if (snd_ctl_open(&ctl, control_name.c_str(), 0) >= 0)
            {

                snd_ctl_card_info_t *info;
                snd_ctl_card_info_alloca(&info);

                if (snd_ctl_card_info(ctl, info) >= 0)
                {

                    device.card_id =
                        snd_ctl_card_info_get_id(info);

                    device.card_name =
                        snd_ctl_card_info_get_name(info);

                    device.card_long_name =
                        snd_ctl_card_info_get_longname(info);
                }

                snd_ctl_close(ctl);
            }

            device.sysfs_path = getSysfsPath(card);

            device.usb_topology =
                getUsbTopology(device.sysfs_path);

            readUdevProperties(
                device.sysfs_path,
                device);

            device.is_usb =
                !device.usb_topology.empty();

            std::string description =
                device.card_name + " " + device.card_long_name;

            std::transform(
                description.begin(),
                description.end(),
                description.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });

            device.is_cmedia =
                description.find("c-media") != std::string::npos ||
                description.find("cmedia") != std::string::npos;

            device.has_capture =
                hasCaptureDevice(card, device.pcm_device);

            if (device.is_usb && device.has_capture)
            {
                devices.push_back(device);
            }

            if (snd_card_next(&card) < 0)
            {
                break;
            }
        }

        return devices;
    }

    AlsaDevice AlsaDeviceDetector::findByIdPath(
        const std::string &id_path)
    {
        if (id_path.empty())
        {
            return {};
        }

        const auto devices = scan();

        for (const auto &device : devices)
        {
            if (device.id_path == id_path &&
                device.is_cmedia &&
                device.has_capture)
            {
                return device;
            }
        }

        return {};
    }

}
