#ifndef _HCC_ESP32_ONEWIRE_H_
#define _HCC_ESP32_ONEWIRE_H_

#include <string>
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
    gpio_num_t gpioOnewire;

    /**
     * GPIO number to use to flash the LED. Use {@code GPIO_NUM_NC} to disable.
     */
    gpio_num_t gpioLED;

    /**
     * Flash the LED for this many milliseconds.
     */
    long flashMillis;

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


    void flashLED();

public:

    /**
     * Create an instance.
     *
     * Specify {@code gpioLED} as {@code GPIO_NUM_NC} if you don't want to flash the LED.
     */
    OneWire(const char *TAG, gpio_num_t gpioOnewire, gpio_num_t gpioLED, long flashMillis)
    {
        this->TAG = TAG;
        this->gpioOnewire = gpioOnewire;
        this->gpioLED = gpioLED;
        this->flashMillis = flashMillis;
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
