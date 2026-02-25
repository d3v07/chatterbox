#ifndef CHATTERBOX_SERVER_ROOM_MANAGER_HPP
#define CHATTERBOX_SERVER_ROOM_MANAGER_HPP

#include <chatterbox/common.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <optional>
#include <chrono>

namespace chatterbox {
namespace server {

struct Room {
    RoomId      id;
    std::string name;
    std::string topic;
    UserId      owner_id;
    bool        is_private   = false;
    size_t      max_members  = 32;
    TimePoint   created_at;
    std::unordered_set<UserId> members;
};

enum class RoomError {
    OK = 0,
    ROOM_NOT_FOUND,
    ROOM_ALREADY_EXISTS,
    ROOM_FULL,
    NOT_A_MEMBER,
    PERMISSION_DENIED,
    NAME_TOO_LONG,
};

// Thread-safe room lifecycle manager.
class RoomManager {
public:
    RoomManager() = default;

    // Create a new room. Returns ROOM_ALREADY_EXISTS if name is taken.
    RoomError create(const std::string& name,
                     UserId             owner_id,
                     bool               is_private = false,
                     size_t             max_members = 32);

    // Join an existing room.
    RoomError join(RoomId room_id, UserId user_id);

    // Leave a room. If the owner leaves the room is dissolved.
    RoomError leave(RoomId room_id, UserId user_id);

    // Dissolve a room immediately (owner or server admin).
    RoomError dissolve(RoomId room_id, UserId requester_id);

    // Look up a room by ID or name.
    std::optional<Room> find(RoomId room_id) const;
    std::optional<Room> find_by_name(const std::string& name) const;

    // Returns all rooms a user belongs to.
    std::vector<RoomId> rooms_for_user(UserId user_id) const;

    // Snapshot of all public rooms for listing.
    std::vector<Room> public_rooms() const;

    // Check membership.
    bool is_member(RoomId room_id, UserId user_id) const;

private:
    mutable std::mutex mu_;
    RoomId             next_id_ = LOBBY_ROOM_ID + 1;
    std::unordered_map<RoomId, Room>      rooms_;
    std::unordered_map<std::string, RoomId> name_index_;
};

} // namespace server
} // namespace chatterbox

#endif // CHATTERBOX_SERVER_ROOM_MANAGER_HPP
