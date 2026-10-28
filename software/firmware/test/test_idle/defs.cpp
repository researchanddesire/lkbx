// Link the actual implementation so the cross-translation-unit tests exercise
// the firmware behavior, using this suite's clock/Arduino stubs.
#include "../../src/services/lastInteraction.cpp"
unsigned long _mock_millis = 0;
