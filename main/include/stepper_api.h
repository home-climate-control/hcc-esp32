#ifndef _HCC_ESP32_STEPPER_API_H_
#define _HCC_ESP32_STEPPER_API_H_

#ifdef __cplusplus
extern "C" {
#endif

namespace stepper {

enum class Direction {
    down = -1,
    up = 1
};

class Stepper {
public:

    virtual ~Stepper() = 0;

    /**
     * Move the stepper in the specified direction, and return the current position.
     */
    virtual int step(Direction d) = 0;

    /**
     * Returns max supported microstepping divider, or 0 if microstepping is not supported.
     */
    virtual int getMaxMicrostep() = 0;

    /**
     * Returns currently used microstepping divider.
     *
     * NOTE: if microstepping is not supported, return value is 1, as a syntax sugar.
     */
    virtual int getMicrostep() = 0;

    /**
     * Set microstepping to the specified divider.
     *
     * Returns the provided value if set successfully, 0 if microstepping is not enabled by configuration,
     * -1 if microstepping not supported, and -2 if the given value is not supported.
     */
    virtual int setMicrostep(int divider) = 0;

    /**
     * Put the controller to sleep with {@code true}, wake up with {@code false}.
     *
     * Returns {@code true} if the controller supports this operation.
     */
    virtual bool powerSave(bool enable) = 0;
};
}
#ifdef __cplusplus
}
#endif //__cplusplus

#endif
