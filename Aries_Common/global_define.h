#pragma once

#include "configuration.h"
#include "database/database.h"
#include "game/client_manager.h"
#include "network/socket_manager.h"
#include "ui/logging.h"

extern std::unique_ptr<CConfiguration> g_configuration;
extern std::unique_ptr<CSocketManager> g_socketMng;
extern std::unique_ptr<AriesClientManager> g_clientMng;
extern std::unique_ptr<AriesDatabase> g_database;
extern AriesGameProtocol *g_gameProtocol;
extern CLogManager logging;
extern std::atomic<bool> g_shutdown_requested;
