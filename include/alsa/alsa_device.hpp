
#pragma once

#include <string>
#include <vector>

namespace usb_independent
{

    struct AlsaDevice
    {
        int card_index = -1;
        int pcm_device = -1;

        std::string card_id;
        std::string card_name;
        std::string card_long_name;

        std::string sysfs_path;
        std::string usb_topology;

        // Stable USB identity from udev
        std::string id_path;
        std::string vendor_id;
        std::string model_id;
        std::string serial;

        bool is_usb = false;
        bool is_cmedia = false;
        bool has_capture = false;
    };

    class AlsaDeviceDetector
    {
    public:
        std::vector<AlsaDevice> scan();
        AlsaDevice findByIdPath(const std::string &id_path);

    private:
        bool hasCaptureDevice(int card, int &pcm_device);

        std::string getSysfsPath(int card);
        std::string getUsbTopology(const std::string &path);

        bool readUdevProperties(
            const std::string &sysfs_path,
            AlsaDevice &device);
    };

}
