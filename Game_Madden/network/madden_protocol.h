#pragma once

#include <network/game_protocol.h>

class MaddenProtocol final : public AriesGameProtocol
{
public:
    MaddenProtocol();

    const aries_game_metadata &metadata() const override;
    bool handle_lobby_packet( ptr_client &client, sptr_packet &packet ) override;

private:
    void authenticate( ptr_client &client, sptr_packet &packet );
    void account( ptr_client &client, sptr_packet &packet );
    void create_persona( ptr_client &client, sptr_packet &packet );
    void persona( ptr_client &client, sptr_packet &packet );
    void news( ptr_client &client, sptr_packet &packet );

    aries_game_metadata metadata_;
};
