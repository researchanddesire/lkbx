#pragma once

// Stable serial evidence shared by release firmware and the hardware runner.
// These hooks observe normal application work; they never advance the state
// machine, suppress updates, change identity, or keep the display awake.
void hardwareHealthBoot();
void hardwareHealthWifiConnected();
void hardwareHealthMqttConnected(const char* broker);
void hardwareHealthMenuProgress();
void hardwareHealthHeartbeat(bool atMainMenu);
