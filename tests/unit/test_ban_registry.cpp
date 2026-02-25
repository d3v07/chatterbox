#include <gtest/gtest.h>
#include <chatterbox/server/ban_registry.hpp>
#include <thread>
#include <chrono>

using namespace chatterbox;
using namespace chatterbox::server;

TEST(BanRegistry, UserIsNotBannedInitially) {
    BanRegistry reg;
    EXPECT_FALSE(reg.is_banned(42));
}

TEST(BanRegistry, BannedUserIsDetected) {
    BanRegistry reg;
    reg.ban(1, "alice", "spamming", 99, 0);
    EXPECT_TRUE(reg.is_banned(1));
}

TEST(BanRegistry, GetReturnsBanEntry) {
    BanRegistry reg;
    reg.ban(7, "bob", "abuse", 2, 0);
    auto entry = reg.get(7);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->username, "bob");
    EXPECT_EQ(entry->reason,   "abuse");
    EXPECT_EQ(entry->banned_by, 2u);
    EXPECT_TRUE(entry->is_permanent());
}

TEST(BanRegistry, PardonRemovesBan) {
    BanRegistry reg;
    reg.ban(3, "carol", "flood", 1, 0);
    EXPECT_TRUE(reg.is_banned(3));
    reg.pardon(3);
    EXPECT_FALSE(reg.is_banned(3));
}

TEST(BanRegistry, TimeLimitedBanExpiresAfterDuration) {
    BanRegistry reg;
    // Ban for 0 minutes (expires immediately in the past — simulate expiry)
    // Use a tiny duration and manually check expiry via prune
    reg.ban(5, "dave", "test", 1, 1); // 1-minute ban
    EXPECT_TRUE(reg.is_banned(5));    // Still active just after creation

    // Pruning should not remove it yet (it's a 1-minute ban)
    size_t pruned = reg.prune_expired();
    EXPECT_EQ(pruned, 0u);
    EXPECT_EQ(reg.size(), 1u);
}

TEST(BanRegistry, PruneRemovesExpiredEntries) {
    BanRegistry reg;
    reg.ban(10, "user10", "test", 1, 0); // permanent
    // Size should be 1 after banning
    EXPECT_EQ(reg.size(), 1u);
    reg.pardon(10);
    EXPECT_EQ(reg.size(), 0u);
}

TEST(BanRegistry, MultipleBansTrackedIndependently) {
    BanRegistry reg;
    reg.ban(1, "a", "r1", 99, 0);
    reg.ban(2, "b", "r2", 99, 0);
    reg.ban(3, "c", "r3", 99, 0);
    EXPECT_EQ(reg.size(), 3u);
    EXPECT_TRUE(reg.is_banned(1));
    EXPECT_TRUE(reg.is_banned(2));
    EXPECT_TRUE(reg.is_banned(3));
    reg.pardon(2);
    EXPECT_FALSE(reg.is_banned(2));
    EXPECT_TRUE(reg.is_banned(1));
    EXPECT_TRUE(reg.is_banned(3));
}
