#pragma once

namespace lockbox::security {

// Command signing is a one-way switch. A box with a burned eFuse key signs
// from the start; a v1 box signs once a proven /auth response has given it a
// command key. Either way an /auth 200 without a key never turns signing off
// again: a caller who could make the box take one such response (an older
// route, a dashboard outage) would otherwise get plain JSON accepted.
inline bool signaturesRequired(bool burnedKey, bool everSigned) {
    return burnedKey || everSigned;
}

// What a valid /auth 200 does to the stored command key. A new key always
// replaces the stored one; a response without one leaves it in place.
enum class AuthKeyAction { store, keep };

inline AuthKeyAction authKeyAction(bool responseHasKey) {
    return responseHasKey ? AuthKeyAction::store : AuthKeyAction::keep;
}

// A report may go out unsealed only from a box that has never signed and has
// no burned key, and only once the gate is known to hold no key. While the
// gate is not loaded nobody knows, so the report is dropped.
inline bool mayReportPlain(bool gateLoaded, bool burnedKey, bool everSigned,
                           bool holdsKey) {
    return gateLoaded && !holdsKey && !signaturesRequired(burnedKey, everSigned);
}

}  // namespace lockbox::security
