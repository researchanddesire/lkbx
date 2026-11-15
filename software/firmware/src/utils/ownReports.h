#ifndef LOCKBOX_OWN_REPORTS_H
#define LOCKBOX_OWN_REPORTS_H

#include <stddef.h>
#include <stdint.h>

// The box publishes its state reports on the topic it takes commands from, so
// the broker hands every report straight back to it. A report is not a
// command: it was refused as "not a valid envelope" (or "bad signature" after
// a v1 sign-in), once per report, which filled the log and hid the lines that
// matter.
//
// The topic is the dashboard's contract and stays as it is. Instead the box
// remembers a fingerprint of the last few reports it sealed and drops a frame
// that is one of them, without a word. Anything else on the topic is still
// opened, checked and logged as before, so a refused command is as visible as
// it was.
//
// A fingerprint is the length and a 64-bit FNV-1a hash of the exact bytes
// sent. No Arduino dependencies: unit-tested natively (test/test_own_reports).
class OwnReports {
  public:
    static constexpr size_t kRemembered = 8;

    void remember(const char* report, size_t length) {
        if (report == nullptr || length == 0) return;
        entries_[next_].length = length;
        entries_[next_].hash = fingerprint(report, length);
        next_ = (next_ + 1) % kRemembered;
    }

    bool matches(const char* frame, size_t length) const {
        if (frame == nullptr || length == 0) return false;
        const uint64_t hash = fingerprint(frame, length);
        for (size_t index = 0; index < kRemembered; ++index) {
            if (entries_[index].length == length &&
                entries_[index].hash == hash) {
                return true;
            }
        }
        return false;
    }

  private:
    struct Entry {
        size_t length = 0;  // zero: nothing remembered in this slot
        uint64_t hash = 0;
    };

    static uint64_t fingerprint(const char* data, size_t length) {
        uint64_t hash = 14695981039346656037ULL;
        for (size_t index = 0; index < length; ++index) {
            hash ^= static_cast<uint8_t>(data[index]);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    Entry entries_[kRemembered];
    size_t next_ = 0;
};

#endif  // LOCKBOX_OWN_REPORTS_H
