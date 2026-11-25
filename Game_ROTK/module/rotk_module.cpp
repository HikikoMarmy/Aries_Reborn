#include <aries_module_api.h>
#include <module/game_module_runtime.h>
#include <module/module_util.h>

#include "../network/rotk_protocol.h"

#include <memory>

ARIES_MODULE_EXPORT uint32_t AriesModule_GetApiVersion()
{
    return ARIES_MODULE_API_VERSION;
}

ARIES_MODULE_EXPORT bool AriesModule_GetInfo( AriesModuleInfo *info )
{
    if( info == nullptr || info->struct_size != sizeof( AriesModuleInfo ) ) return false;
    info->kind = ARIES_MODULE_KIND_GAME_SERVER;
    AriesModuleCopyText( info->id, sizeof( info->id ), "rotk" );
    AriesModuleCopyText( info->display_name, sizeof( info->display_name ), "ROTK" );
    AriesModuleCopyText( info->description, sizeof( info->description ), "The Lord of the Rings: The Return of the King server" );
    AriesModuleCopyText( info->default_config, sizeof( info->default_config ), "aries.ini" );
    return true;
}

ARIES_MODULE_EXPORT AriesModuleHandle AriesModule_Create( const AriesHostApi *host )
{
    return new AriesGameModuleRuntime(
        host,
        "rotk",
        "ROTK",
        "aries.ini",
        11200,
        11201,
        []() -> std::unique_ptr<AriesGameProtocol>
        {
            return std::make_unique<ROTKProtocol>();
        } );
}

ARIES_MODULE_EXPORT bool AriesModule_Start( AriesModuleHandle module, const AriesModuleStartArgs *args )
{
    if( module == nullptr ) return false;
    return static_cast<AriesGameModuleRuntime *>( module )->start( args );
}

ARIES_MODULE_EXPORT void AriesModule_RequestStop( AriesModuleHandle module )
{
    if( module == nullptr ) return;
    static_cast<AriesGameModuleRuntime *>( module )->request_stop();
}

ARIES_MODULE_EXPORT bool AriesModule_GetStatus( AriesModuleHandle module, AriesModuleStatus *status )
{
    if( module == nullptr ) return false;
    return static_cast<AriesGameModuleRuntime *>( module )->get_status( status );
}

ARIES_MODULE_EXPORT void AriesModule_Destroy( AriesModuleHandle module )
{
    delete static_cast<AriesGameModuleRuntime *>( module );
}
