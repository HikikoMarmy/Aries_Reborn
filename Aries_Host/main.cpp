#include "app.h"
#include "theme.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#ifdef _WIN32
#include <windows.h>
#endif

static int run_aries()
{
    SDL_SetMainReady();
    if( false == SDL_Init( SDL_INIT_VIDEO ) ) return 1;

    SDL_Window *window = SDL_CreateWindow( "Aries Reborn", 980, 680, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY );
    if( window == nullptr )
    {
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer( window, nullptr );
    if( renderer == nullptr )
    {
        SDL_DestroyWindow( window );
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    apply_aries_theme();

    ImGui_ImplSDL3_InitForSDLRenderer( window, renderer );
    ImGui_ImplSDLRenderer3_Init( renderer );

    AriesApp app;
    while( false == app.should_exit() )
    {
        SDL_Event event;
        while( SDL_PollEvent( &event ) )
        {
            ImGui_ImplSDL3_ProcessEvent( &event );
            if( event.type == SDL_EVENT_QUIT ) app.request_close();
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        app.render();
        ImGui::Render();

        SDL_SetRenderDrawColor( renderer, 18, 19, 22, 255 );
        SDL_RenderClear( renderer );
        ImGui_ImplSDLRenderer3_RenderDrawData( ImGui::GetDrawData(), renderer );
        SDL_RenderPresent( renderer );
        SDL_Delay( 50 );
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer( renderer );
    SDL_DestroyWindow( window );
    SDL_Quit();
    return 0;
}

#ifdef _WIN32
int WINAPI WinMain( HINSTANCE, HINSTANCE, LPSTR, int )
{
    return run_aries();
}
#else
int main( int, char ** )
{
    return run_aries();
}
#endif
