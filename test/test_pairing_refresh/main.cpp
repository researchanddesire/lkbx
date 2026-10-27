#include <cassert>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <Arduino.h>
#include <PairingCodeLease.h>
#include <CloudInput.h>
#include "services/pairingCode.h"
#include "utils/pairingDashboard.h"
#include "utils/PairingPromptPoll.h"
#include "state/Events.h"

using lockbox::security::PairingCodeLease;
using lockbox::security::pairingExpiryMs;
using namespace boost::sml;

std::atomic<bool> isTimeSynced{true};
int64_t testWallMs = 1793059200000LL;
int64_t testMonotonicMs = 1000;
int64_t currentWallClockMs() { return testWallMs; }
int64_t currentMonotonicMs() { return testMonotonicMs; }

struct StateContext { int redraws = 0; };
struct DisplayMachine {
    auto operator()() const {
        auto drawInitPairingQR = [extra = 0](StateContext& value) { value.redraws += 1 + extra; };
        auto drawPairingQR = drawInitPairingQR;
        auto allow = [value = 1] { return value != 0; };
        auto nothing = [value = 0] { (void)value; };
        return make_transition_table(
            *"idle"_s + event<Done> [allow] / nothing = "idle"_s,
#include "../../src/state/PairingDisplayTransitions.inc"
            "init.preflight.pairingPrompt"_s + event<PairingConfirmed> [allow] / nothing = "success"_s,
            "init.preflight.pairingPrompt"_s + event<ButtonPress> [allow] / nothing = "after-skip"_s,
            "pairing"_s + event<ButtonPress> [allow] / nothing = "idle"_s
        );
    }
};

struct Reply { bool valid = false; };
struct Prompt {
    bool present = true, online = true, boot = true, due = true;
    bool leaveDuringAuth = false, leaveDuringStatus = false, authValid = true;
    int authCalls = 0, statusCalls = 0, confirms = 0, attempts = 0, paints = 0;
    bool active() const { return present; }
    bool connected() const { return online; }
    bool onboarding() const { return boot; }
    bool refreshDue() const { return due; }
    void markAttempt() { ++attempts; }
    void repaintIfChanged() { if (present) ++paints; }
    Reply authenticate(int timeout) {
        assert(timeout == 3000); ++authCalls;
        if (leaveDuringAuth) present = false;
        return {authValid};
    }
    void accept(Reply) { due = false; }
    PairingStatus pairingStatus(int timeout) {
        assert(timeout == 3000); ++statusCalls;
        if (leaveDuringStatus) present = false;
        return PairingStatus::Paired;
    }
    void confirm() { ++confirms; }
};

int main() {
    assert(pairingDashboardUrl("ABCDEF") == String(EXPECTED_PAIRING_URL));
    uint64_t epoch = 0;
    assert(pairingExpiryMs("2026-10-27T00:00:00.123Z", epoch));
    assert(epoch == 1793059200123ULL);
    uint64_t fraction = 0;
    assert(pairingExpiryMs("2026-10-27T00:00:00.1Z", fraction) && fraction == epoch - 23);
    assert(pairingExpiryMs("2026-10-27T00:00:00.123456Z", fraction) && fraction == epoch);
    assert(pairingExpiryMs("2024-02-29T23:59:59Z", fraction));
    for (const char* text : {"2025-02-29T00:00:00Z", "2026-10-27T00:00:00", "2026-10-27T00:00:00+00:00", "2026-13-01T00:00:00Z", "2026-01-01T24:00:00Z", "2026-01-01T00:00:60Z", "2026-01-01T00:00:00.Z", "2026-01-01T00:00:00.0000001Z", "2100-01-01T00:00:00Z"})
        assert(!pairingExpiryMs(text, fraction));
    PairingCodeLease lease;
    assert(lease.visible(epoch, 0, true).empty()); // reboot cannot trust old NVS
    assert(lease.accept("ABCDEF", "2026-10-27T00:05:00.123Z", epoch, 100, true));
    assert(lease.visible(epoch, 100, true) == "ABCDEF");
    assert(lease.visible(epoch + 289999, 290099, true) == "ABCDEF");
    assert(lease.visible(epoch + 290000, 290100, true).empty()); // cleared early
    assert(lease.refreshDue(epoch + 290000, 290100, true));
    lease.attempted(290100);
    assert(!lease.refreshDue(epoch + 294999, 295099, true));
    assert(lease.refreshDue(epoch + 295000, 295100, true));
    // Failure/malformed responses never extend an expired lease.
    assert(!lease.accept("MACADDRESS12", "", epoch, 299999, true));
    assert(!lease.accept("GHIJKL", "2026-10-27T00:00:00Z", epoch, 300100, true));
    assert(lease.visible(epoch + 300000, 300100, true).empty());
    assert(!lease.accept("GHIJKL", "2026-10-27T00:10:00Z", epoch, 300100, false));
    assert(lease.accept("GHIJKL", "2026-10-27T00:10:00Z", epoch + 300000, 300100, true));
    assert(lease.visible(epoch + 300000, 300100, true) == "GHIJKL");
    assert(lease.visible(epoch + 700000, 300101, true).empty()); // forward correction
    assert(lease.visible(epoch - 1000, 600100, true).empty()); // rollback cannot extend monotonic lease
    assert(lease.accept("ABC234", "", 0, 1000, false)); // old server: bounded RAM lease
    assert(lease.visible(0, 290999, false) == "ABC234");
    assert(lease.visible(0, 291000, false).empty());
    assert(lease.visible(0, 999, false).empty());
    JsonDocument auth;
    deserializeJson(auth, R"({"clientId":"channel","username":"synthetic","password":"synthetic","sessionId":"channel","pairingCode":"ABCDEF","hueShift":0,"isPaired":false})");
    assert(lockbox::security::authResponse(auth.as<JsonVariantConst>()));
    auth["pairingCodeExpiresAt"] = "2026-10-27T00:05:00.000Z";
    assert(lockbox::security::authResponse(auth.as<JsonVariantConst>()));
    auth["pairingCodeExpiresAt"] = "2026-10-27T00:05:00";
    assert(!lockbox::security::authResponse(auth.as<JsonVariantConst>()));
    for (int mode = 0; mode < 5; ++mode) {
        Prompt prompt;
        if (mode == 0) prompt.present = false;
        if (mode == 1) prompt.online = false;
        if (mode == 2) prompt.leaveDuringAuth = true;
        if (mode == 3) prompt.leaveDuringStatus = true;
        if (mode == 4) prompt.boot = false;
        pollPairingPrompt(prompt);
        assert(prompt.confirms == 0);
        if (mode < 2) assert(prompt.authCalls == 0 && prompt.statusCalls == 0);
        if (mode == 2 || mode == 4) assert(prompt.statusCalls == 0);
    }
    Prompt success; pollPairingPrompt(success); assert(success.confirms == 1);
    StateContext context;
    sm<DisplayMachine, testing> machine{context};
    machine.set_current_states("init.preflight.pairingPrompt"_s);
    assert(machine.process_event(PairingCodeChanged{}));
    assert(context.redraws == 1);
    machine.process_event(ButtonPress{});
    assert(!machine.process_event(PairingCodeChanged{}));
    assert(!machine.process_event(PairingConfirmed{}));
    assert(machine.is("after-skip"_s) && context.redraws == 1);
    machine.set_current_states("pairing"_s);
    assert(machine.process_event(PairingCodeChanged{}));
    assert(context.redraws == 2);
    acceptPairingCode("ABCDEF", "2026-10-27T00:05:00Z");
    assert(currentPairingCode() == "ABCDEF");
    std::vector<std::thread> readers;
    for (int index = 0; index < 6; ++index) readers.emplace_back([index] {
        for (int iteration = 0; iteration < 1000; ++iteration) {
            acceptPairingCode(index % 2 ? "ABCDEF" : "UVWXYZ", "2026-10-27T00:05:00Z");
            const String code = currentPairingCode();
            assert(code == "ABCDEF" || code == "UVWXYZ");
        }
    });
    for (auto& reader : readers) reader.join();
    testWallMs += 290000;
    testMonotonicMs += 290000;
    assert(currentPairingCode().empty());
    assert(pairingCodeRefreshDue());
    markPairingCodeRefreshAttempt();
    assert(!pairingCodeRefreshDue());
    std::cout << "PASS pairing UTC parsing/lease/rotation/offline/failure/Skip/settings and exact SML display fragment\n";
}
