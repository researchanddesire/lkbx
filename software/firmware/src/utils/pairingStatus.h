#pragma once

enum class PairingStatus { Unknown, Unpaired, Paired };

constexpr PairingStatus pairingStatusFromHttp(int status) {
    // The dashboard returns 418 for an existing, unpaired lockbox. Its 404
    // also covers database lookup failures, so that is not proof of unpairing.
    return status == 200 ? PairingStatus::Paired
         : status == 418 ? PairingStatus::Unpaired
                         : PairingStatus::Unknown;
}
