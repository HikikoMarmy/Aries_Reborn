#pragma once

#include "../game/room_manager.h"

#include <network/game_protocol.h>

#include <atomic>
#include <map>
#include <string>
#include <vector>

class EONProtocol final : public AriesGameProtocol
{
public:
    EONProtocol();

    const aries_game_metadata &metadata() const override;
    bool handle_lobby_packet( ptr_client &client, sptr_packet &packet ) override;
    void lobby_disconnected( ptr_client &client ) override;
    void persona_selected( ptr_client &client ) override;

private:
    struct eon_quick_match_request
    {
        uint32_t client_id{ 0 };
        std::string criteria;
    };

    struct eon_pending_invite
    {
        int64_t inviter_key{ 0 };
        int64_t target_key{ 0 };
        std::string inviter;
        std::string target;
        uint32_t game_type{ 0 };
        uint32_t inviter_character{ 0 };
        uint32_t target_character{ 0 };
        bool inviter_ready{ false };
        bool target_ready{ false };
    };

    void select( ptr_client &client, sptr_packet &packet );
    void room( ptr_client &client, sptr_packet &packet );
    void move( ptr_client &client, sptr_packet &packet );
    void peek( ptr_client &client, sptr_packet &packet );
    void quick_match( ptr_client &client, sptr_packet &packet );
    void message( ptr_client &client, sptr_packet &packet );
    void challenge( ptr_client &client, sptr_packet &packet );
    void custom_user( ptr_client &client, sptr_packet &packet );
    void online_stats( ptr_client &client, sptr_packet &packet );
    void send_room_snapshot( ptr_client &client );
    void send_room_update( ptr_client &client, const ptr_room &room );
    void send_room_user( ptr_client &client, const ptr_client &user );
    void send_room_user_remove( ptr_client &client, const ptr_client &user );
    void send_room_user_clear( ptr_client &client );
    void send_room_user_snapshot( ptr_client &client, const std::string &room_name );
    void broadcast_room_user_snapshot( const std::string &room_name );
    void broadcast_room_user_remove( const std::string &room_name, const ptr_client &user );
    void send_room_population( ptr_client &client, const std::string &room_name );
    void broadcast_room_population( const std::string &room_name );
    std::vector<ptr_client> room_members( const std::string &room_name ) const;
    void send_quick_match_session( ptr_client &host, ptr_client &opponent );
    void send_invite_session( ptr_client &host, ptr_client &opponent, const eon_pending_invite &invite );
    void forget_quick_match_request( const ptr_client &client );
    void forget_invites( const ptr_client &client );

    aries_game_metadata metadata_;
    EONRoomManager room_manager_;
    std::atomic<uint32_t> match_sequence_{ 1 };
    std::map<int64_t, eon_quick_match_request> quick_match_requests_;
    std::map<std::pair<int64_t, int64_t>, eon_pending_invite> pending_invites_;
};
