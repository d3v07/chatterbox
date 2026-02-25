#include <gtest/gtest.h>
#include <chatterbox/server/message_history.hpp>
#include <chatterbox/protocol/message.hpp>

using namespace chatterbox;
using namespace chatterbox::server;

static protocol::Message make_chat(const std::string& text) {
    auto msg = protocol::Message::create_chat_message(2, text);
    return msg;
}

TEST(MessageHistory, StartsEmpty) {
    MessageHistory h;
    EXPECT_EQ(h.size(), 0u);
}

TEST(MessageHistory, AppendIncreasesSize) {
    MessageHistory h;
    h.append(make_chat("hello"), "alice");
    h.append(make_chat("world"), "bob");
    EXPECT_EQ(h.size(), 2u);
}

TEST(MessageHistory, RecentReturnsMessages) {
    MessageHistory h;
    h.append(make_chat("first"),  "alice");
    h.append(make_chat("second"), "bob");
    h.append(make_chat("third"),  "carol");

    auto msgs = h.recent(2);
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_EQ(msgs[0].payload_as_string(), "second");
    EXPECT_EQ(msgs[1].payload_as_string(), "third");
}

TEST(MessageHistory, RecentLimitLargerThanSizeReturnsAll) {
    MessageHistory h;
    h.append(make_chat("a"), "x");
    auto msgs = h.recent(100);
    EXPECT_EQ(msgs.size(), 1u);
}

TEST(MessageHistory, CapacityEvictsOldest) {
    MessageHistory::Config cfg;
    cfg.capacity = 3;
    MessageHistory h(cfg);

    for (int i = 0; i < 5; ++i) {
        h.append(make_chat("msg" + std::to_string(i)), "user");
    }
    EXPECT_EQ(h.size(), 3u);
    auto msgs = h.recent(3);
    EXPECT_EQ(msgs[0].payload_as_string(), "msg2");
    EXPECT_EQ(msgs[2].payload_as_string(), "msg4");
}

TEST(MessageHistory, ClearResetsSize) {
    MessageHistory h;
    h.append(make_chat("x"), "y");
    h.clear();
    EXPECT_EQ(h.size(), 0u);
    EXPECT_TRUE(h.recent(10).empty());
}

TEST(MessageHistory, ReplayCallsCallbackInOrder) {
    MessageHistory h;
    h.append(make_chat("one"),   "a");
    h.append(make_chat("two"),   "b");
    h.append(make_chat("three"), "c");

    std::vector<std::string> received;
    h.replay(10, [&](const protocol::Message& msg) {
        received.push_back(msg.payload_as_string());
    });
    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0], "one");
    EXPECT_EQ(received[2], "three");
}
