#ifndef CHATTERBOX_SERVER_BAN_REGISTRY_HPP
#define CHATTERBOX_SERVER_BAN_REGISTRY_HPP

#include <chatterbox/common.hpp>
#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <chrono>

namespace chatterbox {
namespace server {

struct BanEntry {
    UserId      user_id;
    std::string username;
    std::string reason;
    UserId      banned_by;
    TimePoint   banned_at;
    TimePoint   expires_at;      // Default: TimePoint::max() = permanent
    bool        is_permanent() const;
};

// Thread-safe registry of banned users.
// Bans can be permanent or time-limited.
class BanRegistry {
public:
    BanRegistry() = default;

    // Add a ban. duration_minutes = 0 means permanent.
    void ban(UserId      user_id,
             const std::string& username,
             const std::string& reason,
             UserId      banned_by,
             uint32_t    duration_minutes = 0);

    // Remove a ban early (pardon).
    bool pardon(UserId user_id);

    // Check whether a user is currently banned.
    bool is_banned(UserId user_id) const;

    // Get the ban record (nullopt if not banned or ban expired).
    std::optional<BanEntry> get(UserId user_id) const;

    // Prune all expired bans from the registry.
    size_t prune_expired();

    // Total number of active (possibly expired) entries.
    size_t size() const;

private:
    mutable std::mutex mu_;
    std::unordered_map<UserId, BanEntry> bans_;
};

} // namespace server
} // namespace chatterbox

#endif // CHATTERBOX_SERVER_BAN_REGISTRY_HPP
