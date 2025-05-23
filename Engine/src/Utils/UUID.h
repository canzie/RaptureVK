#pragma once

#include <cstdint>
#include <chrono>
#include <random>
#include <mutex>

namespace Rapture {
    
using UUID = uint64_t;


class UUIDGenerator {
public:

    // Generate a new UUID
    static UUID Generate() {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Get current timestamp in nanoseconds
        auto now = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        
        // Generate random bits
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;
        uint64_t random_bits = dis(gen);
        
        // Combine timestamp (42 bits) with random bits (22 bits)
        // This gives us:
        // - 42 bits for timestamp (enough for ~139 years of nanosecond precision)
        // - 22 bits for random component (4,194,304 possible values per nanosecond)
        UUID uuid = (timestamp & 0x3FFFFFFFFFF) << 22;
        uuid |= (random_bits & 0x3FFFFF);
        
        return uuid;
    }

    // Check if a UUID is valid (non-zero)
    static bool IsValid(UUID uuid) {
        return uuid != 0;
    }

private:
    static std::mutex _mutex;
};


} // namespace Rapture
