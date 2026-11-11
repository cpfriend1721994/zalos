// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zcall::audio {

struct InMessage {
    enum class Kind { Frame, Mute, Speaker, Volume, Device, Bad };
    Kind kind{Kind::Bad};
    uint32_t idx{0};
    uint32_t src{0};
    std::vector<uint8_t> payload;
    bool on{false};
    uint8_t value{0};
    char which{0};
    std::string name;
    uint8_t badKind{0};
};

class InParser {
public:
    void feed(const uint8_t* data, size_t n);
    bool next(InMessage& out);
    bool failed() const { return m_failed; }
    size_t pending() const { return m_buf.size() - m_pos; }

private:
    std::vector<uint8_t> m_buf;
    size_t m_pos{0};
    bool m_failed{false};
};

std::vector<uint8_t> encodeLevelFrame(uint8_t level, const uint8_t* opus, uint16_t size);
std::vector<uint8_t> encodeInFrame(uint32_t idx, uint32_t src, const std::vector<uint8_t>& payload);
std::string pyBytesRepr(uint8_t b);

}
