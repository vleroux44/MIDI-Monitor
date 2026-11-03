#include "MidiLog.h"

bool MidiLog::append(MidiMessage message) {
    message.index = ++totalReceived_;
    bool discarded = false;
    if (messages_.size() >= capacity_) {
        messages_.pop_front();
        ++discarded_;
        discarded = true;
    }
    messages_.push_back(std::move(message));
    return discarded;
}

void MidiLog::clear() {
    messages_.clear();
    totalReceived_ = 0;
    discarded_ = 0;
}
