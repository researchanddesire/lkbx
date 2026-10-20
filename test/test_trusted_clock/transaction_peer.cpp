#include "../../src/utils/trustedClock.h"

// A second translation unit must contend on the same transaction mutex.
bool tryCompetingTimeCorrection(int64_t* correctionSnapshot) {
    const NetworkTimeTransaction transaction;
    if (!transaction) return false;
    timeval tv{};
    lockboxGetTimeOfDay(&tv);
    *correctionSnapshot = int64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    return true;
}
