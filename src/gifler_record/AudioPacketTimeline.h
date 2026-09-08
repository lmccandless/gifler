#pragma once
#include <algorithm>
#include <cstdint>

namespace gifler::record {
// QPC aligns the stream once; device frames preserve sample continuity thereafter.
class AudioPacketTimeline {
public:
    std::int64_t place(std::uint64_t position, std::uint32_t frames,
                       std::int64_t clockSample, bool timestampValid) {
        auto sample = initialized_ ? nextSample_ : clockSample;
        if (timestampValid) {
            if (positionValid_ && position >= nextPosition_)
                sample += static_cast<std::int64_t>(position - nextPosition_);
            else if (positionValid_)
                sample = std::max(sample, clockSample);
            nextPosition_ = position + frames;
        }
        positionValid_ = timestampValid;
        initialized_ = true;
        nextSample_ = sample + frames;
        return sample;
    }
private:
    bool initialized_ = false;
    bool positionValid_ = false;
    std::uint64_t nextPosition_ = 0;
    std::int64_t nextSample_ = 0;
};
}
