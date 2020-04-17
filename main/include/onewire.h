#ifndef _HCC_ESP32_ONEWIRE_H_
#define _HCC_ESP32_ONEWIRE_H_

#include <vector>
#include "owb.h"
#include "owb_rmt.h"
#include "ds18b20.h"

#ifdef __cplusplus
extern "C" {
#endif

namespace hcc_onewire {

class OneWire {
private:

    /**
     * Debugging tag.
     */
    const char *TAG;

    /**
     * GPIO number to use to drive the 1-Wire bus.
     */
    int gpio;

    // Initialized in browse()
    owb_rmt_driver_info rmt_driver_info;

    // Initialized in browse()
    OneWireBus *owb = NULL;

    DS18B20_Info *devices[CONFIG_HCC_ESP32_ONE_WIRE_MAX_DEVICES] = {0};
    char addresses[CONFIG_HCC_ESP32_ONE_WIRE_MAX_DEVICES][17];

    /**
     * Number of devices found on 1-Wire bus. Expected to be atomically set by {@link browse()}.
     */
    int devicesFound = -1;

public:

    OneWire(const char *TAG, int gpio)
    {
        this->TAG = TAG;
        this->gpio = gpio;
    }

    /**
     * Discover connected devices.
     *
     * Returns the number of devices found.
     *
     * @see #devicesFound
     * @see #getDeviceCount
     */
    int browse();

    /**
     * Poll the sensors, and return their readings.
     */
    std::vector<float> poll();

    /**
     * Return number of devices discovered on the bus. -1 if {@link #browse()} hasn't been called yet.
     */
    inline int getDeviceCount()
    {
        return devicesFound;
    }

    inline char *getAddressAt(int offset)
    {
        return addresses[offset];
    };
};

}

#ifdef __cplusplus
}
#endif //__cplusplus


#endif /* _HCC_ESP32_ONEWIRE_H_ */
