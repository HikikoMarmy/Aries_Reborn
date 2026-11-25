#include "stdafx.h"
#include "global_define.h"

std::unique_ptr<CConfiguration> g_configuration;
std::unique_ptr<CSocketManager> g_socketMng;
std::unique_ptr<AriesClientManager> g_clientMng;
std::unique_ptr<AriesDatabase> g_database;
AriesGameProtocol *g_gameProtocol{ nullptr };
CLogManager logging;
std::atomic<bool> g_shutdown_requested{ false };
