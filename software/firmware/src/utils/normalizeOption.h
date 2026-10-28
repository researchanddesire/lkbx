#ifndef LOCKBOX_NORMALIZEENCODER_H
#define LOCKBOX_NORMALIZEENCODER_H

static auto normalizeOption = [](int rawOption, int numOptions) {
    if (numOptions <= 0) return 0;
    return ((rawOption % numOptions) + numOptions) % numOptions;
};

#endif  // LOCKBOX_NORMALIZEENCODER_H
