#ifndef LOCKBOX_DEVICEKEYPROVISIONING_H
#define LOCKBOX_DEVICEKEYPROVISIONING_H

enum class DeviceKeyOutcome {
    // A key is burned: sign in with v2.
    ready,
    // No key, and none was requested or received this boot: sign in with v1.
    unavailable,
    // A key arrived but could not be stored (E-LOCK-999). Sign in with v1.
    failed,
    // No key yet: the dashboard needs this box's v1 sign-in to enroll it
    // first. Sign in with v1, then call prepareDeviceKey() once more.
    afterSignIn,
};

// Pre-auth step, run before every sign-in. A chip without a v2 key asks the
// dashboard for one, once per boot (twice when it answers afterSignIn) and only
// until the dashboard gives a final answer, then burns and verifies it.
// Nothing here ever locks, unlocks or changes a session.
DeviceKeyOutcome prepareDeviceKey();

#endif  // LOCKBOX_DEVICEKEYPROVISIONING_H
