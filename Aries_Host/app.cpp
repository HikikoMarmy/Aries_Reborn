#include "app.h"
#include "ini_file.h"
#include "loaded_module.h"
#include "platform_util.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

static void copy_buffer( auto &buffer, const std::string &value )
{
    const auto count = std::min( buffer.size() - 1, value.size() );
    std::memcpy( buffer.data(), value.data(), count );
    buffer[count] = '\0';
}

static std::string now_string()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t( now );
    std::tm local{};
#ifdef _WIN32
    localtime_s( &local, &time );
#else
    localtime_r( &time, &local );
#endif
    std::ostringstream stream;
    stream << std::put_time( &local, "%Y-%m-%d %H:%M:%S" );
    return stream.str();
}

static const char *level_text( AriesLogLevel level )
{
    switch( level )
    {
        case ARIES_LOG_DEBUG: return "DEBUG";
        case ARIES_LOG_WARNING: return "WARN";
        case ARIES_LOG_ERROR: return "ERROR";
        case ARIES_LOG_RECV: return "RECV";
        case ARIES_LOG_SEND: return "SEND";
        default: return "INFO";
    }
}

static ImVec4 level_color( AriesLogLevel level )
{
    switch( level )
    {
        case ARIES_LOG_WARNING: return ImVec4( 0.84f, 0.73f, 0.49f, 1.0f );
        case ARIES_LOG_ERROR: return ImVec4( 0.96f, 0.28f, 0.28f, 1.0f );
        case ARIES_LOG_RECV: return ImVec4( 0.13f, 0.83f, 0.93f, 1.0f );
        case ARIES_LOG_SEND: return ImVec4( 0.66f, 0.54f, 1.0f, 1.0f );
        case ARIES_LOG_DEBUG: return ImVec4( 0.62f, 0.63f, 0.66f, 1.0f );
        default: return ImVec4( 0.86f, 0.87f, 0.89f, 1.0f );
    }
}

AriesApp::AriesApp()
    : module_manager_( aries_executable_directory() )
{
    discover_modules();
}

void AriesApp::render()
{
    for( auto &state : module_states_ ) update_module( state );
    tick_shutdown();

    const auto viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos( viewport->WorkPos );
    ImGui::SetNextWindowSize( viewport->WorkSize );
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin( "Aries Reborn###AriesRoot", nullptr, flags );
    render_toolbar();
    ImGui::Separator();
    render_module_tabs();
    ImGui::End();

    render_settings_popup();
    render_error_popup();
    render_shutdown_popup();
}

void AriesApp::request_close()
{
    if( shutdown_requested_ || exit_ready_ ) return;
    shutdown_requested_ = true;
    shutdown_started_ = std::chrono::steady_clock::now();
    for( auto &state : module_states_ )
    {
        if( state.session_active ) state.stop_expected = true;
    }
    module_manager_.request_stop_all();
}

bool AriesApp::should_exit() const
{
    return exit_ready_;
}

void AriesApp::discover_modules()
{
    std::vector<std::string> errors;
    module_manager_.discover_modules( errors );
    for( const auto &module : module_manager_.modules() ) add_module( module );
    if( false == errors.empty() )
    {
        std::string message;
        for( const auto &error : errors ) message += error + '\n';
        show_error( "Some modules could not be loaded", message );
    }
}

void AriesApp::add_module( const std::shared_ptr<LoadedModule> &module )
{
    ModuleUiState state;
    state.module = module;
    state.previous_state = module->status().state;
    load_module_settings( state );
    module_states_.push_back( std::move( state ) );
}

void AriesApp::render_toolbar()
{
    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.82f, 0.73f, 1.0f, 1.0f ) );
    ImGui::TextUnformatted( "Aries Reborn" );
    ImGui::PopStyleColor();

    const auto install_width = 130.0f;
    const auto settings_width = 92.0f;
    const auto total = install_width + settings_width + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine( std::max( ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - total - 14.0f ) );
    if( ImGui::Button( "Settings...", ImVec2( settings_width, 0.0f ) ) ) begin_settings();
    ImGui::SameLine();
    ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.08f, 0.40f, 0.46f, 1.0f ) );
    ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.10f, 0.53f, 0.60f, 1.0f ) );
    if( ImGui::Button( "Install Module...", ImVec2( install_width, 0.0f ) ) ) install_module();
    ImGui::PopStyleColor( 2 );
}

void AriesApp::render_module_tabs()
{
    if( module_states_.empty() )
    {
        ImGui::Spacing();
        ImGui::TextDisabled( "No modules are installed. Use Install Module... to add one." );
        return;
    }

    if( ImGui::BeginTabBar( "AriesModules", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll ) )
    {
        for( auto &state : module_states_ )
        {
            const auto label = std::string( state.module->info().display_name ) + "###" + state.module->info().id;
            const auto visible = ImGui::BeginTabItem( label.c_str() );
            const auto tab_min = ImGui::GetItemRectMin();
            const auto tab_max = ImGui::GetItemRectMax();
            if( state.unexpected_exit_alert )
            {
                const auto pulse = 0.45f + 0.55f * std::abs( std::sin( static_cast<float>( ImGui::GetTime() ) * 4.0f ) );
                const auto color = ImGui::GetColorU32( ImVec4( 0.96f, 0.18f, 0.18f, pulse ) );
                ImGui::GetWindowDrawList()->AddLine( ImVec2( tab_min.x + 4.0f, tab_max.y - 1.0f ), ImVec2( tab_max.x - 4.0f, tab_max.y - 1.0f ), color, 3.0f );
            }

            if( visible )
            {
                state.unexpected_exit_alert = false;
                render_module_panel( state );
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
}

void AriesApp::render_module_panel( ModuleUiState &state )
{
    const auto status = state.module->status();
    ImVec4 state_color( 0.62f, 0.63f, 0.66f, 1.0f );
    if( status.state == ARIES_MODULE_STATE_RUNNING ) state_color = ImVec4( 0.13f, 0.83f, 0.93f, 1.0f );
    else if( status.state == ARIES_MODULE_STATE_STARTING ) state_color = ImVec4( 0.66f, 0.54f, 1.0f, 1.0f );
    else if( status.state == ARIES_MODULE_STATE_STOPPING ) state_color = ImVec4( 0.84f, 0.73f, 0.49f, 1.0f );
    else if( status.state == ARIES_MODULE_STATE_FAULTED ) state_color = ImVec4( 0.96f, 0.28f, 0.28f, 1.0f );

    ImGui::Spacing();
    ImGui::TextColored( state_color, "%s", state_text( status.state ) );
    ImGui::SameLine();
    ImGui::TextDisabled( "%s", status.message );
    ImGui::Spacing();

    const auto editable = status.state == ARIES_MODULE_STATE_STOPPED || status.state == ARIES_MODULE_STATE_FAULTED;
    ImGui::BeginDisabled( false == editable );
    ImGui::SetNextItemWidth( 320.0f );
    ImGui::InputText( "Bind IP", state.bind_ip.data(), state.bind_ip.size() );
    ImGui::SetNextItemWidth( 320.0f );
    ImGui::InputText( "Service IP", state.service_ip.data(), state.service_ip.size() );
    if( state.module->config_section() == "demangler" )
    {
        ImGui::SetNextItemWidth( 320.0f );
        ImGui::InputText( "Ports", state.ports.data(), state.ports.size() );
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    if( status.state == ARIES_MODULE_STATE_STOPPED || status.state == ARIES_MODULE_STATE_FAULTED )
    {
        if( ImGui::Button( "Start", ImVec2( 90.0f, 0.0f ) ) )
        {
            save_module_settings( state );
            std::string error;
            if( false == state.module->start( state.bind_ip.data(), state.service_ip.data(), error ) ) show_error( "Unable to start module", error );
            else
            {
                state.session_active = true;
                state.stop_expected = false;
                state.unexpected_exit_alert = false;
            }
        }
    }
    else if( status.state == ARIES_MODULE_STATE_STARTING || status.state == ARIES_MODULE_STATE_RUNNING )
    {
        if( ImGui::Button( "Stop", ImVec2( 90.0f, 0.0f ) ) )
        {
            state.stop_expected = true;
            state.module->request_stop();
        }
    }
    else
    {
        ImGui::BeginDisabled();
        ImGui::Button( "Stopping...", ImVec2( 90.0f, 0.0f ) );
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled( status.state == ARIES_MODULE_STATE_STOPPING );
    if( ImGui::Button( "Replace Module..." ) ) replace_module( state );
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox( "Auto-scroll", &state.auto_scroll );

    ImGui::Spacing();
    ImGui::TextDisabled( "Log" );
    const auto child_size = ImVec2( 0.0f, std::max( 180.0f, ImGui::GetContentRegionAvail().y ) );
    if( ImGui::BeginChild( "ModuleLog", child_size, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar ) )
    {
        for( const auto &line : state.logs ) ImGui::TextColored( level_color( line.level ), "%s", line.text.c_str() );
        if( state.auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f ) ImGui::SetScrollHereY( 1.0f );
    }
    ImGui::EndChild();
}

void AriesApp::render_settings_popup()
{
    if( settings_requested_ )
    {
        ImGui::OpenPopup( "Aries Reborn Settings" );
        settings_requested_ = false;
    }

    if( ImGui::BeginPopupModal( "Aries Reborn Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        ImGui::SetNextItemWidth( 420.0f );
        ImGui::InputText( "Database", database_path_.data(), database_path_.size() );
        ImGui::SetNextItemWidth( 420.0f );
        ImGui::InputText( "Log directory", log_directory_.data(), log_directory_.size() );
        ImGui::TextDisabled( "Database changes apply after modules restart." );
        if( ImGui::Button( "Save", ImVec2( 90.0f, 0.0f ) ) )
        {
            IniFile ini;
            ini.load( module_manager_.config_path() );
            ini.set( "global", "database_path", database_path_.data() );
            ini.set( "global", "log_directory", log_directory_.data() );
            if( false == ini.save( module_manager_.config_path() ) ) show_error( "Unable to save settings", "Could not write aries.ini" );
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if( ImGui::Button( "Cancel", ImVec2( 90.0f, 0.0f ) ) ) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void AriesApp::render_error_popup()
{
    if( error_title_.empty() && false == error_requested_ ) return;
    if( error_requested_ )
    {
        ImGui::OpenPopup( error_title_.c_str() );
        error_requested_ = false;
    }

    if( ImGui::BeginPopupModal( error_title_.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        ImGui::TextWrapped( "%s", error_message_.c_str() );
        ImGui::Spacing();
        if( ImGui::Button( "OK", ImVec2( 90.0f, 0.0f ) ) ) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void AriesApp::render_shutdown_popup()
{
    if( false == shutdown_requested_ || exit_ready_ ) return;
    ImGui::OpenPopup( "Stopping servers" );
    if( ImGui::BeginPopupModal( "Stopping servers", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        ImGui::TextUnformatted( "Waiting for modules to report Stopped..." );
        ImGui::EndPopup();
    }
}

void AriesApp::update_module( ModuleUiState &state )
{
    std::string maintenance_error;
    state.module->perform_maintenance( maintenance_error );
    if( false == maintenance_error.empty() ) state.logs.push_back( { ARIES_LOG_ERROR, "[MODULE] " + maintenance_error } );
    append_logs( state );

    const auto current = state.module->status().state;
    if( current == ARIES_MODULE_STATE_STARTING || current == ARIES_MODULE_STATE_RUNNING ) state.session_active = true;
    else if( state.session_active && ( current == ARIES_MODULE_STATE_STOPPED || current == ARIES_MODULE_STATE_FAULTED ) )
    {
        if( false == state.stop_expected ) state.unexpected_exit_alert = true;
        state.session_active = false;
        state.stop_expected = false;
    }
    state.previous_state = current;
}

void AriesApp::append_logs( ModuleUiState &state )
{
    const auto entries = state.module->take_logs();
    if( entries.empty() ) return;

    std::ofstream file( state.module->log_file_path(), std::ios::app );
    for( const auto &entry : entries )
    {
        const auto line = '[' + now_string() + "] [" + level_text( entry.level ) + "] " + entry.message;
        state.logs.push_back( { entry.level, line } );
        if( file.is_open() ) file << line << '\n';
    }
    constexpr size_t max_lines = 5000;
    if( state.logs.size() > max_lines ) state.logs.erase( state.logs.begin(), state.logs.begin() + static_cast<std::ptrdiff_t>( state.logs.size() - max_lines ) );
}

void AriesApp::load_module_settings( ModuleUiState &state )
{
    IniFile ini;
    ini.load( module_manager_.config_path() );
    const auto section = state.module->config_section();
    copy_buffer( state.bind_ip, ini.get( section, "bind_ip", "0.0.0.0" ) );
    copy_buffer( state.service_ip, ini.get( section, "service_ip", "" ) );
    copy_buffer( state.ports, ini.get( section, "ports", "3658" ) );
}

void AriesApp::save_module_settings( const ModuleUiState &state )
{
    IniFile ini;
    ini.load( module_manager_.config_path() );
    const auto section = state.module->config_section();
    ini.set( section, "bind_ip", state.bind_ip.data() );
    ini.set( section, "service_ip", state.service_ip.data() );
    if( section == "demangler" ) ini.set( section, "ports", state.ports.data() );
    ini.save( module_manager_.config_path() );
}

void AriesApp::install_module()
{
    const auto path = aries_open_module_file_dialog();
    if( path.empty() ) return;
    std::string error;
    auto module = module_manager_.install_module( path, error );
    if( module == nullptr ) show_error( "Unable to install module", error );
    else add_module( module );
}

void AriesApp::replace_module( ModuleUiState &state )
{
    const auto path = aries_open_module_file_dialog();
    if( path.empty() ) return;
    const auto before = state.module->status().state;
    std::string error;
    if( false == state.module->request_replacement( path, error ) )
    {
        show_error( "Unable to replace module", error );
        return;
    }
    if( before == ARIES_MODULE_STATE_STARTING || before == ARIES_MODULE_STATE_RUNNING || before == ARIES_MODULE_STATE_STOPPING ) state.stop_expected = true;
}

void AriesApp::begin_settings()
{
    IniFile ini;
    ini.load( module_manager_.config_path() );
    copy_buffer( database_path_, ini.get( "global", "database_path", "data/aries.db" ) );
    copy_buffer( log_directory_, ini.get( "global", "log_directory", "logs" ) );
    settings_requested_ = true;
}

void AriesApp::tick_shutdown()
{
    if( false == shutdown_requested_ || exit_ready_ ) return;
    if( module_manager_.all_stopped() )
    {
        std::string error;
        if( module_manager_.unload_all( error ) ) exit_ready_ = true;
        else
        {
            shutdown_requested_ = false;
            show_error( "Unable to unload module", error );
        }
        return;
    }

    if( std::chrono::steady_clock::now() - shutdown_started_ > std::chrono::seconds( 15 ) )
    {
        shutdown_requested_ = false;
        show_error( "Servers are still stopping", "One or more modules have not reported Stopped. Aries will keep running rather than detach a live module." );
    }
}

void AriesApp::show_error( std::string title, std::string message )
{
    error_title_ = std::move( title );
    error_message_ = std::move( message );
    error_requested_ = true;
}

const char *AriesApp::state_text( AriesModuleState state )
{
    switch( state )
    {
        case ARIES_MODULE_STATE_STARTING: return "Starting";
        case ARIES_MODULE_STATE_RUNNING: return "Running";
        case ARIES_MODULE_STATE_STOPPING: return "Stopping";
        case ARIES_MODULE_STATE_FAULTED: return "Faulted";
        default: return "Stopped";
    }
}
