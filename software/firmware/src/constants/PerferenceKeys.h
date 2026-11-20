#ifndef LOCKBOX_PERFERENCEKEYS_H
#define LOCKBOX_PERFERENCEKEYS_H

#include <Arduino.h>

// groups
namespace Keys {
    // the group for the settings
    constexpr char mqtt[] = "mqtt";
    constexpr char base[] = "lockbox";
    constexpr char session[] = "session";
    constexpr char identity[] = "identity";

    namespace Identity {
        constexpr char secret[] = "secret";
        constexpr char commandKey[] = "cmdKey";
        constexpr char commandLastTimestamp[] = "cmdLastTs";
        // The command replay ring and timestamp (CommandGate replay state).
        constexpr char commandReplay[] = "cmdReplay";
        // Set once a command key was stored: signing never turns off again.
        constexpr char signingRequired[] = "cmdSigned";
        // The dashboard's answer to the one-time v2 key request: it will
        // never send a key (refused), or the earliest time to ask again.
        constexpr char keyRequestRefused[] = "v2Refused";
        constexpr char keyRequestRetryAt[] = "v2RetryAt";
        // Why the last key received could not be stored (E-LOCK-999 code).
        constexpr char keyFailure[] = "v2Failure";
    }

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
        // The emergency unlock's record (utils/emergencyRecord.h). The
        // session it ended, until the dashboard is seen to have ended it too;
        // empty for a local session.
        constexpr char emergencySession[] = "eSession";
        // The EMERGENCY report in plain form, and whether the broker has yet
        // to acknowledge it (utils/emergencyReport.h).
        constexpr char emergencyReport[] = "eReport";
        constexpr char emergencyOwed[] = "eOwed";
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
        // Chaster controls this session: the box asks the dashboard before it
        // opens for a break or unlocks. Cleared with the session.
        constexpr char chasterControlled[] = "chaster";
        // Page where others vote on the session (Chaster's Share links page),
        // shown as a QR code from the lock menu. Empty when there is none.
        constexpr char voteUrl[] = "voteUrl";
    }
}

#endif  // LOCKBOX_PERFERENCEKEYS_H
