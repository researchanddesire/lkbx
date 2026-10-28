#include "ina.h"

#include "constants/LogTags.h"

Adafruit_INA219 ina;
bool inaReady = false;

void initINA() {
    // Initialize I2C for INA219
    Wire.begin(Pins::INA::data, Pins::INA::clock);

    #ifdef NO_INA219_INSTALLED
        return;
    #endif

    for (int attempts = 0; attempts < 10; attempts++) {
        if (ina.begin(&Wire)) {
            ina.powerSave(false);
            inaReady = true;
            return;
        }
        ESP_LOGW(INA_TAG, "Failed to find INA219 chip, attempt %d/10", attempts + 1);
        delay(10);
    }

    ESP_LOGE(INA_TAG, "INA219 not found after 10 attempts, battery monitoring disabled");
}
