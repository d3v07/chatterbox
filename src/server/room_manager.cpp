#include <chatterbox/server/room_manager.hpp>
#include <algorithm>

namespace chatterbox {
namespace server {

RoomError RoomManager::create(const std::string& name,
                               UserId             owner_id,
                               bool               is_private,
                               size_t             max_members) {
    if (name.size() > MAX_ROOM_NAME_LENGTH) return RoomError::NAME_TOO_LONG;

    std::lock_guard<std::mutex> lock(mu_);
    if (name_index_.count(name)) return RoomError::ROOM_ALREADY_EXISTS;

    RoomId id = next_id_++;
    Room r;
    r.id          = id;
    r.name        = name;
    r.owner_id    = owner_id;
    r.is_private  = is_private;
    r.max_members = max_members;
    r.created_at  = Clock::now();
    r.members.insert(owner_id);

    name_index_[name] = id;
    rooms_[id] = std::move(r);
    return RoomError::OK;
}

RoomError RoomManager::join(RoomId room_id, UserId user_id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return RoomError::ROOM_NOT_FOUND;
    Room& r = it->second;
    if (r.members.size() >= r.max_members) return RoomError::ROOM_FULL;
    r.members.insert(user_id);
    return RoomError::OK;
}

RoomError RoomManager::leave(RoomId room_id, UserId user_id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return RoomError::ROOM_NOT_FOUND;
    Room& r = it->second;
    if (!r.members.count(user_id)) return RoomError::NOT_A_MEMBER;
    r.members.erase(user_id);
    // Dissolve if owner leaves or room is empty
    if (r.members.empty() || r.owner_id == user_id) {
        name_index_.erase(r.name);
        rooms_.erase(it);
    }
    return RoomError::OK;
}

RoomError RoomManager::dissolve(RoomId room_id, UserId requester_id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return RoomError::ROOM_NOT_FOUND;
    if (it->second.owner_id != requester_id &&
        requester_id != SERVER_USER_ID) {
        return RoomError::PERMISSION_DENIED;
    }
    name_index_.erase(it->second.name);
    rooms_.erase(it);
    return RoomError::OK;
}

std::optional<Room> RoomManager::find(RoomId room_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return std::nullopt;
    return it->second;
}

std::optional<Room> RoomManager::find_by_name(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = name_index_.find(name);
    if (it == name_index_.end()) return std::nullopt;
    auto rit = rooms_.find(it->second);
    if (rit == rooms_.end()) return std::nullopt;
    return rit->second;
}

std::vector<RoomId> RoomManager::rooms_for_user(UserId user_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<RoomId> result;
    for (const auto& [id, room] : rooms_) {
        if (room.members.count(user_id)) result.push_back(id);
    }
    return result;
}

std::vector<Room> RoomManager::public_rooms() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Room> result;
    for (const auto& [id, room] : rooms_) {
        if (!room.is_private) result.push_back(room);
    }
    return result;
}

bool RoomManager::is_member(RoomId room_id, UserId user_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return false;
    return it->second.members.count(user_id) > 0;
}

} // namespace server
} // namespace chatterbox
