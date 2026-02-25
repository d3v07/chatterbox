#include <gtest/gtest.h>
#include <chatterbox/server/config_parser.hpp>

using namespace chatterbox::server;

static const char* SAMPLE_INI = R"ini(
# Server configuration
[server]
max_users        = 32
thread_pool_size = 4
log_file         = chatterbox.log
enable_logging   = true

[rate_limit]
max_messages = 10
window_ms    = 1000
penalty_ms   = 5000

[history]
capacity        = 200
max_age_seconds = 3600
persist_to_disk = false
)ini";

TEST(ConfigParser, ParsesStrings) {
    ConfigParser p;
    p.parse(SAMPLE_INI);
    EXPECT_EQ(p.get("server", "log_file"), "chatterbox.log");
}

TEST(ConfigParser, ParsesInts) {
    ConfigParser p;
    p.parse(SAMPLE_INI);
    EXPECT_EQ(p.get_int("server",     "max_users"),         32);
    EXPECT_EQ(p.get_int("server",     "thread_pool_size"),  4);
    EXPECT_EQ(p.get_int("rate_limit", "max_messages"),      10);
    EXPECT_EQ(p.get_int("history",    "capacity"),          200);
}

TEST(ConfigParser, ParsesBools) {
    ConfigParser p;
    p.parse(SAMPLE_INI);
    EXPECT_TRUE (p.get_bool("server",  "enable_logging"));
    EXPECT_FALSE(p.get_bool("history", "persist_to_disk"));
}

TEST(ConfigParser, DefaultsOnMissingKey) {
    ConfigParser p;
    p.parse(SAMPLE_INI);
    EXPECT_EQ(p.get    ("server", "missing_key", "default"), "default");
    EXPECT_EQ(p.get_int("server", "missing_int", 99),         99);
    EXPECT_EQ(p.get_bool("server","missing_bool", true),     true);
}

TEST(ConfigParser, HasReturnsFalseForMissing) {
    ConfigParser p;
    p.parse(SAMPLE_INI);
    EXPECT_TRUE (p.has("server",  "max_users"));
    EXPECT_FALSE(p.has("server",  "nonexistent"));
    EXPECT_FALSE(p.has("missing", "key"));
}

TEST(ConfigParser, ThrowsOnUnterminatedSection) {
    ConfigParser p;
    EXPECT_THROW(p.parse("[bad_section"), ConfigParser::ParseError);
}

TEST(ConfigParser, ThrowsOnMissingEquals) {
    ConfigParser p;
    EXPECT_THROW(p.parse("[sec]\nbadkey"), ConfigParser::ParseError);
}

TEST(ConfigParser, IgnoresComments) {
    ConfigParser p;
    p.parse("[s]\n# this is a comment\n; another comment\nkey = val\n");
    EXPECT_EQ(p.get("s", "key"), "val");
    EXPECT_FALSE(p.has("s", "# this is a comment"));
}
