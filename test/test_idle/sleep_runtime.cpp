// Compile the real sleep service against hardware fakes, keeping its guards,
// event handler, wake-source setup and peripheral sequencing under test.
#define LOCKBOX_DISPLAY_H
#define LOCKBOX_INA_H
#define LOCKBOX_MOTOR_H
#include "sleep_fakes.h"
#include "../../src/services/sleep.cpp"

namespace Pins { namespace Buttons { int buttonEnter = 12; } }
