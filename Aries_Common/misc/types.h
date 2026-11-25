#pragma once

#include <cstdint>
#include <memory>

struct aries_socket;
class aries_packet;
class aries_client;
class CSocketManager;
class AriesClientManager;
class CConfiguration;
class CLogManager;
class AriesDatabase;
class AriesGameProtocol;

using sptr_socket = std::shared_ptr<aries_socket>;
using sptr_packet = std::shared_ptr<aries_packet>;
using ptr_client = std::shared_ptr<aries_client>;
