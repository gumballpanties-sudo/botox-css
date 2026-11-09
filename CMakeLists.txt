cmake_minimum_required(VERSION 3.16)
project(botox)

# C++20: the aim tree (src/hacks/aim) uses std::format, designated initialisers,
# concepts and <ranges>. botox's own sources are C++17-clean and build fine at 20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/src/dependencies/imgui)
set(MH_DIR    ${CMAKE_SOURCE_DIR}/src/dependencies/minhook)

# ==== aimbot subsystem =======================================================================
# src/hacks/aim is the aim half of the DLL: its own engine SDK layer, hook plumbing and the aimbot
# feature tree. It includes botox's single copy of ImGui and MinHook directly -- two ImGui
# builds in one DLL would be an ODR violation.
set(AIM_DIR ${CMAKE_SOURCE_DIR}/src/hacks/aim)

# The whole subsystem: the engine layer (defs/sdk/helpers/utils), the aim features, every
# engine hook (hooks.cpp) and the entry point. Each MAKE_HOOK in hooks.cpp registers itself at
# static-init time; CHooks::Initialize's whitelist (aim.cpp) decides which ones are installed,
# so none of them can collide with a hook botox already owns.
file(GLOB AIM_SRC CONFIGURE_DEPENDS ${AIM_DIR}/*.cpp)
# project-local core-only freetype (no png/brotli/bz2/zlib) -- global vcpkg untouched
set(FREETYPE_STATIC_DIR "${CMAKE_SOURCE_DIR}/vcpkg_local/x64-windows-static")

add_library(botox SHARED
    src/dllmain.cpp
    src/hooks/netvars.cpp
    src/hacks/visuals/visuals.cpp
    src/hacks/visuals/lagres.cpp
    src/render/render.cpp
    src/render/dpi.cpp
    src/hacks/screen/colorcorrect.cpp
    src/hacks/screen/bloom.cpp
    src/hacks/screen/screenfx.cpp
    src/hacks/screen/frame_dump.cpp
    src/hacks/screen/resolution_spoof.cpp
    src/hacks/visuals/entity_cache.cpp
    src/hacks/web/websurface.cpp
    src/hacks/web/webview2host.cpp
    src/hacks/botoxnet/botoxnet.cpp
    src/hacks/screen/ssao.cpp
    src/hacks/screen/dof.cpp
    src/hacks/screen/frameblend.cpp
    src/hacks/hud/killfeed.cpp
    src/hacks/hud/crosshair.cpp
    src/hacks/hud/watermark.cpp
    src/hacks/hud/watermark_styles.cpp
    src/hacks/misc/boot_screen.cpp
    src/hacks/hud/mediaplayer.cpp
    src/hacks/misc/unitE_events.cpp
    src/hacks/hud/hitmarker.cpp
    src/hacks/misc/killeffects.cpp
    src/hacks/misc/killeffects_world.cpp
    src/hacks/misc/misc_overlays.cpp
    src/hacks/misc/minesweeper.cpp
    src/hacks/hud/points.cpp
    src/hacks/hud/spectator_list.cpp
    src/hacks/misc/discord_ex.cpp
    src/hacks/misc/game_extras.cpp
    src/hacks/misc/names.cpp
    src/hacks/menu/unitE_menu.cpp
    src/hacks/hud/indicators.cpp
    src/hacks/menu/menu.cpp
    src/hacks/menu/menu_tab_visuals.cpp
    src/hacks/menu/menu_tab_movement.cpp
    src/hacks/menu/menu_tab_misc.cpp
    src/hacks/menu/menu_tab_settings.cpp
    src/hacks/menu/config.cpp
    src/hooks/thread.cpp
    src/hacks/movement/prediction.cpp
    src/hacks/movement/movement.cpp
    src/hacks/menu/unitA_menu.cpp
    src/hacks/network/netregion.cpp
    src/hacks/hud/scoreboard.cpp
    src/hooks/crashlog.cpp
    src/hooks/fsn.cpp
    src/hacks/visuals/flipworld.cpp
    src/hacks/movement/detections.cpp
    src/hacks/movement/recorder.cpp
    src/hacks/menu/unitB_menu.cpp
    src/hacks/menu/unitC_menu.cpp
    src/hacks/screen/screenfx_passes.cpp
    src/hacks/visuals/world_fx.cpp
    src/hacks/visuals/world_fx2.cpp
    src/hacks/visuals/world_fx3.cpp
    src/hacks/menu/unitD_menu.cpp
    src/hacks/visuals/reflections.cpp
    src/hacks/visuals/mc_world.cpp
    src/hacks/menu/unitD3_menu.cpp
    src/hacks/screen/aspect_stretch.cpp
    src/hacks/hud/gif_tex.cpp
    src/hacks/hud/avatar_cache.cpp
    src/hacks/screen/blur_rect.cpp
    src/hacks/hud/hud_extra.cpp   # unit hud: fov circle, net graph, hud fonts
    src/hacks/menu/fonts.cpp
    src/hacks/visuals/lsd_chams.cpp
    src/hacks/visuals/player_stencil.cpp
    src/hacks/visuals/esp_preview_tex.cpp
    src/dependencies/lua/lua_unity.cpp
    src/hacks/lua/lua.cpp
    src/hacks/lua/lua_api.cpp
    src/hacks/menu/menu_tab_lua.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_dx9.cpp
    ${IMGUI_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/freetype/imgui_freetype.cpp
    ${MH_DIR}/hde/hde64.c
    ${MH_DIR}/buffer.c
    ${MH_DIR}/hook.c
    ${MH_DIR}/trampoline.c
    ${AIM_SRC}
)

target_include_directories(botox PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${MH_DIR}
    "${FREETYPE_STATIC_DIR}/include"
)

target_link_directories(botox PRIVATE
    "${FREETYPE_STATIC_DIR}/lib"
    "$<$<CONFIG:Debug>:${FREETYPE_STATIC_DIR}/debug/lib>"
)

target_link_libraries(botox PRIVATE
    ${CMAKE_SOURCE_DIR}/src/dependencies/webview2/lib/x64/WebView2LoaderStatic.lib
    windowsapp d3d11 winhttp ole32
    d3d9
    winmm
    user32
    gdi32
    dwmapi
    psapi
    optimized freetype debug freetyped
)

target_compile_definitions(botox PRIVATE
    _WIN32_WINNT=0x0A00
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    # IMGUI_ENABLE_FREETYPE lives in imconfig.h -- defining here too = C4005 spam
    # the aim tree expects these two from the build system.
    _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR
    __CONFIGURATION__="botox"
    __PLATFORM__="x64"
)

# The aimbot is the hot path (projectile sim, movement sim, bone setup). botox's global
# /O1 /Os is a size choice for the visual features; build the aim tree for speed.
set_source_files_properties(${AIM_SRC} PROPERTIES
    COMPILE_OPTIONS "/FI${AIM_DIR}/prefix.h;/bigobj;$<$<CONFIG:Release>:/O2>"
)

# Size opt: /O1 favor size, /GL+LTCG whole-program, strip unused w/ /OPT:REF+ICF
# /MP = parallel cl per source. Pure build-time, no codegen change; movement.cpp is a
# separate cl invocation anyway (its own /O2 below) so it overlaps the rest of the TUs.
target_compile_options(botox PRIVATE
    /MP
    $<$<CONFIG:Release>:/O1 /Os /GL /Gy /Gw>
)

# ...except the engine-prediction sim -- the one genuinely hot loop here, so hot it runs
# on a per-cmd clock + sim budget (k_cmdSimCap) with a reserve fenced off for AirStuck.
# Compiling THAT for size directly costs sims per tick, which is the resource the feature
# logic is rationing. /O2 (speed) on these files only. prediction.cpp is the dih107 split
# of the same sim core (Pred_Simulate, snapshots) and lost this override when it moved.
set_source_files_properties(src/hacks/movement/movement.cpp src/hacks/movement/prediction.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CONFIG:Release>:/O2>"
)
# /MAP: crash.log prints every frame as `module+0xRVA`, and the dll loads at a random base, so
# botox.dll's own offsets are only readable against the link map. Costs nothing at runtime and
# ships nothing extra -- build\Release\botox.map stays next to the dll.
target_link_options(botox PRIVATE
    $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /LTCG /MAP>
)

set_target_properties(botox PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    OUTPUT_NAME "botox"
    PREFIX ""
    SUFFIX ".dll"
)

set(CMAKE_GENERATOR_PLATFORM x64 CACHE STRING "" FORCE)
