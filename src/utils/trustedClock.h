#pragma once
#include <cstdint>
#include <sys/time.h>

int lockboxGetTimeOfDay(struct timeval* tv, void* timezone = nullptr);
int lockboxSetTimeOfDay(const struct timeval* tv, const void* timezone = nullptr);
time_t lockboxTime(time_t* out = nullptr);
void clearNetworkTimeHint();
uint64_t networkTimeHintMs();
bool beginTlsClockBootstrap(uint64_t hintMs);
// A zero value restores the application's clock plus monotonic elapsed time.
bool finishTlsClockBootstrap(uint64_t authenticatedTimeMs);

// One shared transaction spans SNTP setup, TLS bootstrap, and local deadline
// correction. Concurrent callers skip their attempt without resetting the
// in-flight caller's correction snapshot.
class NetworkTimeTransaction {
 public:
    NetworkTimeTransaction();
    ~NetworkTimeTransaction();
    NetworkTimeTransaction(const NetworkTimeTransaction&) = delete;
    NetworkTimeTransaction& operator=(const NetworkTimeTransaction&) = delete;
    explicit operator bool() const { return acquired_; }
 private:
    bool acquired_ = false;
};
