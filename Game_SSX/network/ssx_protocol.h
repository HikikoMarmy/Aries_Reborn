#pragma once

#include <network/game_protocol.h>

class SSXProtocol final : public AriesGameProtocol
{
public:
    SSXProtocol();

    const aries_game_metadata &metadata() const override;
    bool handle_lobby_packet( ptr_client &client, sptr_packet &packet ) override;

private:
    void news( ptr_client &client, sptr_packet &packet );

    aries_game_metadata metadata_;
};
