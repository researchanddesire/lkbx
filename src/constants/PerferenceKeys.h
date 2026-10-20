#ifndef LOCKBOX_PERFERENCEKEYS_H
#define LOCKBOX_PERFERENCEKEYS_H

#include <Arduino.h>

// groups
namespace Keys {
    // the group for the settings
    constexpr char mqtt[] = "mqtt";
    constexpr char base[] = "lockbox";
    constexpr char session[] = "session";

    namespace Base {
        constexpr char isAlpha[] = "isAlpha";
        constexpr char hasBeenOnboarded[] = "hasOnboarded";

        constexpr char isLocalPlay[] = "isLocalPlay";
        constexpr char isLocked[] = "isLocked";
        constexpr char isLockTimer[] = "isLockTimer";
        constexpr char isEBlocked[] = "isEBlocked";

        constexpr char isMotorCalibrated[] = "isMTRCalibrated";
        constexpr char isButtonCalibrated[] = "isBTNCalibrated";

        constexpr char isMotorInverted[] = "isMTRInverted";
        constexpr char isButtonInverted[] = "isBTNInverted";

        constexpr char lastUpdated[] = "lastUpdated";
        constexpr char emergencyEnd[] = "emergencyEnd";
        constexpr char pairingCode[] = "pairingCode";
        constexpr char hueShift[] = "hueShift";
        constexpr char isPaired[] = "isPaired";
        // NVS keys are limited to 15 characters. These identify an immutable
        // release whose automatic OTA attempt failed, preventing a boot loop.
        constexpr char otaFailRelease[] = "otaFailRelease";
        constexpr char otaFailCount[] = "otaFailCount";
        constexpr char otaFailBuild[] = "otaFailBuild";
        constexpr char otaFailVersion[] = "otaFailVersion";
        constexpr char otaFailTrack[] = "otaFailTrack";
        constexpr char otaInstalled[] = "otaInstalled";
    }

    namespace Session {
        constexpr char sessionId[] = "sessionId";
        constexpr char lockName[] = "lockName";
        constexpr char requestedBy[] = "requestedBy";
        constexpr char lockState[] = "lockState";
        constexpr char isBreak[] = "isBreak";
        constexpr char breakKeyholderOnly[] = "breakKhOnly";
        constexpr char isEUnlock[] = "isEUnlock";
        constexpr char isTimeDisp[] = "isTimeDisp";
        constexpr char displayElapsedTime[] = "displayElapsed";
        constexpr char isTestLock[] = "isTestLock";

        constexpr char breakReg[] = "breakReg";
        constexpr char breakMax[] = "breakMax";
        constexpr char breakLast[] = "breakLast";
        constexpr char breakStart[] = "breakStart";
        constexpr char isActiveBreak[] = "isActiveBreak";
        constexpr char breakEndAccepted[] = "breakEndAck";
        constexpr char lockEnd[] = "lockEnd";
        constexpr char lockStart[] = "lockStart";
        constexpr char duration[] = "duration";
        // Chaster freeze state. These keys live in the session namespace so a
        // new session starts unfrozen and clearState() drops them.
        constexpr char isFrozen[] = "isFrozen";
        constexpr char frozenAt[] = "frozenAt";
        constexpr char freezeReason[] = "freezeReason";
    }
}

#endif  // LOCKBOX_PERFERENCEKEYS_H
