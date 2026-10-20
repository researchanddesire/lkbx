#include "caller.h"

#include "../../src/services/lastInteraction.h"

extern unsigned long _mock_millis;

void callerSetNotIdle(unsigned long mockTime) {
    _mock_millis = mockTime;
    setNotIdle("test_caller");
}

void callerSetNotIdleISR(unsigned long mockTime) {
    _mock_millis = mockTime;
    setNotIdleISR();
}
