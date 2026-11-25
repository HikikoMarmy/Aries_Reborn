#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define ARIES_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define ARIES_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

static constexpr uint32_t ARIES_MODULE_API_VERSION = 1;

enum AriesModuleKind : uint32_t
{
    ARIES_MODULE_KIND_GAME_SERVER = 1,
    ARIES_MODULE_KIND_SERVICE = 2
};

enum AriesModuleState : uint32_t
{
    ARIES_MODULE_STATE_STOPPED = 0,
    ARIES_MODULE_STATE_STARTING = 1,
    ARIES_MODULE_STATE_RUNNING = 2,
    ARIES_MODULE_STATE_STOPPING = 3,
    ARIES_MODULE_STATE_FAULTED = 4
};

enum AriesLogLevel : uint32_t
{
    ARIES_LOG_INFO = 0,
    ARIES_LOG_DEBUG = 1,
    ARIES_LOG_WARNING = 2,
    ARIES_LOG_ERROR = 3,
    ARIES_LOG_RECV = 4,
    ARIES_LOG_SEND = 5
};

struct AriesHostApi
{
    uint32_t struct_size{ sizeof( AriesHostApi ) };
    uint32_t api_version{ ARIES_MODULE_API_VERSION };
    void *context{ nullptr };
    void ( *log )( void *context, const char *module_id, AriesLogLevel level, const char *message ){ nullptr };
    void *reserved[8]{};
};

struct AriesModuleInfo
{
    uint32_t struct_size{ sizeof( AriesModuleInfo ) };
    uint32_t kind{ 0 };
    char id[64]{};
    char display_name[128]{};
    char description[256]{};
    char default_config[128]{};
};

struct AriesModuleStartArgs
{
    uint32_t struct_size{ sizeof( AriesModuleStartArgs ) };
    char bind_ip[64]{};
    char service_ip[256]{};
    char config_path[260]{};
};

struct AriesModuleStatus
{
    uint32_t struct_size{ sizeof( AriesModuleStatus ) };
    AriesModuleState state{ ARIES_MODULE_STATE_STOPPED };
    char message[256]{};
};

using AriesModuleHandle = void *;

using PFN_AriesModule_GetApiVersion = uint32_t ( * )();
using PFN_AriesModule_GetInfo = bool ( * )( AriesModuleInfo *info );
using PFN_AriesModule_Create = AriesModuleHandle ( * )( const AriesHostApi *host );
using PFN_AriesModule_Start = bool ( * )( AriesModuleHandle module, const AriesModuleStartArgs *args );
using PFN_AriesModule_RequestStop = void ( * )( AriesModuleHandle module );
using PFN_AriesModule_GetStatus = bool ( * )( AriesModuleHandle module, AriesModuleStatus *status );
using PFN_AriesModule_Destroy = void ( * )( AriesModuleHandle module );
