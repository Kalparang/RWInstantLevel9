#pragma once
#include <cmath>
#include <cstdint>

namespace mastery {
enum class Reason { Boost, NoAward, AlreadyMax, InvalidProgress, WrongCap, InvalidCurve, InconsistentTotal };
struct Decision { uint32_t gain; Reason reason; };

// Native progression is (display rank - 1) + fractional progress, NOT raw XP.
// thresholds[7] is the cumulative XP needed for displayed rank 9.
inline Decision adjust(float progress, uint32_t current, uint32_t gained,
                       const uint16_t* thresholds, uint16_t maxRank) noexcept {
    if (gained == 0) return {gained, Reason::NoAward};
    if (!std::isfinite(progress) || progress < 0.0f || progress > 8.0f)
        return {gained, Reason::InvalidProgress};
    if (progress == 8.0f) return {gained, Reason::AlreadyMax};
    if (maxRank != 9) return {gained, Reason::WrongCap};
    uint32_t previous = 0;
    for (unsigned i = 0; i < 8; ++i) {
        if (thresholds[i] <= previous) return {gained, Reason::InvalidCurve};
        previous = thresholds[i];
    }
    const auto rank = static_cast<unsigned>(std::floor(progress));
    const uint32_t lower = rank ? thresholds[rank - 1] : 0;
    const float fractional = progress - static_cast<float>(rank);
    const uint32_t expected = static_cast<uint32_t>(
        static_cast<float>(lower) + fractional * static_cast<float>(thresholds[rank] - lower));
    const auto difference = current > expected ? current - expected : expected - current;
    if (difference > 1 || current >= thresholds[7] || gained > 65535)
        return {gained, Reason::InconsistentTotal};
    // Exactly the remaining amount: avoids the native 16-bit sum wrapping.
    return {static_cast<uint32_t>(thresholds[7]) - current, Reason::Boost};
}
} // namespace mastery
