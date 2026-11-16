#ifndef LOCKBOX_MISSING_TASK_H
#define LOCKBOX_MISSING_TASK_H

#include <stdint.h>

// A page is drawn by a task of its own, and a task's stack is one block of
// internal RAM. When memory is short the task cannot be created, and the page
// is entered all the same: on the 2026-11-17 rig run R8 came back to its home
// menu with 4 KB free, the menu task did not fit, and the screen stayed half
// drawn for as long as the box sat on that page. Nothing tried again.
//
// So a page whose task is missing gets it started later, once there is
// memory for it. This is the decision only: when to try. A page that is just
// being entered starts its own task, so the task has to be missing on two
// looks in a row before it is started from here, and it is then tried once a
// look for as long as it stays missing.
//
// No Arduino dependencies, so it is unit-tested natively. See
// test/test_missing_task.

namespace missingTask {
    // How often the page is looked at. A task that has just ended gives its
    // stack back within a few hundred milliseconds.
    constexpr uint32_t kLookMs = 2000;
}  // namespace missingTask

class MissingTask {
  public:
    // Called often. `wanted`: a page that needs the task is up. `running`:
    // the task exists. True when the caller should start the task now.
    bool due(bool wanted, bool running, uint32_t nowMs) {
        if (looked_ &&
            static_cast<uint32_t>(nowMs - lookedMs_) < missingTask::kLookMs) {
            return false;
        }
        looked_ = true;
        lookedMs_ = nowMs;

        if (!wanted || running) {
            missing_ = false;
            return false;
        }
        if (!missing_) {
            // The first look: the page may be starting its task right now.
            missing_ = true;
            return false;
        }
        return true;
    }

    // The task was missing at the last look.
    bool missing() const { return missing_; }

  private:
    bool looked_ = false;
    uint32_t lookedMs_ = 0;
    bool missing_ = false;
};

#endif  // LOCKBOX_MISSING_TASK_H
