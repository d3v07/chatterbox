#ifndef CHATTERBOX_PROTOCOL_TIMESTAMP_HPP
#define CHATTERBOX_PROTOCOL_TIMESTAMP_HPP

#include <chatterbox/common.hpp>
#include <cstdint>
#include <chrono>
#include <string>
#include <atomic>

namespace chatterbox {
namespace protocol {

// High-resolution timestamp for message ordering
// Uses microseconds since epoch for precision
class Timestamp {
public:
    using ValueType = uint64_t;

    // Create timestamp with current time
    Timestamp();

    // Create timestamp with specific value
    explicit Timestamp(ValueType value);

    // Create from system clock time point
    explicit Timestamp(std::chrono::system_clock::time_point tp);

    // Get current time as timestamp
    static Timestamp now();

    // Get epoch (zero) timestamp
    static Timestamp epoch();

    // Get the raw value (microseconds since epoch)
    ValueType value() const { return value_; }

    // Convert to milliseconds
    int64_t to_milliseconds() const { return static_cast<int64_t>(value_ / 1000); }

    // Convert to seconds
    double to_seconds() const { return static_cast<double>(value_) / 1000000.0; }

    // Convert to system_clock time_point
    std::chrono::system_clock::time_point to_time_point() const;

    // Format as string (ISO 8601)
    std::string to_string() const;

    // Format as human-readable string
    std::string to_readable_string() const;

    // Time since this timestamp (in milliseconds)
    int64_t elapsed_ms() const;

    // Comparison operators
    bool operator==(const Timestamp& other) const { return value_ == other.value_; }
    bool operator!=(const Timestamp& other) const { return value_ != other.value_; }
    bool operator<(const Timestamp& other) const { return value_ < other.value_; }
    bool operator<=(const Timestamp& other) const { return value_ <= other.value_; }
    bool operator>(const Timestamp& other) const { return value_ > other.value_; }
    bool operator>=(const Timestamp& other) const { return value_ >= other.value_; }

    // Arithmetic operators
    Timestamp operator+(int64_t microseconds) const;
    Timestamp operator-(int64_t microseconds) const;
    int64_t operator-(const Timestamp& other) const;

    Timestamp& operator+=(int64_t microseconds);
    Timestamp& operator-=(int64_t microseconds);

    // Check if timestamp is valid (non-zero)
    bool is_valid() const { return value_ != 0; }

    // Check if timestamp is in the past
    bool is_past() const { return *this < now(); }

    // Check if timestamp is in the future
    bool is_future() const { return *this > now(); }

private:
    ValueType value_; // Microseconds since Unix epoch
};

// Monotonic timestamp for measuring intervals
// Uses steady_clock to be immune to system clock changes
class MonotonicTimestamp {
public:
    using ValueType = uint64_t;

    MonotonicTimestamp();
    explicit MonotonicTimestamp(ValueType value);

    static MonotonicTimestamp now();

    ValueType value() const { return value_; }

    int64_t elapsed_ms() const;
    int64_t elapsed_us() const;

    bool operator<(const MonotonicTimestamp& other) const { return value_ < other.value_; }
    bool operator>(const MonotonicTimestamp& other) const { return value_ > other.value_; }

    int64_t operator-(const MonotonicTimestamp& other) const;

private:
    ValueType value_; // Microseconds since arbitrary epoch
};

// Timestamp generator with guaranteed uniqueness
class TimestampGenerator {
public:
    TimestampGenerator();

    // Get next unique timestamp
    Timestamp next();

    // Get next unique timestamp with sequence number
    std::pair<Timestamp, SequenceNum> next_with_sequence();

    // Reset the generator
    void reset();

    // Get total timestamps generated
    uint64_t count() const { return counter_.load(); }

private:
    std::atomic<uint64_t> last_timestamp_;
    std::atomic<SequenceNum> sequence_;
    std::atomic<uint64_t> counter_;
};

// Utility functions
namespace time_utils {

// Sleep for specified duration
void sleep_ms(int64_t milliseconds);
void sleep_us(int64_t microseconds);

// Get current time in various formats
uint64_t current_time_ms();
uint64_t current_time_us();

// Parse timestamp from string
Timestamp parse_timestamp(const std::string& str);

// Calculate latency between two timestamps
double calculate_latency_ms(const Timestamp& sent, const Timestamp& received);

// Check if a timestamp has expired
bool is_expired(const Timestamp& ts, int64_t timeout_ms);

} // namespace time_utils

} // namespace protocol
} // namespace chatterbox

#endif // CHATTERBOX_PROTOCOL_TIMESTAMP_HPP
