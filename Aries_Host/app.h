#pragma once

#include "module_manager.h"

#include <aries_module_api.h>

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

class LoadedModule;

struct ModuleLogLine
{
    AriesLogLevel level{ ARIES_LOG_INFO };
    std::string text;
};

struct ModuleUiState
{
    std::shared_ptr<LoadedModule> module;
    std::array<char, 64> bind_ip{};
    std::array<char, 256> service_ip{};
    std::array<char, 128> ports{};
    std::vector<ModuleLogLine> logs;
    AriesModuleState previous_state{ ARIES_MODULE_STATE_STOPPED };
    bool session_active{ false };
    bool stop_expected{ false };
    bool unexpected_exit_alert{ false };
    bool auto_scroll{ true };
};

class AriesApp
{
public:
    AriesApp();

    void render();
    void request_close();
    bool should_exit() const;

private:
    void discover_modules();
    void add_module( const std::shared_ptr<LoadedModule> &module );
    void render_toolbar();
    void render_module_tabs();
    void render_module_panel( ModuleUiState &state );
    void render_settings_popup();
    void render_error_popup();
    void render_shutdown_popup();
    void update_module( ModuleUiState &state );
    void append_logs( ModuleUiState &state );
    void load_module_settings( ModuleUiState &state );
    void save_module_settings( const ModuleUiState &state );
    void install_module();
    void replace_module( ModuleUiState &state );
    void begin_settings();
    void tick_shutdown();
    void show_error( std::string title, std::string message );
    static const char *state_text( AriesModuleState state );

    ModuleManager module_manager_;
    std::vector<ModuleUiState> module_states_;
    bool settings_requested_{ false };
    bool error_requested_{ false };
    bool shutdown_requested_{ false };
    bool exit_ready_{ false };
    std::string error_title_;
    std::string error_message_;
    std::array<char, 512> database_path_{};
    std::array<char, 512> log_directory_{};
    std::chrono::steady_clock::time_point shutdown_started_{};
};
