#ifndef LOCKBOX_MENUITEM_H
#define LOCKBOX_MENUITEM_H

#include <Arduino.h>

#include "constants/Colors.h"
#include "constants/PerferenceKeys.h"
#include "constants/strings.h"
#include "ui/iconsSmall.h"

// numeric enum for every menu item ever.
enum MenuItemE {
    LOCK,
    LOCK_TIMER,
    WIFI_SETTINGS,
    UPDATE,
    SLEEP,
    PAIRING,
    ENROL_IN_ALPHA,
    UNENROL_FROM_ALPHA,
    ABOUT,
    SETTINGS,
    RESTART,
    BACK,
    EMERGENCY,
    BREAK,
};

// struct for menu item
struct MenuItem {
    const MenuItemE id;
    const char* name;
    const uint8_t* bitmap;

    int color = -1;
    int unfocusedColor = -1;

    const char* setting = "";
};

// MainMenu
static const MenuItem mainMenu[] = {
    {MenuItemE::LOCK_TIMER, Str::Menu::LOCK, bitmap_clock},
    {MenuItemE::LOCK, Str::Menu::LOCK_FOREVER, bitmap_locked},
    {MenuItemE::SETTINGS, Str::Menu::SETTINGS, bitmap_settings},
    {MenuItemE::SLEEP, Str::Menu::SLEEP, bitmap_sleep},
};

static const int numMainMenu = sizeof(mainMenu) / sizeof(mainMenu[0]);

// EmergencyMenu
static const MenuItem lockedMenu[] = {
    {MenuItemE::BACK, Str::Menu::GO_BACK, bitmap_back},
    {MenuItemE::SLEEP, Str::Menu::SLEEP, bitmap_sleep},
    {MenuItemE::WIFI_SETTINGS, Str::Menu::WIFI_SETTINGS, bitmap_wifi},
    {MenuItemE::BREAK, Str::Menu::TAKE_A_BREAK, bitmap_coffee,
     .setting = Keys::Session::isBreak},
    {MenuItemE::EMERGENCY, Str::Menu::EMERGENCY_UNLOCK, bitmap_emergency, Colors::red,
     Colors::dimRed, .setting = Keys::Session::isEUnlock},
    {MenuItemE::RESTART, Str::Menu::RESTART_DEVICE, bitmap_restart},
};

static const MenuItem settingsMenu[] = {
    {MenuItemE::BACK, Str::Menu::GO_BACK, bitmap_back},
    {MenuItemE::WIFI_SETTINGS, Str::Menu::WIFI_SETTINGS, bitmap_wifi},
    {MenuItemE::PAIRING, Str::Menu::PAIRING, bitmap_link},
    {MenuItemE::UPDATE, Str::Menu::UPDATE_DEVICE, bitmap_update},
    {MenuItemE::ENROL_IN_ALPHA, Str::Menu::ENROL_IN_ALPHA, researchAndDesireBeaker02},
    {MenuItemE::RESTART, Str::Menu::RESTART_DEVICE, bitmap_restart},
};

static const int numSettingsMenu =
    sizeof(settingsMenu) / sizeof(settingsMenu[0]);

static const MenuItem lockedMenuNoEUnlock[] = {
    {MenuItemE::BACK, Str::Menu::GO_BACK, bitmap_back},
    {MenuItemE::SLEEP, Str::Menu::SLEEP, bitmap_sleep},
    {MenuItemE::WIFI_SETTINGS, Str::Menu::WIFI_SETTINGS, bitmap_wifi},
    {MenuItemE::BREAK, Str::Menu::TAKE_A_BREAK, bitmap_coffee,
     .setting = Keys::Session::isBreak},
    {MenuItemE::RESTART, Str::Menu::RESTART_DEVICE, bitmap_restart},
};

static const MenuItem lockedMenuNoBreak[] = {
    {MenuItemE::BACK, Str::Menu::GO_BACK, bitmap_back},
    {MenuItemE::SLEEP, Str::Menu::SLEEP, bitmap_sleep},
    {MenuItemE::WIFI_SETTINGS, Str::Menu::WIFI_SETTINGS, bitmap_wifi},
    {MenuItemE::BREAK, Str::Menu::TAKE_A_BREAK, bitmap_coffee,
     .setting = Keys::Session::isBreak},
    {MenuItemE::EMERGENCY, Str::Menu::EMERGENCY_UNLOCK, bitmap_emergency, Colors::red,
     Colors::dimRed, .setting = Keys::Session::isEUnlock},
    {MenuItemE::RESTART, Str::Menu::RESTART_DEVICE, bitmap_restart},
};

static const MenuItem lockedMenuNoBreakNoEUnlock[] = {
    {MenuItemE::BACK, Str::Menu::GO_BACK, bitmap_back},
    {MenuItemE::SLEEP, Str::Menu::SLEEP, bitmap_sleep},
    {MenuItemE::WIFI_SETTINGS, Str::Menu::WIFI_SETTINGS, bitmap_wifi},
    {MenuItemE::RESTART, Str::Menu::RESTART_DEVICE, bitmap_restart},
};

#endif  // LOCKBOX_MENUITEM_H
