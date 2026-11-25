#pragma once

#include <aries_module_api.h>
#include <server/server.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class AriesGameProtocol;

class AriesGameModuleRuntime
{
public:
    using protocol_factory = std::function<std::unique_ptr<AriesGameProtocol>()>;

    AriesGameModuleRuntime(
        const AriesHostApi *host,
        std::string module_id,
        std::string display_name,
        std::string default_config,
        uint16_t lobby_port,
        uint16_t buddy_port,
        protocol_factory factory );
    ~AriesGameModuleRuntime();

    bool start( const AriesModuleStartArgs *args );
    void request_stop();
    bool get_status( AriesModuleStatus *status ) const;

private:
    static void started_callback( void *context );
    void worker_main( AriesModuleStartArgs args );
    void set_state( AriesModuleState state, const std::string &message );
    void install_log_sink();
    void emit_log( const wchar_t *level, const std::wstring &message ) const;
    static std::string wide_to_utf8( const std::wstring &value );
    static AriesLogLevel map_log_level( const wchar_t *level );

    AriesHostApi host_{};
    std::string module_id_;
    std::string display_name_;
    std::string default_config_;
    uint16_t lobby_port_{ 0 };
    uint16_t buddy_port_{ 0 };
    protocol_factory factory_;
    std::atomic<AriesModuleState> state_{ ARIES_MODULE_STATE_STOPPED };
    std::atomic<bool> stop_requested_{ false };
    mutable std::mutex status_mutex_;
    std::string status_message_{ "Stopped" };
    std::thread worker_;
};
