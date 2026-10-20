#!/usr/bin/env python3
"""Compile the real preferences wrapper against a counting in-memory NVS fake."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FAKE_PREFERENCES = r'''
#pragma once
#include <cstdint>
#include <map>
#include <string>
using String = std::string;
using SemaphoreHandle_t = void*;
inline void* xSemaphoreCreateMutex() { return nullptr; }
class Preferences {
 public:
  int opens = 0, reads = 0;
  bool failOpen = false, failWrite = false;
  std::string ns;
  std::map<std::string, std::string> values;
  std::string name(const char* key) { return ns + "/" + key; }
  bool begin(const char* name, bool = false) {
    ++opens; ns = name; return !failOpen;
  }
  void end() {}
  bool isKey(const char* key) { return values.count(name(key)); }
  String getString(const char* key, const char* fallback = "") {
    ++reads; return isKey(key) ? values[name(key)] : fallback;
  }
  bool getBool(const char* key, bool fallback = false) {
    ++reads; return isKey(key) ? values[name(key)] == "1" : fallback;
  }
  uint64_t getULong64(const char* key, uint64_t fallback = 0) {
    ++reads; return isKey(key) ? std::stoull(values[name(key)]) : fallback;
  }
  size_t putString(const char* key, const char* value) {
    if (failWrite) return 0;
    values[name(key)] = value; return 1;
  }
  size_t putBool(const char* key, bool value) { return putString(key, value ? "1" : "0"); }
  size_t putULong64(const char* key, uint64_t value) {
    return putString(key, std::to_string(value).c_str());
  }
  bool remove(const char* key) { return values.erase(name(key)); }
  bool clear() {
    for (auto it = values.begin(); it != values.end();) {
      if (it->first.rfind(ns + "/", 0) == 0) it = values.erase(it);
      else ++it;
    }
    return true;
  }
};
'''

CACHE_TEST = r'''
#include <cassert>
#include "services/settings.h"
#include "constants/PerferenceKeys.h"
int main() {
  LockboxPreferences p;
  SessionTiming timing;
  assert(!p.getLockedState());
  assert(p.getSessionTiming(timing));
  assert(timing.lockEnd == 0 && !timing.breakActive);
  assert(!timing.frozen && timing.frozenAt == 0 && timing.freezeReason.empty());
  const int reads = p.reads, opens = p.opens;
  for (int i = 0; i < 1000; ++i) {
    assert(!p.getLockedState());
    assert(p.getSessionTiming(timing));
  }
  assert(p.reads == reads && p.opens == opens);

  p.begin(Keys::base); p.putBool(Keys::Base::isLocked, true); p.end();
  assert(p.getLockedState());
  p.begin(Keys::session);
  p.putULong64(Keys::Session::lockStart, 100);
  p.putULong64(Keys::Session::lockEnd, 200);
  p.putULong64(Keys::Session::breakStart, 150);
  p.putULong64(Keys::Session::breakMax, 20);
  p.putBool(Keys::Session::isActiveBreak, true);
  p.putBool(Keys::Session::isFrozen, true);
  p.putULong64(Keys::Session::frozenAt, 175);
  p.putString(Keys::Session::freezeReason, "Paused by test");
  p.end();
  assert(p.getSessionTiming(timing));
  assert(timing.lockStart == 100 && timing.lockEnd == 200);
  assert(timing.breakStart == 150 && timing.breakMax == 20 && timing.breakActive);
  assert(timing.frozen && timing.frozenAt == 175 && timing.freezeReason == "Paused by test");
  const int frozenReads = p.reads, frozenOpens = p.opens;
  for (int i = 0; i < 1000; ++i) {
    assert(p.getSessionTiming(timing) && timing.frozen);
  }
  assert(p.reads == frozenReads && p.opens == frozenOpens);
  p.begin(Keys::session); p.putBool(Keys::Session::isFrozen, false); p.end();
  assert(p.getSessionTiming(timing) && !timing.frozen);
  p.begin(Keys::session); p.remove(Keys::Session::freezeReason); p.end();
  assert(p.getSessionTiming(timing) && timing.freezeReason.empty());

  p.begin(Keys::base); p.putBool(Keys::Base::isLocked, false); p.end();
  assert(!p.getLockedState());
  p.begin(Keys::session); p.remove(Keys::Session::isActiveBreak); p.end();
  assert(p.getSessionTiming(timing) && !timing.breakActive);
  p.begin(Keys::session); p.clear(); p.end();
  assert(p.getSessionTiming(timing) && timing.lockEnd == 0);

  p.begin(Keys::session); p.putULong64(Keys::Session::lockEnd, 300); p.end();
  p.failOpen = true;
  assert(!p.getSessionTiming(timing));
  p.failOpen = false;
  assert(p.getSessionTiming(timing) && timing.lockEnd == 300);
  p.failWrite = true;
  p.begin(Keys::session); assert(p.putULong64(Keys::Session::lockEnd, 400) == 0); p.end();
  assert(p.getSessionTiming(timing) && timing.lockEnd == 300);
  p.failWrite = false;

  String id, state;
  p.begin(Keys::session); p.putString(Keys::Session::sessionId, "first"); p.end();
  assert(p.getSessionIdentity(id, state) && id == "first");
  p.begin(Keys::session); p.putString(Keys::Session::sessionId, String("second")); p.end();
  assert(p.getSessionIdentity(id, state) && id == "second");
}
'''


class SettingsCacheTests(unittest.TestCase):
    def test_polling_and_all_mutations(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / "Preferences.h").write_text(FAKE_PREFERENCES)
            (temp / "Arduino.h").write_text('#include "Preferences.h"\n')
            (temp / "test.cpp").write_text(CACHE_TEST)
            subprocess.run([
                "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                f"-I{temp}", f"-I{ROOT / 'src'}", str(temp / "test.cpp"),
                str(ROOT / "src/services/settings.cpp"), "-o", str(temp / "test"),
            ], check=True)
            subprocess.run([str(temp / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
