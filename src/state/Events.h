#ifndef LOCKBOX_EVENTS_H
#define LOCKBOX_EVENTS_H

#include <boost/sml.hpp>

namespace sml = boost::sml;

enum class ErrorT : uint8_t {
    UNDEFINED,
    CANNOT_LOCK,
    CANNOT_UNLOCK,
    NO_BACKPLATE,
    WIFI_ERROR,
    WIFI_NETWORK_UNREACHABLE,
    WIFI_AUTH_FAILED,
    WIFI_NO_IP,
    SERVER_UNREACHABLE,
    TIMEOUT,
    MQTT_AUTH_FAILED,
    MQTT_TIMEOUT,
    NO_UPDATE_AVAILABLE,
    BREAK_NOT_ENABLED,
    TOO_SOON_FOR_BREAK,
    BREAK_KEYHOLDER_ONLY,
    OTA_FAILED,
};

/**
 * These are the events that the OSSM state machine can respond to.
 * They are used in OSSM.h and can be called from anywhere in the code that has
 * access to an OSSM state machine
 *
 * For Example:
 *  ossm->sm.process_event(ButtonPress{});
 *
 *
 * There's nothing special about these events, they are just structs.
 * They just happen to be defined inside of the OSSM State Machine class.
 */
struct BackPress {};
struct BackLongPress {};
struct ButtonPress {};
struct LongPress {};
struct DoublePress {};
struct WifiConnected {};
struct Done {};
// Only physical pin detection can advance calibration to motor movement.
struct ButtonCalibrated {};
struct PairingConfirmed {};

struct Error {
    ErrorT error = ErrorT::UNDEFINED;
};

struct SyncSession {};
struct Lock {};
struct LockTimer {
    uint64_t lockEnd;
};
// Re-read persisted freeze/timer metadata without moving the motor or
// acknowledging a lock. Payload-free so delayed events cannot restore stale state.
struct FreezeChange {};
struct Unlock {};
struct TimerExpired {};
struct LockOffered {};
struct Decline {};
struct Break {};
struct ForceUpdate {};
struct Recalibrate {};
struct WiFiSync {
    String ssid;
    String password;
};
struct EncoderValue {
    int value;
};

// Definitions to make the table easier to read.
static auto backPress = sml::event<BackPress>;
static auto backLongPress = sml::event<BackPress>;
static auto buttonPress = sml::event<ButtonPress>;
static auto longPress = sml::event<LongPress>;
static auto doublePress = sml::event<DoublePress>;
static auto done = sml::event<Done>;
static auto error = sml::event<Error>;
static auto wifiConnected = sml::event<WifiConnected>;
static auto forceUpdate = sml::event<ForceUpdate>;
static auto recalibrate = sml::event<Recalibrate>;

static auto syncSession = sml::event<SyncSession>;
static auto lock = sml::event<Lock>;
static auto locktimer = sml::event<LockTimer>;
static auto freezeState = sml::event<FreezeChange>;
static auto unlock = sml::event<Unlock>;
static auto timerExpired = sml::event<TimerExpired>;
static auto lockOffered = sml::event<LockOffered>;
static auto decline = sml::event<Decline>;
static auto breakE = sml::event<Break>;
static auto wifi = sml::event<WiFiSync>;
static auto encoder = sml::event<EncoderValue>;

#endif  // LOCKBOX_EVENTS_H
