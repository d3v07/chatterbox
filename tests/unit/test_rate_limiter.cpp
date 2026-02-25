#include <gtest/gtest.h>
#include <chatterbox/server/rate_limiter.hpp>
#include <thread>
#include <chrono>

using namespace chatterbox;
using namespace chatterbox::server;

TEST(RateLimiter, AllowsMessagesWithinBurst) {
    RateLimiter::Config cfg;
    cfg.max_messages = 5;
    cfg.window_ms    = 1000;
    cfg.penalty_ms   = 100;
    RateLimiter limiter(cfg);

    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(limiter.check(42)) << "message " << i << " should be allowed";
    }
}

TEST(RateLimiter, BlocksOnceburstExceeded) {
    RateLimiter::Config cfg;
    cfg.max_messages = 3;
    cfg.window_ms    = 2000;
    cfg.penalty_ms   = 5000;
    RateLimiter limiter(cfg);

    for (int i = 0; i < 3; ++i) limiter.check(1);
    EXPECT_FALSE(limiter.check(1)) << "4th message should be dropped";
}

TEST(RateLimiter, DropsIncrementCounter) {
    RateLimiter::Config cfg;
    cfg.max_messages = 2;
    cfg.window_ms    = 2000;
    cfg.penalty_ms   = 500;
    RateLimiter limiter(cfg);

    limiter.check(7);
    limiter.check(7);
    limiter.check(7); // dropped

    auto stats = limiter.get_stats(7);
    EXPECT_EQ(stats.total_dropped, 1u);
    EXPECT_EQ(stats.total_sent,    2u);
    EXPECT_EQ(limiter.total_drops(), 1u);
}

TEST(RateLimiter, RemoveUserClearsState) {
    RateLimiter::Config cfg;
    cfg.max_messages = 1;
    cfg.window_ms    = 5000;
    cfg.penalty_ms   = 10000;
    RateLimiter limiter(cfg);

    limiter.check(99);
    limiter.check(99); // triggers penalty
    limiter.remove_user(99);

    // After removal a fresh check should succeed
    EXPECT_TRUE(limiter.check(99));
}

TEST(RateLimiter, SoftEnforcementNeverPenalises) {
    RateLimiter::Config cfg;
    cfg.max_messages     = 2;
    cfg.window_ms        = 2000;
    cfg.soft_enforcement = true;
    RateLimiter limiter(cfg);

    // With soft enforcement all messages are allowed (just warns)
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(limiter.check(5));
    }
}

TEST(RateLimiter, IndependentPerUser) {
    RateLimiter::Config cfg;
    cfg.max_messages = 2;
    cfg.window_ms    = 2000;
    cfg.penalty_ms   = 1000;
    RateLimiter limiter(cfg);

    limiter.check(1); limiter.check(1);
    EXPECT_FALSE(limiter.check(1)) << "user 1 should be limited";
    EXPECT_TRUE(limiter.check(2))  << "user 2 should still be allowed";
}
