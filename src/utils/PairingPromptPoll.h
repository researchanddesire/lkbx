#pragma once
#include "pairingStatus.h"

// The actual worker uses this transaction, outside the state-machine mutex.
// Callers re-check the current state before repaint/confirmation, so a response
// arriving after Skip cannot paint or advance an unrelated screen.
template<class Prompt>
void pollPairingPrompt(Prompt& prompt) {
    if (!prompt.active()) return;
    prompt.repaintIfChanged();
    if (!prompt.connected()) return;
    if (prompt.refreshDue()) {
        prompt.markAttempt();
        const auto response = prompt.authenticate(3000);
        if (response.valid) {
            prompt.accept(response);
            if (response.isPaired && prompt.active() && prompt.onboarding()) prompt.confirm();
        }
        prompt.repaintIfChanged();
    }
    if (prompt.active() && prompt.onboarding() &&
        prompt.pairingStatus(3000) == PairingStatus::Paired &&
        prompt.active() && prompt.onboarding()) prompt.confirm();
    prompt.repaintIfChanged();
}
