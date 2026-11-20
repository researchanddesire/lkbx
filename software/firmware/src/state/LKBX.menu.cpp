#include "LKBX.h"
#include "services/radHil.h"

#include "services/tasks.h"
#include "services/hardwareHealth.h"
#include "structs/MenuItem.h"
#include "ui/menuUI.h"
#include "utils/clearDisplay.h"
#include "utils/memory.h"
#include "utils/psram.h"

namespace sml = boost::sml;
using namespace sml;

void drawMenuTask(void *pvParameters) {
    // parse LKBX from pvParameters
    LKBX *lkbx = (LKBX *)pvParameters;
    auto items = lkbx->activeMenu;
    int numItems = lkbx->menuSize;

    auto isInCorrectState = [](LKBX *lkbx) {
        // Add any states that you want to support here.
        return lkbx->sm->is("home.idle"_s) || lkbx->sm->is("home"_s) ||
               lkbx->sm->is("home.settings"_s) || lkbx->sm->is("lockMenu"_s);
    };

    // Allocate the off-screen menu canvas (PSRAM-first). Retry briefly in case
    // memory is momentarily fragmented. On boards whose PSRAM failed init
    // (SPIRAM_IGNORE_NOTFOUND boots them with 0 PSRAM) don't try at all: the
    // ~131KB internal fallback either fails or starves WiFi/TLS. Without a
    // canvas, render via MenuUI::drawDirect instead of leaving the menu blank.
    const bool psramOk = lockboxPsramAvailable();
    for (int attempt = 0; psramOk && !MenuUI::ensureCanvasAllocated() &&
                          isInCorrectState(lkbx) && attempt < 20;
         attempt++) {
        printMemoryDiagnostics("drawMenuTask");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    const bool useCanvas = psramOk && MenuUI::ensureCanvasAllocated();
    if (!useCanvas) {
        ESP_LOGW(MENU_UI_TAG,
                 "Menu canvas unavailable (psram=%d) - using direct draw",
                 psramOk);
        MenuUI::releaseCanvas();
    }

    int lastEncoderValue = 0;
    bool isFirstRun = true;

    while (isInCorrectState(lkbx)) {
#ifdef RAD_HIL_VARIANT
        radHilProgress(lkbx->sm->is("home.idle"_s) && isUnlocked() && !isPaddleLocked());
#endif
        // Continue reporting normal task progress when the screen dims/sleeps.
        // A heartbeat from another task alone must not hide a frozen menu task.
        if (!isFirstRun && lkbx->sm->is("home.idle"_s))
            hardwareHealthMenuProgress();
        // If we're sleeping, don't draw anything (poll often so encoder ISR
        // wake is noticed quickly).
        if (idleState != IdleState::NOT_IDLE) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        // check if the encoder has changed

        auto value = lkbx->encoder.readEncoder();

        if (!isFirstRun && value == lastEncoderValue) {
            vTaskDelay(100);
            continue;
        }

        if (useCanvas) {
            MenuUI::compose(value, numItems, items);
            MenuUI::blitTo(lkbx->display);
        } else {
            MenuUI::drawDirect(lkbx->display, value, numItems, items);
        }

        lastEncoderValue = value;
        isFirstRun = false;
        vTaskDelay(100);
    }

    MenuUI::releaseCanvas();
    TaskHandles::drawMenu = nullptr;
    vTaskDelete(nullptr);
};

void LKBX::drawMenu() {
    if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
        clearScreen(display);
        xSemaphoreGive(displayMutex);
    }
    encoder.setAcceleration(0);
    encoder.setBoundaries(0, menuSize - 1, true);
    encoder.setEncoderValue(0);

    // A page being entered and restoreMenu can both get here.
    std::lock_guard<std::mutex> lock(menuTaskStart);

    // if the task is already running, don't start it again
    if (TaskHandles::drawMenu != nullptr) {
        vTaskDelete(TaskHandles::drawMenu);
        // Not left pointing at the deleted task if the new one cannot start.
        TaskHandles::drawMenu = nullptr;
    }

    ESP_LOGD(TASKDEBUG_TAG, "Creating draw menu task");
    // The stack of the task deleted above is freed a moment later, so a first
    // attempt can fail where a second succeeds. If it still does not fit, the
    // menu is drawn once it does (restoreMenu).
    startTaskWithRetry(drawMenuTask, Tasks::drawMenu, this,
                       &TaskHandles::drawMenu);
}

// A menu page needs its task: it draws the menu and follows the dial. If the
// task could not be created when the page was entered (no free block of
// internal RAM for its stack), the page stayed blank under its header for as
// long as the box sat on it, and after a memory shortage that is the home
// menu. So the task is started here once it fits (utils/missingTask.h). The
// page was cleared when it was entered, and the task paints the whole menu
// on its first pass. Called from the activity log task.
void LKBX::restoreMenu() {
    const bool onMenu = sm->is("home.idle"_s) || sm->is("home.settings"_s) ||
                        sm->is("lockMenu"_s);
    if (!menuWatch.due(onMenu, TaskHandles::drawMenu != nullptr, millis())) {
        return;
    }

    std::lock_guard<std::mutex> lock(menuTaskStart);
    // The page started its own task since the look.
    if (TaskHandles::drawMenu != nullptr) return;

    // One attempt: this is tried again at the next look. A task started for
    // a page that has just been left ends by itself on its first pass.
    if (startTaskChecked(drawMenuTask, Tasks::drawMenu, this,
                         &TaskHandles::drawMenu)) {
        ESP_LOGW(MENU_UI_TAG,
                 "The menu task was not running; started it, the menu is "
                 "drawn now");
    }
}

void LKBX::drawEmergencyMenu() {
    if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
        clearScreen(display);
        xSemaphoreGive(displayMutex);
    }

    bool isEUnlock = false;
    bool isBreak = false;

    // get preferences access
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::session);
        isEUnlock = preferences.getBool(Keys::Session::isEUnlock, true);
        isBreak = preferences.getBool(Keys::Session::isBreak, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    if (isEUnlock && isBreak) {
        activeMenu = lockedMenu;
        menuSize = sizeof(lockedMenu) / sizeof(lockedMenu[0]);
    } else if (isEUnlock) {
        activeMenu = lockedMenuNoBreak;
        menuSize = sizeof(lockedMenuNoBreak) / sizeof(lockedMenuNoBreak[0]);
    } else if (isBreak) {
        activeMenu = lockedMenuNoEUnlock;
        menuSize = sizeof(lockedMenuNoEUnlock) / sizeof(lockedMenuNoEUnlock[0]);
    } else {
        activeMenu = lockedMenuNoBreakNoEUnlock;
        menuSize = sizeof(lockedMenuNoBreakNoEUnlock) /
                   sizeof(lockedMenuNoBreakNoEUnlock[0]);
    }

    // With a voting link the menu offers it right after Go Back.
    static MenuItem lockedMenuWithVote[maxLockedMenuItems];
    if (!readVoteUrl().isEmpty() && menuSize < maxLockedMenuItems) {
        int size = 0;
        for (int i = 0; i < menuSize; i++) {
            lockedMenuWithVote[size++] = activeMenu[i];
            if (activeMenu[i].id == MenuItemE::BACK)
                lockedMenuWithVote[size++] = voteMenuItem;
        }
        activeMenu = lockedMenuWithVote;
        menuSize = size;
    }

    encoder.setAcceleration(0);
    encoder.setBoundaries(0, menuSize - 1, true);
    encoder.setEncoderValue(0);

    ESP_LOGD(TASKDEBUG_TAG, "Creating draw emergency menu task");
    // The lock menu leads to Emergency Unlock: it must not stay blank because
    // the page before it had not yet given its stack back. If its task still
    // does not fit, restoreMenu starts it once it does.
    std::lock_guard<std::mutex> lock(menuTaskStart);
    startTaskWithRetry(drawMenuTask, Tasks::drawMenu, this,
                       &TaskHandles::drawMenu);
}
