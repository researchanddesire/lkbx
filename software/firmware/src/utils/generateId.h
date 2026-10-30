#ifndef LOCKBOX_GENERATEID_H
#define LOCKBOX_GENERATEID_H

#include "Arduino.h"
#include "CloudInput.h"
#include "constants/strings.h"

enum MessageType { EVENT, STATE, UNKNOWN, SYNC, REBOOT };

// Structure to hold parsed JSON values
struct ParsedMessage {
    String id;
    MessageType type;
    long at;
    bool valid;
};

// Function to generate a random string ID
static String generateRandomID() {
    String randomID = "";
    const char* characters = Str::Debug::ALPHANUMERIC_CHARS;
    const int charactersLength = strlen(characters);

    for (int i = 0; i < 16; i++) {
        randomID += characters[esp_random() % charactersLength];
    }
    return randomID;
}

// MESSAGE STRUCTURE
// {
//   id: string, // a random event id string
//   type: "sync" | "reboot",
//   at: number // in milliseconds
// }

// Parse JSON from a string into the caller-owned document. Returns parsed
// header fields. The caller can reuse `doc` for further field access (e.g.
// SYNC payloads) without a second deserializeJson call.
inline ParsedMessage parseJsonMessage(const String& jsonString,
                                      JsonDocument& doc) {
    ParsedMessage parsedMessage{};

    DeserializationError error = deserializeJson(doc, jsonString);
    if (error || !lockbox::security::commandResponse(doc.as<JsonVariantConst>())) {
        Serial.print(Str::Debug::DESERIALIZE_FAILED);
        Serial.println(error.f_str());
        parsedMessage.valid = false;
        return parsedMessage;
    }

    parsedMessage.id = doc[Str::Json::ID].as<String>();
    parsedMessage.at = doc[Str::Json::AT].as<uint64_t>();
    parsedMessage.valid = true;

    String type = doc[Str::Json::TYPE].as<String>();
    if (type.equals(Str::MsgType::EVENT)) {
        parsedMessage.type = EVENT;
    } else if (type.equals(Str::MsgType::STATE)) {
        parsedMessage.type = STATE;
    } else if (type.equals(Str::MsgType::SYNC)) {
        parsedMessage.type = SYNC;
    } else if (type.equals(Str::MsgType::REBOOT)) {
        parsedMessage.type = REBOOT;
    } else {
        parsedMessage.type = UNKNOWN;
    }

    return parsedMessage;
}

#endif  // LOCKBOX_GENERATEID_H
