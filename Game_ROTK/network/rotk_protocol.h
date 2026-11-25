#pragma once

#include <network/game_protocol.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class ROTKProtocol final : public AriesGameProtocol
{
public:
    ROTKProtocol();

    const aries_game_metadata &metadata() const override;
    bool handle_lobby_packet( ptr_client &client, sptr_packet &packet ) override;
    bool handle_buddy_packet( ptr_client &client, sptr_packet &packet ) override;
    void lobby_disconnected( ptr_client &client ) override;
    void buddy_disconnected( ptr_client &client ) override;
    void persona_selected( ptr_client &client ) override;

private:
    struct rotk_room_state
    {
        uint32_t id{ 0 };
        std::string name;
        std::string description;
        std::string password;
        uint32_t max_players{ 4 };
        bool persistent{ false };
    };

    struct rotk_match_parameters
    {
        uint32_t stage{ 0 };
        uint32_t character{ 0 };
        std::vector<uint32_t> character_candidates;
        uint32_t voip{ 0 };
        uint32_t shot{ 10 };
        uint32_t mode_flags{ 0x2 };
        uint32_t challenge_flags{ 0x08012464 };
    };

    struct rotk_quick_match_request
    {
        rotk_match_parameters parameters;
    };

    struct rotk_pending_challenge
    {
        std::string self;
        std::string host;
        std::string opponent;
        std::string remote_address;
        std::string local_address;
        uint16_t remote_port{ 0 };
        uint16_t local_port{ 0 };
        std::string auth;
        std::string session;
        rotk_match_parameters parameters;
        uint32_t seed{ 0 };
        uint32_t epoch{ 0 };
    };

    bool handle_shared_packet( ptr_client &client, sptr_packet &packet );
    void select( ptr_client &client, sptr_packet &packet );
    void room( ptr_client &client, sptr_packet &packet );
    void move( ptr_client &client, sptr_packet &packet );
    void peek( ptr_client &client, sptr_packet &packet );
    void send_lobby_snapshot( ptr_client &client );
    void send_lobby_room( ptr_client &client, const rotk_room_state &room );
    void broadcast_lobby_room( const rotk_room_state &room );
    void send_lobby_room_remove( ptr_client &client, const rotk_room_state &room );
    void broadcast_lobby_room_remove( const rotk_room_state &room );
    void send_room_user( ptr_client &client, const ptr_client &user );
    void send_room_user_remove( ptr_client &client, const ptr_client &user );
    void send_room_user_clear( ptr_client &client );
    void send_room_user_snapshot( ptr_client &client, const std::string &room_name );
    std::vector<ptr_client> room_members( const std::string &room_name ) const;
    void broadcast_room_user_snapshot( const std::string &room_name );
    void broadcast_room_user_remove( const std::string &room_name, const ptr_client &user );
    void send_room_log_message( ptr_client &client, const std::string &actor, const std::string &message );
    void broadcast_room_log_message( const std::string &room_name, const std::string &actor, const std::string &message );
    void send_room_population( ptr_client &client, const std::string &room_name );
    void broadcast_room_population( const std::string &room_name );
    void create_system_rooms();
    bool get_room_state( const std::string &room_name, rotk_room_state &room );
    std::vector<rotk_room_state> get_room_snapshot();
    uint32_t room_population( const std::string &room_name ) const;
    void leave_room( ptr_client &client );
    void remove_client_room_state( const ptr_client &client );
    void remove_empty_custom_rooms();
    void online_stats( ptr_client &client, sptr_packet &packet );
    void ranking_results( ptr_client &client, sptr_packet &packet );
    void custom_user( ptr_client &client, sptr_packet &packet );
    void quick_match( ptr_client &client, sptr_packet &packet );
    void quick_match_kind( ptr_client &client, sptr_packet &packet );
    void challenge( ptr_client &client, sptr_packet &packet );
    void message( ptr_client &client, sptr_packet &packet );
    void send_quick_match_session( ptr_client &host, ptr_client &opponent );
    void send_pending_challenge( ptr_client &client, const rotk_pending_challenge &challenge );
    rotk_match_parameters make_match_parameters( sptr_packet &packet ) const;
    void apply_embedded_match_parameters( rotk_match_parameters &parameters, const std::string &text ) const;
    void forget_client( const ptr_client &client );

    aries_game_metadata metadata_;
    std::atomic<uint32_t> next_room_id_{ 1 };
    std::atomic<uint32_t> match_sequence_{ 1 };
    mutable std::mutex room_mutex_;
    std::map<std::string, rotk_room_state> rooms_;
    std::map<int64_t, rotk_quick_match_request> match_requests_;
    std::map<int64_t, rotk_pending_challenge> pending_challenges_;
    std::map<std::string, uint32_t> recent_session_starts_;
};
