#pragma once

#include "MidiMessage.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

class MidiLog {
public:
    explicit MidiLog(std::size_t capacity = 100000) : capacity_(capacity) {}

    bool append(MidiMessage message);
    void clear();
    std::size_t size() const { return messages_.size(); }
    const MidiMessage& at(std::size_t index) const { return messages_.at(index); }
    std::vector<MidiMessage> snapshot() const { return {messages_.begin(), messages_.end()}; }
    std::uint64_t totalReceived() const { return totalReceived_; }
    std::uint64_t discarded() const { return discarded_; }
    std::size_t capacity() const { return capacity_; }

private:
    std::size_t capacity_;
    std::deque<MidiMessage> messages_;
    std::uint64_t totalReceived_ = 0;
    std::uint64_t discarded_ = 0;
};
