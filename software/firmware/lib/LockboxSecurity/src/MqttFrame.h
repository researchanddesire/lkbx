#pragma once

#include <cstddef>
#include <cstring>

namespace lockbox::security {

enum class FrameResult { rejected, incomplete, complete };

// ESP-MQTT may split one publication across multiple callbacks. Nothing is
// parsed or dispatched until the entire, correctly addressed frame arrives.
template <std::size_t Capacity = 8192>
class MqttFrame {
 public:
    void reset() { size_ = expected_ = 0; }
    const char* data() const { return data_; }
    std::size_t size() const { return size_; }

    FrameResult append(const char* allowedTopic, const char* topic,
                       int topicLength, const char* bytes, int length,
                       int total, int offset, bool retained = false) {
        if (offset == 0) reset();
        // A retained command would fire again on every reconnect. Dropping the
        // first fragment leaves later fragments with nothing to continue.
        if (retained) {
            reset();
            return FrameResult::rejected;
        }
        if (!allowedTopic || !bytes || length <= 0 || total <= 0 ||
            offset < 0 || topicLength < 0 ||
            static_cast<std::size_t>(total) > Capacity || offset > total ||
            length > total - offset || std::memchr(bytes, '\0', length)) {
            reset();
            return FrameResult::rejected;
        }
        const bool hasTopic = topic && topicLength > 0;
        if ((offset == 0 && !hasTopic) ||
            (hasTopic && (std::strlen(allowedTopic) !=
                              static_cast<std::size_t>(topicLength) ||
                          std::memcmp(topic, allowedTopic, topicLength)))) {
            reset();
            return FrameResult::rejected;
        }
        if (offset == 0) expected_ = total;
        if (expected_ != static_cast<std::size_t>(total) ||
            size_ != static_cast<std::size_t>(offset)) {
            reset();
            return FrameResult::rejected;
        }
        std::memcpy(data_ + size_, bytes, length);
        size_ += length;
        data_[size_] = '\0';
        return size_ == expected_ ? FrameResult::complete
                                 : FrameResult::incomplete;
    }

 private:
    char data_[Capacity + 1]{};
    std::size_t size_ = 0;
    std::size_t expected_ = 0;
};

}  // namespace lockbox::security
