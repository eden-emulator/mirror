// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "web_service/announce_room_json.h"
#include "web_service/web_backend.h"

import jacinth;

namespace AnnounceMultiplayerRoom {

// TODO: rewrite this to use jacinth meta spec
static void to_json(jacinth::mutable_value json, const Room& room)
{
    json["port"] = room.information.port;
    json["name"] = room.information.name;
    if (!room.information.description.empty())
        json["description"] = room.information.description;
    json["preferredGameName"] = room.information.preferred_game.name;
    json["preferredGameId"] = room.information.preferred_game.id;
    json["maxPlayers"] = room.information.member_slots;
    json["netVersion"] = room.netVersion;
    json["hasPassword"] = room.hasPassword;
    if (!room.members.empty())
        json["players"] = room.members;
}

static void from_json(const jacinth::value& json, Room& room)
{
    room.verify_uid = json.get("externalGuid");
    room.ip = json.get("address");
    room.information.name = json.get("name");

    if (json["description"].is_null()) {
        room.information.description = "";
        LOG_DEBUG(Network, "Room '{}' doesn't contain a description", room.information.name);
    } else {
        room.information.description = json.get("description");
    }

    room.information.host_username = json.get("owner");
    room.information.port = json.get("port");
    room.information.preferred_game.name = json.get("preferredGameName");
    room.information.preferred_game.id = json.get("preferredGameId");
    room.information.member_slots = json.get("maxPlayers");
    room.netVersion = json.get("netVersion");
    room.hasPassword = json.get("hasPassword");

    if (json["players"].is_null())
        LOG_DEBUG(Network, "No players key");
    else
        room.members = json.get("players");
}

} // namespace AnnounceMultiplayerRoom

namespace WebService {

void RoomJson::SetRoomInformation(const std::string& name, const std::string& description,
                                  const u16 port, const u32 max_player, const u32 net_version,
                                  const bool has_password,
                                  const AnnounceMultiplayerRoom::GameInfo& preferred_game) {
    room.information.name = name;
    room.information.description = description;
    room.information.port = port;
    room.information.member_slots = max_player;
    room.netVersion = net_version;
    room.hasPassword = has_password;
    room.information.preferred_game = preferred_game;
}
void RoomJson::AddPlayer(const AnnounceMultiplayerRoom::Member& member) {
    room.members.push_back(member);
}

WebService::WebResult RoomJson::Update() {
    if (room_id.empty()) {
        LOG_ERROR(WebService, "Room must be registered to be updated");
        return WebService::WebResult{WebService::WebResult::Code::LibError,
                                     "Room is not registered", ""};
    }
    jacinth::json json;
    json["players"] = room.members;
    return client.PostJson(fmt::format("/lobby/{}", room_id), json.dump(), false);
}

WebService::WebResult RoomJson::Register() {
    jacinth::json json = room;
    auto result = client.PostJson("/lobby", json.dump(), false);
    if (result.result_code != WebService::WebResult::Code::Success) {
        return result;
    }

    auto reply_json = jacinth::json::read(result.returned_data);
    room = reply_json;
    room_id = reply_json.get("id");

    return WebService::WebResult{WebService::WebResult::Code::Success, "", room.verify_uid};
}

void RoomJson::ClearPlayers() {
    room.members.clear();
}

AnnounceMultiplayerRoom::RoomList RoomJson::GetRoomList() {
    auto reply = client.GetJson("/lobby", true).returned_data;
    if (reply.empty()) {
        return {};
    }
    return jacinth::json::read(reply)["rooms"];
}

void RoomJson::Delete() {
    if (room_id.empty()) {
        LOG_ERROR(WebService, "Room must be registered to be deleted");
    } else {
        // This jthread won't be destroyed until after the dtor has been ran
        // Once the thread finishes it will stay resident on the vector -- destroyed and freed by dtor()
        // this is still valid while in dtor, so... yeah
        detached_tasks.emplace_back([this](std::stop_token stop_token) {
            client.DeleteJson(fmt::format("/lobby/{}", room_id), "", false);
        });
    }
}

} // namespace WebService
