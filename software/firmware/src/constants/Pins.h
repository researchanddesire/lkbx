#ifndef LOCKBOX_PINS_CPP
#define LOCKBOX_PINS_CPP

/**
 * Pin definitions for the OSSM
 *
 * These are all the hardware pins used by the OSSM.
 * These include pin definitions for Drivers, Buttons and Remotes
 *
 * To reference a pin, import this file and use the Pins namespace as follows:
 *
 * ```cpp
 * #include "constants/pins.cpp"
 *
 * Pins::button_in
 * Pins::Driver::motorStepPin
 * Pins::Homing::stopPin
 *
 * ```
 */

namespace Pins {

    namespace Display {

        constexpr int TFT_DC = 7;
        constexpr int TFT_CS = 1;
        constexpr int TFT_RESET = 16;
        constexpr int TFT_MOSI = 5;
        constexpr int TFT_SCLK = 2;
        constexpr int TFT_BL = 11;

    }

    namespace Buttons {
        // Button input pin
        // Button input pin
        constexpr int buttonBack = 0;
        extern int buttonEnter;
        constexpr int buttonPowerDet = 8;  // Switch
    }

    namespace MOTOR {
        extern int A;             // MTR1
        extern int B;             // MTR2
        constexpr int mode = 47;  // Mode
    }

    namespace Encoder {
        // Encoder input pins
        constexpr int encoderA = 13;
        constexpr int encoderB = 14;
    }

    namespace INA {
        // I2C pins
        constexpr int data = 9;
        constexpr int clock = 10;
    }

    // Definitely could be named better
    namespace Magnetics {
        // Magnetic sensor pins
        constexpr int hall1 = 6;
        constexpr int hall2 = 15;
        constexpr int tmr1 = 17;
        constexpr int tmr2 = 18;
        constexpr int tmr3 = 21;
    }

    namespace Power {
        constexpr int usbDetect = 4;
    }
}

#endif  // LOCKBOX_PINS_CPP
