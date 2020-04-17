#ifndef _HCC_ESP32_ONEWIRE_H_
#define _HCC_ESP32_ONEWIRE_H_

#ifdef __cplusplus
extern "C" {
#endif

namespace hcc_onewire {

#define MAX_DEVICES (CONFIG_HCC_ESP32_ONE_WIRE_MAX_DEVICES)


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

    DS18B20_Info *devices[MAX_DEVICES] = {0};
    char addresses[MAX_DEVICES][17];

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
     */
    int browse();
};

}

#ifdef __cplusplus
}
#endif //__cplusplus


#endif /* _HCC_ESP32_ONEWIRE_H_ */
