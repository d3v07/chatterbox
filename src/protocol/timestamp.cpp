#include <chatterbox/protocol/timestamp.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <thread>

namespace chatterbox {
namespace protocol {

// Timestamp implementation
Timestamp::Timestamp()
    : value_(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
      ).count())
{
}

Timestamp::Timestamp(ValueType value)
    : value_(value)
{
}

Timestamp::Timestamp(std::chrono::system_clock::time_point tp)
    : value_(std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()
      ).count())
{
}

Timestamp Timestamp::now() {
    return Timestamp();
}

Timestamp Timestamp::epoch() {
    return Timestamp(0);
}

std::chrono::system_clock::time_point Timestamp::to_time_point() const {
    return std::chrono::system_clock::time_point(
        std::chrono::microseconds(value_)
    );
}

std::string Timestamp::to_string() const {
    auto tp = to_time_point();
    auto time = std::chrono::system_clock::to_time_t(tp);
    auto us = value_ % 1000000;

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(6) << us << 'Z';
    return oss.str();
}

std::string Timestamp::to_readable_string() const {
    auto tp = to_time_point();
    auto time = std::chrono::system_clock::to_time_t(tp);

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%H:%M:%S");
    return oss.str();
}

int64_t Timestamp::elapsed_ms() const {
    return (now() - *this) / 1000;
}

Timestamp Timestamp::operator+(int64_t microseconds) const {
    return Timestamp(value_ + microseconds);
}

Timestamp Timestamp::operator-(int64_t microseconds) const {
    return Timestamp(value_ - microseconds);
}

int64_t Timestamp::operator-(const Timestamp& other) const {
    return static_cast<int64_t>(value_) - static_cast<int64_t>(other.value_);
}

Timestamp& Timestamp::operator+=(int64_t microseconds) {
    value_ += microseconds;
    return *this;
}

Timestamp& Timestamp::operator-=(int64_t microseconds) {
    value_ -= microseconds;
    return *this;
}

// MonotonicTimestamp implementation
MonotonicTimestamp::MonotonicTimestamp()
    : value_(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
      ).count())
{
}

MonotonicTimestamp::MonotonicTimestamp(ValueType value)
    : value_(value)
{
}

MonotonicTimestamp MonotonicTimestamp::now() {
    return MonotonicTimestamp();
}

int64_t MonotonicTimestamp::elapsed_ms() const {
    return (now() - *this) / 1000;
}

int64_t MonotonicTimestamp::elapsed_us() const {
    return now() - *this;
}

int64_t MonotonicTimestamp::operator-(const MonotonicTimestamp& other) const {
    return static_cast<int64_t>(value_) - static_cast<int64_t>(other.value_);
}

// TimestampGenerator implementation
TimestampGenerator::TimestampGenerator()
    : last_timestamp_(0)
    , sequence_(0)
    , counter_(0)
{
}

Timestamp TimestampGenerator::next() {
    uint64_t current = Timestamp::now().value();
    uint64_t last = last_timestamp_.load();

    // Ensure monotonically increasing timestamps
    while (current <= last) {
        current = last + 1;
    }

    // Try to update atomically
    while (!last_timestamp_.compare_exchange_weak(last, current)) {
        if (current <= last) {
            current = last + 1;
        }
    }

    counter_.fetch_add(1);
    return Timestamp(current);
}

std::pair<Timestamp, SequenceNum> TimestampGenerator::next_with_sequence() {
    auto ts = next();
    auto seq = sequence_.fetch_add(1);
    return {ts, seq};
}

void TimestampGenerator::reset() {
    last_timestamp_.store(0);
    sequence_.store(0);
    counter_.store(0);
}

// Utility functions
namespace time_utils {

void sleep_ms(int64_t milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void sleep_us(int64_t microseconds) {
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
}

uint64_t current_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

uint64_t current_time_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

Timestamp parse_timestamp(const std::string& str) {
    // Parse ISO 8601 format: YYYY-MM-DDTHH:MM:SS.uuuuuuZ
    std::tm tm = {};
    int microseconds = 0;

    std::istringstream iss(str);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

    if (iss.peek() == '.') {
        iss.ignore();
        iss >> microseconds;
    }

    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    tp += std::chrono::microseconds(microseconds);

    return Timestamp(tp);
}

double calculate_latency_ms(const Timestamp& sent, const Timestamp& received) {
    int64_t diff = received - sent;
    return static_cast<double>(diff) / 1000.0;
}

bool is_expired(const Timestamp& ts, int64_t timeout_ms) {
    return ts.elapsed_ms() > timeout_ms;
}

} // namespace time_utils

} // namespace protocol
} // namespace chatterbox
