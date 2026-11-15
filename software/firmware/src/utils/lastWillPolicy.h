#ifndef LOCKBOX_LAST_WILL_POLICY_H
#define LOCKBOX_LAST_WILL_POLICY_H

#include <cstdint>

// Which last will a connection gets. No Arduino dependencies: see
// test/test_last_will.
//
//   SEALED  the box holds a command key: the v4 "will" seal bound to its MQTT
//           client id (CommandGate::sealLastWill). The dashboard refuses a
//           plain will from such a box.
//   PLAIN   the box has never signed and has no burned key: today's plain
//           "disconnected" report, with a new id for every connection.
//   NONE    connect without a will. A box that signs never registers a plain
//           one, and a sealed will stamped before 2024 would be refused.
enum class LastWill : uint8_t { SEALED, PLAIN, NONE };

// 2024-01-01T00:00:00Z in milliseconds: the earliest time the dashboard
// accepts in a sealed will. The clock need not be server-verified beyond
// that, because the dashboard never compares it with the time of arrival.
constexpr uint64_t kLastWillEarliestMs = 1704067200000ULL;

inline LastWill lastWillKind(bool holdsKey, bool mayReportPlain,
                             uint64_t nowMs) {
    if (holdsKey) {
        return nowMs >= kLastWillEarliestMs ? LastWill::SEALED : LastWill::NONE;
    }
    return mayReportPlain ? LastWill::PLAIN : LastWill::NONE;
}

#endif  // LOCKBOX_LAST_WILL_POLICY_H
