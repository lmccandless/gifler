#include "gifler_record/RecorderSession.h"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
    gifler::record::RecorderSession session;
    gifler::record::RecorderSettings settings{};
    settings.fps = 10;
    settings.queueCapacity = 4;

    std::cout << "Starting synthetic recording for 2 seconds...\n";
    session.start_synthetic(settings, 160, 90);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    session.stop();

    const auto& frames = session.frame_store().frames();
    std::cout << "Stored frames after coalescing: " << frames.size() << "\n";
    if (frames.empty()) {
        std::cerr << "No frames stored.\n";
        return 1;
    }
    std::cout << "First frame: " << frames.front().width << "x" << frames.front().height << " stride " << frames.front().stride << "\n";
    return 0;
}
