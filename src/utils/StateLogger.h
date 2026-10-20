#ifndef LOCKBOX_STATELOGGER_H
#define LOCKBOX_STATELOGGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

#include <boost/sml.hpp>
#include <cassert>

#include "../constants/LogTags.h"
#include "constants/strings.h"
#include "generateId.h"
#include "services/lastInteraction.h"
#include "services/mqtt.h"

namespace sml = boost::sml;
using namespace sml;

/**
 * @brief Logs state machine events for the OSSM class.
 *
 * The StateLogger class is responsible for logging the events of the state
 * machine used in the OSSM class. The logging level can be adjusted according
 * to the project's needs. By default, only messages with a level of "LOG_DEBUG"
 * or above are shown. This can be modified in the platformio.ini file or by
 * adding one of the following build flags:
 */
struct StateLogger {
    volatile bool stateChangeMessagePending = false;

    template <class SM, class TEvent>
    [[gnu::used]] void log_process_event(const TEvent&) {
        const char* eventName = sml::aux::get_type_name<TEvent>();
        if (strncmp(eventName, Str::Debug::SML_PREFIX, strlen(Str::Debug::SML_PREFIX)) == 0) {
            ESP_LOGV(STATE_MACHINE_TAG, "event: %s", eventName);
        } else {
            ESP_LOGD(STATE_MACHINE_TAG, "event: %s", eventName);
        }
    }

    template <class SM, class TGuard, class TEvent>
    [[gnu::used]] void log_guard(const TGuard&, const TEvent&, bool result) {
        const char* resultString = result ? Str::Debug::GUARD_PASS : Str::Debug::GUARD_FAIL;
        ESP_LOGD(STATE_MACHINE_TAG, "%s: %s (event: %s)", resultString,
                 sml::aux::get_type_name<TGuard>(),
                 sml::aux::get_type_name<TEvent>());
    }

    template <class SM, class TAction, class TEvent>
    [[gnu::used]] void log_action(const TAction&, const TEvent&) {
        // #region agent log
        ESP_LOGD(STATE_MACHINE_TAG, "action: %s (event: %s)",
                 sml::aux::get_type_name<TAction>(),
                 sml::aux::get_type_name<TEvent>());
        // #endregion
    }

    template <class SM, class TSrcState, class TDstState>
    [[gnu::used]] void log_state_change(const TSrcState& src,
                                        const TDstState& dst) {
        ESP_LOGI(STATE_MACHINE_TAG, "%s -> %s", src.c_str(), dst.c_str());

        stateChangeMessagePending = true;
        setNotIdle(Str::Input::STATE_MACHINE);
    }
};
#endif  // LOCKBOX_STATELOGGER_H
