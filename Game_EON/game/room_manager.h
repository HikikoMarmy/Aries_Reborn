#pragma once

#include <misc/types.h>
#include "room.h"

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class EONRoomManager
{
public:
    EONRoomManager();

    ptr_room create_room( ptr_client &client, const std::string &room_name, const std::string &description, const std::string &password, uint32_t max_players );
    bool get_room( const std::string &name, ptr_room &room );
    bool join_room( ptr_client &client, const std::string &name, const std::string &password, ptr_room &room );
    std::vector<ptr_room> get_rooms();
    void leave_room( ptr_client &client );
    void remove_client( ptr_client &client );

private:
    void create_system_rooms();
    void remove_empty_rooms();

    std::atomic<uint32_t> next_room_id_{ 1000 };
    std::map<std::string, ptr_room> rooms_;
    std::mutex mutex_;
};
