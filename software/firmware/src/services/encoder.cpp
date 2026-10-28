#include "encoder.h"

#include "Arduino.h"
#include "constants/Pins.h"
#include "lastInteraction.h"

AiEsp32RotaryEncoder rotaryEncoder(
    Pins::Encoder::encoderA, Pins::Encoder::encoderB, -1, -1, 4, true);

static void IRAM_ATTR readEncoderISR() {
    setNotIdleISR();
    rotaryEncoder.readEncoder_ISR();
}

void initEncoder() {
    rotaryEncoder.begin();
    rotaryEncoder.setup(readEncoderISR);

    pinMode(Pins::Encoder::encoderA, INPUT_PULLUP);
    pinMode(Pins::Encoder::encoderB, INPUT_PULLUP);

    rotaryEncoder.setEncoderValue(0);
    rotaryEncoder.setAcceleration(0);
}
