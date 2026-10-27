#include "pairingCode.h"
#include <PairingCodeLease.h>
#include <mutex>
#include "services/serverOnline.h"
#include "utils/clockCorrection.h"

namespace {
std::mutex mutex;
lockbox::security::PairingCodeLease lease;
}

void acceptPairingCode(const String& code, const String& expiresAt) {
    std::lock_guard<std::mutex> guard(mutex);
    lease.accept(code.c_str(), expiresAt.c_str(), currentWallClockMs(), currentMonotonicMs(), isTimeSynced);
}
String currentPairingCode() {
    std::lock_guard<std::mutex> guard(mutex);
    return String(lease.visible(currentWallClockMs(), currentMonotonicMs(), isTimeSynced).c_str());
}
bool pairingCodeRefreshDue() {
    std::lock_guard<std::mutex> guard(mutex);
    return lease.refreshDue(currentWallClockMs(), currentMonotonicMs(), isTimeSynced);
}
void markPairingCodeRefreshAttempt() {
    std::lock_guard<std::mutex> guard(mutex);
    lease.attempted(currentMonotonicMs());
}
