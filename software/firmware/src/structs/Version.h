#ifndef LOCKBOX_VERSION_STRUCT_H
#define LOCKBOX_VERSION_STRUCT_H

#include <stdio.h>

#include "constants/Version.h"

struct Version {
    int major = 0;
    int minor = 1;
    int patch = 1;
};

inline const Version currentVersion = {
    .major = MAJOR_VERSION,
    .minor = MINOR_VERSION,
    .patch = PATCH_VERSION,
};

inline char currentVersionString[16] = "";

inline void initVersionString() {
    snprintf(currentVersionString, sizeof(currentVersionString), "%d.%d.%d",
             MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION);
}

#endif  // LOCKBOX_VERSION_STRUCT_H
