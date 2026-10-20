#ifndef TEXT_PAGES_H
#define TEXT_PAGES_H

#include <constants/strings.h>

struct TextPage {
    const char* header = "";
    const char* url = "";
    const char* title = "";
    const char* tagline = "";
    const char* content = "";
    const char* leftFooter = "";
    const char* rightFooter = "";
};

static const TextPage initPairingSuccessPage = {
    .header = Str::Screen::INIT_PAIRING_SUCCESS_HEADER,
    .content = Str::Screen::INIT_PAIRING_SUCCESS_CONTENT};

static const TextPage otaFailedPage = {.header = Str::Screen::OTA_FAILED_HEADER,
                                       .content = Str::Screen::OTA_FAILED_CONTENT};

#endif  // TEXT_PAGES_H
