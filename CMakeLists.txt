cmake_minimum_required(VERSION 3.19)
project(Seelie VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# On Windows, prefer the real git.exe over any git.cmd wrapper on PATH.
# cmd.exe wrappers strip ^ as an escape char, breaking FetchContent's
# `git rev-parse HEAD^0` (it becomes HEAD0 and aborts the update step).
if(WIN32 AND NOT DEFINED CACHE{GIT_EXECUTABLE})
    find_program(GIT_EXECUTABLE NAMES git.exe
        HINTS "$ENV{ProgramFiles}/Git/cmd" "$ENV{ProgramFiles}/Git/bin"
        NO_CACHE)
    if(GIT_EXECUTABLE)
        set(GIT_EXECUTABLE "${GIT_EXECUTABLE}" CACHE FILEPATH "Path to git executable" FORCE)
    endif()
endif()

# Export compile_commands.json so clangd-based editors (Qt Creator, VS Code,
# Neovim) can resolve Qt headers without choking on every QObject include.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia Sql LinguistTools Test)

# TTS support is now HTTP-based (no WebSocket dependency). Kept as an
# optional feature so the build can still drop the AI tab on minimal builds.
option(SEELIE_TTS_ENABLED "Enable TTS (Text-to-Speech) support" ON)

# NSFW pack categories (currently: azur_lane) are excluded from the bundled
# pack set by default so store-distributable builds ship a SFW lineup. Users
# can still drop NSFW .spk archives into ~/.config/Seelie/packs/ at runtime —
# this gate only controls what ships in the installer.
# Enable with: cmake -DSEELIE_INCLUDE_NSFW=ON ..
#
# Classification is per-pack: a pack is NSFW iff its manifest.json `tags`
# array contains the literal string "nsfw". No category is banned wholesale —
# any individual pack can be cleared by removing that tag from its manifest.
option(SEELIE_INCLUDE_NSFW "Bundle packs whose manifest tags include \"nsfw\"" OFF)

qt_standard_project_setup()

# --- rlottie via FetchContent ------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    rlottie
    URL      https://github.com/Samsung/rlottie/archive/refs/tags/v0.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# rlottie v0.2 defaults LOTTIE_MODULE=ON, which builds rlottie-image-loader as
# a SHARED library regardless of BUILD_SHARED_LIBS. That produces a separate
# DLL that cmake --install tries to copy and fails on Windows because the
# install rules reference a non-existent file. Disabling the module folds
# stb_image.cpp directly into the static rlottie library.
set(LOTTIE_MODULE OFF CACHE BOOL "" FORCE)
# NOTE: rlottie v0.2 does NOT have a RLOTTIE_BUILD_EXAMPLE option.
# We patch out the example directory below after FetchContent populates it.
# rlottie declares cmake_minimum_required(3.3) which is incompatible with CMake 4.x
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE INTERNAL "" FORCE)

# Patch rlottie before building: its ARMv7 NEON assembly is incompatible with Apple Silicon (ARM64)
FetchContent_GetProperties(rlottie)
if(NOT rlottie_POPULATED)
    # FetchContent_Populate is needed (not MakeAvailable) because we patch
    # rlottie source files between population and add_subdirectory.  Silence
    # CMP0169 deprecation — the two-step pattern is intentional here.
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(rlottie)
    if(APPLE)
        # rlottie's ARMv7 NEON assembly is incompatible with Apple Silicon (ARM64/AArch64).
        # Provide C++ fallback implementations for the missing assembly symbols.
        set(NEON_PATCH_FILE "${rlottie_SOURCE_DIR}/src/vector/vdrawhelper_neon.cpp")
        file(READ "${NEON_PATCH_FILE}" NEON_PATCH_CONTENT)
        if(NOT NEON_PATCH_CONTENT MATCHES "pixman_composite_src_n_8888_asm_neon.*fallback")
            file(APPEND "${NEON_PATCH_FILE}" "

/* macOS fallback for missing ARMv7 NEON assembly symbols */
#if defined(__APPLE__)
extern \"C\" void pixman_composite_src_n_8888_asm_neon(int32_t w, int32_t h,
                                                     uint32_t *dst,
                                                     int32_t   dst_stride,
                                                     uint32_t  src)
{
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            dst[y * dst_stride + x] = src;
        }
    }
}

extern \"C\" void pixman_composite_over_n_8888_asm_neon(int32_t w, int32_t h,
                                                      uint32_t *dst,
                                                      int32_t   dst_stride,
                                                      uint32_t  src)
{
    uint8_t src_a = src >> 24;
    if (src_a == 0) return;
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            uint32_t d = dst[y * dst_stride + x];
            uint8_t da = d >> 24;
            uint8_t dr = (d >> 16) & 0xFF;
            uint8_t dg = (d >> 8) & 0xFF;
            uint8_t db = d & 0xFF;
            uint8_t sa = src >> 24;
            uint8_t sr = (src >> 16) & 0xFF;
            uint8_t sg = (src >> 8) & 0xFF;
            uint8_t sb = src & 0xFF;
            uint16_t inv_sa = 255 - sa;
            uint8_t a = sa + (da * inv_sa / 255);
            uint8_t r = sr + (dr * inv_sa / 255);
            uint8_t g = sg + (dg * inv_sa / 255);
            uint8_t b = sb + (db * inv_sa / 255);
            dst[y * dst_stride + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
}
#endif
")
        endif()
    endif()

    # Patch rlottie vrle.cpp: GCC 13+ requires explicit <limits> include
    set(VRLE_FILE "${rlottie_SOURCE_DIR}/src/vector/vrle.cpp")
    file(READ "${VRLE_FILE}" VRLE_CONTENT)
    if(NOT VRLE_CONTENT MATCHES "#include <limits>")
        string(REPLACE "#include <vector>" "#include <vector>\n#include <limits>"
               VRLE_PATCHED "${VRLE_CONTENT}")
        file(WRITE "${VRLE_FILE}" "${VRLE_PATCHED}")
    endif()

    # Patch rlottie CMakeLists.txt to remove version script linker flag.
    # It uses ${CMAKE_SOURCE_DIR}/rlottie.expmap which resolves to the
    # top-level project dir (not rlottie's) when used via FetchContent.
    set(RLOTTIE_CMAKELISTS "${rlottie_SOURCE_DIR}/CMakeLists.txt")
    file(READ "${RLOTTIE_CMAKELISTS}" RLOTTIE_CMAKE_CONTENT)
    string(REGEX REPLACE "\"-Wl,--version-script=[^\"]*rlottie\\.expmap\"" "\"\""
           RLOTTIE_CMAKE_CONTENT "${RLOTTIE_CMAKE_CONTENT}")

    # Also patch out the example directory — rlottie v0.2 unconditionally builds
    # lottie2gif which Qt Creator auto-detects as a run target.
    string(REPLACE "add_subdirectory(example)"
                    "# add_subdirectory(example)  # disabled by Seelie"
                    RLOTTIE_CMAKE_PACHED "${RLOTTIE_CMAKE_CONTENT}")
    if(NOT RLOTTIE_CMAKE_CONTENT STREQUAL RLOTTIE_CMAKE_PACHED)
        file(WRITE "${RLOTTIE_CMAKELISTS}" "${RLOTTIE_CMAKE_PACHED}")
    endif()

    add_subdirectory(${rlottie_SOURCE_DIR} ${rlottie_BINARY_DIR})
endif()

# --- QHotkey (global shortcut support) ----------------------------------------
include(FetchContent)
FetchContent_Declare(QHotkey
    GIT_REPOSITORY https://github.com/Skycoder42/QHotkey.git
    GIT_TAG 1.5.0)
FetchContent_MakeAvailable(QHotkey)

# --- Live2D Cubism SDK -------------------------------------------------------
set(CUBISM_SDK_DIR ${CMAKE_SOURCE_DIR}/thirdparty/CubismNativeSamples)
set(CUBISM_FRAMEWORK_DIR ${CMAKE_SOURCE_DIR}/thirdparty/CubismNativeFramework)
set(CUBISM_CORE_DIR ${CUBISM_SDK_DIR}/Core)

# Live2D is non-optional. Every shipped pack is Live2D, so a build without
# the SDK produces an Seelie.exe that ships unrenderable assets and shows a
# transparent window at runtime. Refuse to configure rather than ship that.

if(EXISTS ${CUBISM_CORE_DIR}/include/Live2DCubismCore.h)
    message(STATUS "Live2D Cubism SDK found")

    # GLEW (downloaded by Samples/OpenGL/thirdParty/scripts/setup_glew_glfw)
    set(GLEW_DIR ${CUBISM_SDK_DIR}/Samples/OpenGL/thirdParty/glew)
    if(NOT EXISTS ${GLEW_DIR}/include)
        message(FATAL_ERROR "GLEW not found. Run: cd thirdparty/CubismNativeSamples/Samples/OpenGL/thirdParty && bash scripts/setup_glew_glfw")
    endif()

    # Build GLEW as static library
    set(BUILD_UTILS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${GLEW_DIR}/build/cmake ${CMAKE_CURRENT_BINARY_DIR}/glew)
    target_compile_definitions(glew_s PRIVATE GLEW_NO_GLU)
    if(APPLE)
        # Suppress macOS OpenGL deprecation warnings
        add_compile_definitions(GL_SILENCE_DEPRECATION)
    endif()

    # Import Cubism Core (pre-built binary) — platform-specific paths
    # Use UNKNOWN IMPORTED so both static (.a/.lib) and shared (.dll) linking works
    add_library(Live2DCubismCore UNKNOWN IMPORTED)
    if(APPLE)
        EXECUTE_PROCESS(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)
        set_target_properties(Live2DCubismCore PROPERTIES
            IMPORTED_LOCATION ${CUBISM_CORE_DIR}/lib/macos/${ARCHITECTURE}/libLive2DCubismCore.a
            INTERFACE_INCLUDE_DIRECTORIES ${CUBISM_CORE_DIR}/include
        )
    elseif(WIN32)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_CUBISM_ARCH x86_64)
        else()
            set(_CUBISM_ARCH x86)
        endif()
        if(MSVC)
            # MSVC: use static library from lib/ directory
            if(MSVC_TOOLSET_VERSION)
                set(_CUBISM_MSVC ${MSVC_TOOLSET_VERSION})
            else()
                set(_CUBISM_MSVC 143)
            endif()
            set_target_properties(Live2DCubismCore PROPERTIES
                IMPORTED_LOCATION ${CUBISM_CORE_DIR}/lib/windows/${_CUBISM_ARCH}/${_CUBISM_MSVC}/Live2DCubismCore_MD.lib
                IMPORTED_LOCATION_DEBUG ${CUBISM_CORE_DIR}/lib/windows/${_CUBISM_ARCH}/${_CUBISM_MSVC}/Live2DCubismCore_MDd.lib
                INTERFACE_INCLUDE_DIRECTORIES ${CUBISM_CORE_DIR}/include
            )
        else()
            # MinGW/Clang: use DLL import library from dll/ directory.
            # llvm-mingw's LLD linker can consume MSVC import libraries directly.
            # At runtime, Live2DCubismCore.dll must be next to the executable.
            set_target_properties(Live2DCubismCore PROPERTIES
                IMPORTED_LOCATION ${CUBISM_CORE_DIR}/dll/windows/${_CUBISM_ARCH}/Live2DCubismCore.dll
                IMPORTED_IMPLIB ${CUBISM_CORE_DIR}/dll/windows/${_CUBISM_ARCH}/Live2DCubismCore.lib
                INTERFACE_INCLUDE_DIRECTORIES ${CUBISM_CORE_DIR}/include
            )
            # Bundle the runtime DLL into the install tree so the packaged
            # build can find it. windeployqt only handles Qt's own DLLs;
            # without this, an installed Seelie.exe fails at startup with
            #   "Live2DCubismCore.dll was not found".
            install(FILES ${CUBISM_CORE_DIR}/dll/windows/${_CUBISM_ARCH}/Live2DCubismCore.dll
                    DESTINATION ${CMAKE_INSTALL_BINDIR})
            # Also stage it next to Seelie.exe in the build dir so a direct-run
            # (Qt Creator / `./Seelie.exe`) works without a manual copy.
            set(_SEELIE_CUBISM_RUNTIME_DLL
                ${CUBISM_CORE_DIR}/dll/windows/${_CUBISM_ARCH}/Live2DCubismCore.dll
                CACHE INTERNAL "Cubism runtime DLL to copy beside Seelie.exe")
        endif()
    else()
        # Linux
        set_target_properties(Live2DCubismCore PROPERTIES
            IMPORTED_LOCATION ${CUBISM_CORE_DIR}/lib/linux/x86_64/libLive2DCubismCore.a
            INTERFACE_INCLUDE_DIRECTORIES ${CUBISM_CORE_DIR}/include
        )
    endif()

    # Patch Cubism Framework for MinGW: wglGetProcAddress returns PROC, not void*
    set(_RENDERER_FILE "${CUBISM_FRAMEWORK_DIR}/src/Rendering/OpenGL/CubismRenderer_OpenGLES2.cpp")
    file(READ "${_RENDERER_FILE}" _RENDERER_CONTENT)
    if(NOT _RENDERER_CONTENT MATCHES "reinterpret_cast.*wglGetProcAddress")
        string(REPLACE "void* ptr = wglGetProcAddress(name);"
               "void* ptr = reinterpret_cast<void*>(wglGetProcAddress(name));"
               _RENDERER_CONTENT "${_RENDERER_CONTENT}")
        file(WRITE "${_RENDERER_FILE}" "${_RENDERER_CONTENT}")
    endif()

    # Patch Cubism Framework: InitializeGlFunctions() normally resolves GL function
    # pointers via wglGetProcAddress(), but this conflicts with QOpenGLContext/GLEW.
    # GLEW already resolves all pointers via glewInit(). However, this file declares
    # anonymous-namespace local variables that SHADOW GLEW's global __glew* pointers.
    # We must initialize these locals from GLEW's already-valid globals.
    #
    # Strategy: Use a Python script to replace each WinGlGetProcAddress("name") call
    # with a direct assignment from the corresponding GLEW __glew* global variable.
    file(READ "${_RENDERER_FILE}" _RENDERER_CONTENT2)
    if(NOT _RENDERER_CONTENT2 MATCHES "SEELIE_GLEW_COPY_FROM_GLEW")
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        execute_process(
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/cmake/patch_cubism_gl.py" "${_RENDERER_FILE}"
            OUTPUT_VARIABLE _PATCH_OUTPUT
            RESULT_VARIABLE _PATCH_RESULT
        )
        message(STATUS "Cubism GL patch: ${_PATCH_OUTPUT}")
        if(NOT _PATCH_RESULT EQUAL 0)
            message(WARNING "Failed to patch Cubism GL functions: ${_PATCH_RESULT}")
        endif()
    endif()

    # Build Cubism Framework
    set(FRAMEWORK_SOURCE OpenGL)
    add_subdirectory(${CUBISM_FRAMEWORK_DIR} ${CMAKE_CURRENT_BINARY_DIR}/Framework)
    # Platform-specific OpenGL target defines
    if(APPLE)
        target_compile_definitions(Framework PUBLIC CSM_TARGET_MAC_GL)
    elseif(WIN32)
        target_compile_definitions(Framework PUBLIC CSM_TARGET_WIN_GL)
    else()
        target_compile_definitions(Framework PUBLIC CSM_TARGET_LINUX_GL)
    endif()
    target_include_directories(Framework PUBLIC ${GLEW_DIR}/include)
    target_link_libraries(Framework Live2DCubismCore glew_s)

    # OpenGL
    find_package(OpenGL REQUIRED)

    set(SEELIE_HAS_LIVE2D TRUE)
else()
    message(FATAL_ERROR
        "\n"
        "Live2D Cubism SDK Core is not installed.\n"
        "\n"
        "Expected:  ${CUBISM_CORE_DIR}/include/Live2DCubismCore.h\n"
        "\n"
        "The SDK is mandatory — every shipped pack is Live2D, so a build\n"
        "without it would produce an Seelie.exe that renders a transparent\n"
        "window at runtime. To install:\n"
        "\n"
        "  1. Download the Cubism SDK for Native (free, requires sign-in):\n"
        "       https://www.live2d.com/en/sdk/download/native/\n"
        "  2. Extract the archive's `Core/` folder over\n"
        "       ${CUBISM_CORE_DIR}/\n"
        "     so that the path above exists.\n"
        "  3. Fetch the SDK's bundled GLEW/GLFW (one-time, separate script):\n"
        "       cd thirdparty/CubismNativeSamples/Samples/OpenGL/thirdParty\n"
        "       bash scripts/setup_glew_glfw\n"
        "  4. Re-run configure.\n")
endif()

# --- Executable --------------------------------------------------------------
if(WIN32)
    # VERSIONINFO + icon resource: configure_file substitutes
    # @PROJECT_VERSION@ etc. so right-click → Properties → Details on
    # Seelie.exe shows the same version that's compiled in.
    configure_file(${CMAKE_SOURCE_DIR}/assets/seelie.rc.in
                   ${CMAKE_BINARY_DIR}/seelie.rc
                   @ONLY)
    set(PLATFORM_SOURCES ${CMAKE_BINARY_DIR}/seelie.rc)
    # MinGW's windres doesn't emit dep info for ICON-included files,
    # so ninja can't tell seelie.rc.obj depends on seelie.ico. Without this,
    # touching seelie.ico does NOT rebuild seelie.rc.obj — the linked binary
    # then ships with a stale embedded icon.
    set_source_files_properties(${CMAKE_BINARY_DIR}/seelie.rc PROPERTIES
        OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/assets/seelie.ico)
endif()

qt_add_executable(Seelie
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    ${PLATFORM_SOURCES}
    src/mainwindow.cpp
    src/mainwindow.h
    src/SpriteAnimationEngine.cpp
    src/SpriteAnimationEngine.h
    src/LottieAnimationEngine.cpp
    src/LottieAnimationEngine.h
    src/LottieEffectOverlay.cpp
    src/LottieEffectOverlay.h
    src/Live2DAnimationEngine.cpp
    src/Live2DAnimationEngine.h
    src/CharacterPack.cpp
    src/CharacterPack.h
    src/CharacterPackManager.cpp
    src/CharacterPackManager.h
    src/PetStateMachine.cpp
    src/PetStateMachine.h
    src/UpdateChecker.cpp
    src/UpdateChecker.h
    src/IPCServer.cpp
    src/IPCServer.h
    src/UDPWorker.cpp
    src/UDPWorker.h
    src/EventRouter.cpp
    src/EventRouter.h
    src/TipsEngine.cpp
    src/TipsEngine.h
    src/TipsCatalog.cpp
    src/TipsCatalog.h
    src/ConfigManager.cpp
    src/ConfigManager.h
    src/SystemTray.cpp
    src/SystemTray.h
    src/TipWidget.cpp
    src/TipWidget.h
    src/SettingsPanelWidget.cpp
    src/SettingsPanelWidget.h
    src/PersonaDialog.cpp
    src/PersonaDialog.h
    src/EditLLMProfileDialog.cpp
    src/EditLLMProfileDialog.h
    src/StatisticsDialog.cpp
    src/StatisticsDialog.h
    src/MemoryManager.cpp
    src/MemoryManager.h
    src/PackManagerWidget.cpp
    src/PackManagerWidget.h
    src/StyledAlertWidget.cpp
    src/StyledAlertWidget.h
    src/ECGWidget.cpp
    src/ECGWidget.h
    src/MacFocusFix.h
    $<$<PLATFORM_ID:Darwin>:src/MacFocusFix.mm>
    src/PlatformWindow.cpp
    src/PlatformWindow.h
    src/StyleUtils.cpp
    src/StyleUtils.h
    src/StatisticsPersistence.cpp
    src/StatisticsPersistence.h
    src/StatisticsManager.cpp
    src/StatisticsManager.h
    src/ExportManifest.cpp
    src/ExportManifest.h
    src/ConfigExporter.cpp
    src/ConfigExporter.h
    src/ConfigImporter.cpp
    src/ConfigImporter.h
    src/AutoStartManager.cpp
    src/AutoStartManager.h
    src/CanonicalEvents.h
    src/PackDropHandler.cpp
    src/PackDropHandler.h
    src/CharacterPackLoader.cpp
    src/CharacterPackLoader.h
    src/AnimationEngine.h
    thirdparty/miniz/miniz.c
    thirdparty/miniz/miniz.h
    src/GlobalShortcutManager.cpp
    src/GlobalShortcutManager.h
    src/FullscreenWatcher.cpp
    src/FullscreenWatcher.h
    src/llm/LLMProfile.h
    src/llm/LLMProvider.h
    src/llm/LLMProvider.cpp
    src/PersonaPool.h
    src/PersonaPool.cpp
    src/PersonaEngine.h
    src/PersonaEngine.cpp
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/TTSEngine.cpp>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/TTSEngine.h>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/tts/ITTSProvider.h>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/tts/ProviderConfig.h>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/tts/TTSProviderRegistry.h>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/tts/TTSProviderRegistry.cpp>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/tts/TTSVoiceCache.h>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/tts/TTSVoiceCache.cpp>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/tts/StepFunHTTPProvider.h>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/tts/StepFunHTTPProvider.cpp>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/tts/MiniMaxHTTPProvider.h>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/tts/MiniMaxHTTPProvider.cpp>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/tts/AzureSpeechProvider.h>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/tts/AzureSpeechProvider.cpp>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/tts/OpenAITTSProvider.h>
    $<$<BOOL:${SEELIE_TTS_ENABLED}>:src/tts/OpenAITTSProvider.cpp>
)

target_include_directories(Seelie PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Default endpoint for the version-check / update server. Baked into the
# binary via SEELIE_DEFAULT_UPDATE_ENDPOINT and into the shipped Seelie.ini via
# configure_file. Override at configure time, e.g.:
#   cmake -DSEELIE_DEFAULT_UPDATE_ENDPOINT=updates.example.com:9340 ..
# The value is just a default — users can still override at runtime via
# QSettings (`updateServerEndpoint` key) without rebuilding.
set(SEELIE_DEFAULT_UPDATE_ENDPOINT "101.133.144.133:9340" CACHE STRING
    "Default host:port for the Seelie update server")

# Substitute @SEELIE_DEFAULT_UPDATE_ENDPOINT@ into the portable Seelie.ini that
# the installer ships next to Seelie.exe.
configure_file(
    ${CMAKE_SOURCE_DIR}/installer/seelie.ini.template
    ${CMAKE_BINARY_DIR}/Seelie.ini
    @ONLY)

# Pass version and source directory to C++ code
target_compile_definitions(Seelie PRIVATE
    PROJECT_VERSION="${PROJECT_VERSION}"
    SEELIE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    SEELIE_DEFAULT_UPDATE_ENDPOINT="${SEELIE_DEFAULT_UPDATE_ENDPOINT}"
)

# --- Icon resource -----------------------------------------------------------
qt_add_resources(Seelie "icons"
    PREFIX "/icons"
    BASE "assets/icons"
    FILES
        assets/icons/seelie.png
)

# Per-locale tip messages (event tips + random greetings). Authoring lives
# in plain JSON instead of Qt's .ts/.qm so non-engineers can add a language
# by dropping in another `tips.<locale>.json`.
qt_add_resources(Seelie "tips_i18n"
    PREFIX "/i18n"
    BASE "assets"
    FILES
        assets/i18n/tips.en.json
        assets/i18n/tips.zh_CN.json
)

# Global stylesheet (Persona 5 design system). Loaded by main.cpp via
# qApp->setStyleSheet on startup. Styles widgets that don't carry their
# own setStyleSheet — most importantly the tray menu and pet context
# menu, which were inheriting OS-native styling. Per-widget inline QSS
# still wins over this (Qt specificity rule), so the existing 48-odd
# inline setStyleSheet calls are unaffected.
qt_add_resources(Seelie "styles"
    PREFIX "/styles"
    BASE "assets"
    FILES
        assets/styles/seelie.qss
)

# --- Translations ------------------------------------------------------------
qt_add_translations(
    TARGETS Seelie
    TS_FILES Seelie_zh_CN.ts
)

# --- Link libraries ----------------------------------------------------------
target_link_libraries(Seelie
    PRIVATE
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Network
        Qt::Sql
        Qt::Multimedia
        rlottie::rlottie
        QHotkey::QHotkey
)

# TTS support (HTTP-based, no WebSocket dependency)
if(SEELIE_TTS_ENABLED)
    target_compile_definitions(Seelie PRIVATE SEELIE_TTS_ENABLED=1)
endif()

# rlottie header path (FetchContent puts it in build/_deps)
target_include_directories(Seelie PRIVATE
    ${rlottie_SOURCE_DIR}/inc
)

# Live2D libraries (conditional)
if(SEELIE_HAS_LIVE2D)
    target_link_libraries(Seelie PRIVATE Framework ${OPENGL_LIBRARIES})
    target_include_directories(Seelie PRIVATE
        ${CUBISM_FRAMEWORK_DIR}/src
        ${CUBISM_CORE_DIR}/include
        ${GLEW_DIR}/include
    )
    target_compile_definitions(Seelie PRIVATE SEELIE_LIVE2D_SUPPORT)
    if(APPLE)
        target_compile_definitions(Seelie PRIVATE CSM_TARGET_MAC_GL)
    elseif(WIN32)
        target_compile_definitions(Seelie PRIVATE CSM_TARGET_WIN_GL)
        # Stage Live2DCubismCore.dll next to Seelie.exe in the build dir so
        # direct-run (Qt Creator / `./Seelie.exe`) works. The install rule
        # above handles the packaged case; this handles dev iteration.
        if(_SEELIE_CUBISM_RUNTIME_DLL)
            add_custom_command(TARGET Seelie POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${_SEELIE_CUBISM_RUNTIME_DLL}
                        $<TARGET_FILE_DIR:Seelie>
                COMMENT "Staging Live2DCubismCore.dll next to Seelie.exe"
                VERBATIM)
        endif()
    else()
        target_compile_definitions(Seelie PRIVATE CSM_TARGET_LINUX_GL)
    endif()
    # Qt OpenGL support
    find_package(Qt6 COMPONENTS OpenGL OpenGLWidgets QUIET)
    if(Qt6OpenGL_FOUND)
        target_link_libraries(Seelie PRIVATE Qt::OpenGL)
    endif()
    if(Qt6OpenGLWidgets_FOUND)
        target_link_libraries(Seelie PRIVATE Qt::OpenGLWidgets)
    endif()

    if(WIN32)
        # dwmapi: DwmSetWindowAttribute() is used in MainWindow::showEvent
        # to opt out of Windows 11's automatic rounded corners, drop shadow,
        # and Mica/Acrylic backdrop on the frameless pet window.
        target_link_libraries(Seelie PRIVATE dwmapi)
    endif()

    # Bundle Cubism Framework shader sources as Qt resources.
    # CubismShader_OpenGLES2::LoadShaderProgramFromFile requests paths like
    # "FrameworkShaders/VertShaderSrc.vert"; our loader maps that to
    # ":/live2d/FrameworkShaders/<name>", so we alias each file accordingly.
    set(_LIVE2D_SHADER_DIR
        ${CUBISM_FRAMEWORK_DIR}/src/Rendering/OpenGL/Shaders/Standard)
    file(GLOB _LIVE2D_SHADER_FILES
        ${_LIVE2D_SHADER_DIR}/*.vert
        ${_LIVE2D_SHADER_DIR}/*.frag)
    foreach(_shader ${_LIVE2D_SHADER_FILES})
        get_filename_component(_shader_name ${_shader} NAME)
        set_source_files_properties(${_shader} PROPERTIES
            QT_RESOURCE_ALIAS "FrameworkShaders/${_shader_name}")
    endforeach()
    qt_add_resources(Seelie "live2d_shaders"
        PREFIX "/live2d"
        FILES ${_LIVE2D_SHADER_FILES}
    )
endif()

# --- Install -----------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS Seelie
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# --- Generate official packs -------------------------------------------------
# Generate .spk packs from pack directories during build
set(PACKS_DIR ${CMAKE_BINARY_DIR}/packs)
file(MAKE_DIRECTORY ${PACKS_DIR})

# Sprite packs (Clippy + ClippyJS agents) are currently disabled. The pack
# directories were moved to assets/packs-disabled/ so the runtime
# CharacterPackManager scan in dev builds skips them too. To re-enable,
# move the directories back to assets/packs/ and uncomment the two blocks
# below plus the macOS bundle loops further down.
#
# # Clippy pack (has external animations.json definitions file)
# add_custom_command(
#     OUTPUT ${PACKS_DIR}/clippy.spk
#     COMMAND ${CMAKE_COMMAND} -E tar "cf" ${PACKS_DIR}/clippy.spk --format=zip --
#         ${CMAKE_SOURCE_DIR}/assets/packs/clippy/manifest.json
#         ${CMAKE_SOURCE_DIR}/assets/packs/clippy/preview.png
#         ${CMAKE_SOURCE_DIR}/assets/packs/clippy/sprites/map.png
#         ${CMAKE_SOURCE_DIR}/assets/packs/clippy/sprites/animations.json
#     DEPENDS
#         ${CMAKE_SOURCE_DIR}/assets/packs/clippy/manifest.json
#         ${CMAKE_SOURCE_DIR}/assets/packs/clippy/preview.png
#         ${CMAKE_SOURCE_DIR}/assets/packs/clippy/sprites/map.png
#         ${CMAKE_SOURCE_DIR}/assets/packs/clippy/sprites/animations.json
#     COMMENT "Generating clippy.spk"
#     WORKING_DIRECTORY ${PACKS_DIR}
# )
#
# # ClippyJS agent packs (inline animations in manifest.json, no definitions file)
# set(CLIPPYJS_PACKS bonzi f1 genie genius links merlin peedy rocky rover)
#
# foreach(pack ${CLIPPYJS_PACKS})
#     add_custom_command(
#         OUTPUT ${PACKS_DIR}/${pack}.spk
#         COMMAND ${CMAKE_COMMAND} -E tar "cf" ${PACKS_DIR}/${pack}.spk --format=zip --
#             ${CMAKE_SOURCE_DIR}/assets/packs/${pack}/manifest.json
#             ${CMAKE_SOURCE_DIR}/assets/packs/${pack}/preview.png
#             ${CMAKE_SOURCE_DIR}/assets/packs/${pack}/sprites/map.png
#         DEPENDS
#             ${CMAKE_SOURCE_DIR}/assets/packs/${pack}/manifest.json
#             ${CMAKE_SOURCE_DIR}/assets/packs/${pack}/preview.png
#             ${CMAKE_SOURCE_DIR}/assets/packs/${pack}/sprites/map.png
#         COMMENT "Generating ${pack}.spk"
#         WORKING_DIRECTORY ${PACKS_DIR}
#     )
#     list(APPEND ALL_PACK_SPKS ${PACKS_DIR}/${pack}.spk)
# endforeach()

set(ALL_PACK_SPKS "")

# --- Optional: bulk-import packs from the upstream submodule ---------------
# The 21 first-party packs in assets/packs/ are tracked in git. Everything
# else (Azur Lane, Girls Frontline, Idol Dimension, Konosuba, Live2D Free
# Material samples) is a build artifact regenerated from the upstream repo:
#
#   git submodule update --init thirdparty/upstream-live2d
#   cmake --build build --target import_packs
#   cmake --build build --target generate_packs   # then build .spks
#
# `import_packs` is a manual target — it is NOT part of the default build,
# so a fresh clone without the submodule still builds Seelie.exe + the 21
# original .spks fine.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND EXISTS ${CMAKE_SOURCE_DIR}/thirdparty/upstream-live2d/.git)
    add_custom_target(import_packs
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/import_live2d.py
                --local ${CMAKE_SOURCE_DIR}/thirdparty/upstream-live2d
                --cap 50
                --out-dir ${CMAKE_SOURCE_DIR}/assets/packs
        COMMENT "Importing Live2D packs from upstream submodule (curated PICKS + bulk per-category)"
        USES_TERMINAL
        VERBATIM)
endif()

# Live2D packs (model3.json + moc3 + textures/ + motions/ + optional physics/pose/expressions)
# These ship the whole pack directory, so we enumerate files with GLOB_RECURSE
# and archive paths relative to the pack root.
#
# Auto-discovered: any directory under assets/packs/ with a manifest.json is
# treated as a pack. CONFIGURE_DEPENDS makes CMake re-glob when files change,
# so running `import_packs` and rebuilding picks up new packs automatically.
file(GLOB _PACK_MANIFESTS CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/assets/packs/*/manifest.json)
set(LIVE2D_PACKS "")
set(_skipped_nsfw_count 0)
foreach(_manifest ${_PACK_MANIFESTS})
    get_filename_component(_pack_dir ${_manifest} DIRECTORY)
    get_filename_component(_pack_name ${_pack_dir} NAME)

    # NSFW gate (per-pack): a pack is NSFW iff its manifest `tags` array
    # contains the literal "nsfw". Drop it from the bundled set when
    # SEELIE_INCLUDE_NSFW is OFF. string(JSON) needs CMake >= 3.19 (already
    # our minimum). A missing/malformed/empty tags array is treated as SFW
    # (fail-open: never silently drop a legit pack because of a parse hiccup).
    if(NOT SEELIE_INCLUDE_NSFW)
        file(READ ${_manifest} _manifest_json)
        set(_is_nsfw FALSE)
        string(JSON _tags_count ERROR_VARIABLE _tags_err
               LENGTH "${_manifest_json}" tags)
        if(NOT _tags_err AND _tags_count GREATER 0)
            math(EXPR _tags_last "${_tags_count} - 1")
            foreach(_ti RANGE ${_tags_last})
                string(JSON _tag GET "${_manifest_json}" tags ${_ti})
                if(_tag STREQUAL "nsfw")
                    set(_is_nsfw TRUE)
                    break()
                endif()
            endforeach()
        endif()
        if(_is_nsfw)
            math(EXPR _skipped_nsfw_count "${_skipped_nsfw_count} + 1")
            continue()
        endif()
    endif()

    list(APPEND LIVE2D_PACKS ${_pack_name})
endforeach()
list(SORT LIVE2D_PACKS)
list(LENGTH LIVE2D_PACKS _live2d_pack_count)
if(SEELIE_INCLUDE_NSFW)
    message(STATUS "Live2D packs discovered: ${_live2d_pack_count} (NSFW bundled — SEELIE_INCLUDE_NSFW=ON)")
else()
    message(STATUS "Live2D packs discovered: ${_live2d_pack_count} "
                   "(${_skipped_nsfw_count} NSFW packs excluded — set -DSEELIE_INCLUDE_NSFW=ON to bundle)")
endif()
foreach(pack ${LIVE2D_PACKS})
    file(GLOB_RECURSE _${pack}_FILES RELATIVE
        ${CMAKE_SOURCE_DIR}/assets/packs/${pack}
        ${CMAKE_SOURCE_DIR}/assets/packs/${pack}/*)
    set(_${pack}_DEPS "")
    foreach(f ${_${pack}_FILES})
        list(APPEND _${pack}_DEPS ${CMAKE_SOURCE_DIR}/assets/packs/${pack}/${f})
    endforeach()
    add_custom_command(
        OUTPUT ${PACKS_DIR}/${pack}.spk
        COMMAND ${CMAKE_COMMAND} -E tar "cf" ${PACKS_DIR}/${pack}.spk --format=zip -- ${_${pack}_FILES}
        DEPENDS ${_${pack}_DEPS}
        COMMENT "Generating ${pack}.spk"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/assets/packs/${pack}
    )
    list(APPEND ALL_PACK_SPKS ${PACKS_DIR}/${pack}.spk)
endforeach()

add_custom_target(generate_packs ALL DEPENDS ${ALL_PACK_SPKS})
add_dependencies(Seelie generate_packs)

# Install built-in pack directories.
#
# We ship raw directories (manifest.json + model/textures/motions) rather than
# .spk archives because CharacterPackManager::discoverPacks() in src/
# CharacterPackManager.cpp scans for sub-directories + .codex-pet files — it
# does NOT extract .spk archives at discovery time. Shipping .spk made the
# 25 built-in packs invisible at runtime.
#
# Filter through LIVE2D_PACKS so the NSFW gate (per-pack manifest "nsfw" tag,
# controlled by SEELIE_INCLUDE_NSFW) applies to the installer payload too.
#
# .spk archives are still generated by the generate_packs target — kept for
# manual drag-drop distribution and the CMake-time per-pack archive output.
foreach(pack ${LIVE2D_PACKS})
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/packs/${pack}
            DESTINATION ${CMAKE_INSTALL_BINDIR}/assets/packs)
endforeach()

# Bundle the rest of assets/ that the runtime expects to find next to the exe:
#   - animations.json     : sprite-engine sentinel + frame timing fallback
#   - fonts/              : HarmonyOS Sans SC TTFs (CJK glyph rendering)
#   - i18n/               : tips.<locale>.json catalogs (event-tip text)
#   - lottie/             : lottie effect overlays (sparkle, confetti, alert-pulse)
# Skipped on purpose:
#   - packs/              : installed above as .spk archives, not raw dirs
#   - packs-disabled/     : developer-only sprite-pack archive of legacy agents
#   - screenshots/        : repo docs only, never read at runtime
#   - icons/              : qt_add_resources baked this into the binary
install(FILES ${CMAKE_SOURCE_DIR}/assets/animations.json
        DESTINATION ${CMAKE_INSTALL_BINDIR}/assets)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/fonts
        DESTINATION ${CMAKE_INSTALL_BINDIR}/assets)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/i18n
        DESTINATION ${CMAKE_INSTALL_BINDIR}/assets
        FILES_MATCHING PATTERN "tips.*.json")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/lottie
        DESTINATION ${CMAKE_INSTALL_BINDIR}/assets)

# --- macOS: app icon & hide Dock icon ----------------------------------------
if(APPLE)
    # AppKit is needed for MacFocusFix.mm (NSWindow / NSPanel APIs).
    # CoreGraphics is needed for FullscreenWatcher.cpp (CGWindowListCopyWindowInfo).
    target_link_libraries(Seelie PRIVATE "-framework AppKit" "-framework CoreGraphics")
    # Bake the @executable_path rpath into the binary at link time. macdeployqt
    # is supposed to do this post-build via install_name_tool, but it fails on
    # malformed bundled QtQml frameworks and aborts before fixing rpath. And
    # post-build install_name_tool can't run anyway: the linker ad-hoc signs
    # binaries on Apple Silicon, so install_name_tool refuses with "malformed
    # object" without first stripping the signature. Embedding rpath at link
    # time sidesteps both issues.
    #
    # The Qt SDK lib dir is appended as a fallback so Qt Creator debug runs
    # and direct ./Seelie.app launches (which don't run macdeployqt) can still
    # resolve frameworks like QtMultimedia. dyld checks rpaths in order, so
    # deployed bundles always find their bundled framework first; the SDK
    # path is only consulted on dev machines that have it.
    get_target_property(_qt_qmake_loc Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake_loc}" DIRECTORY)
    get_filename_component(_qt_lib_dir "${_qt_bin_dir}/../lib" ABSOLUTE)
    set_target_properties(Seelie PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks;${_qt_lib_dir}"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
    set_target_properties(Seelie PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "seelie.icns"
        MACOSX_BUNDLE_GUI_IDENTIFIER "im.cheng.seelie"
        MACOSX_BUNDLE_BUNDLE_NAME "Seelie"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2026 HUANG Cheng. All rights reserved."
        MACOSX_BUNDLE_INFO_STRING "Seelie ${PROJECT_VERSION} — Copyright (C) 2026 HUANG Cheng"
    )
    set_source_files_properties(assets/seelie.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(Seelie PRIVATE assets/seelie.icns)

    # Include packs in the macOS bundle Resources.
    # Sprite packs disabled — see the generation block above. Re-enable both
    # together.
    # set_source_files_properties(${PACKS_DIR}/clippy.spk PROPERTIES
    #     MACOSX_PACKAGE_LOCATION "Resources/packs"
    # )
    # target_sources(Seelie PRIVATE ${PACKS_DIR}/clippy.spk)
    #
    # foreach(pack ${CLIPPYJS_PACKS})
    #     set_source_files_properties(${PACKS_DIR}/${pack}.spk PROPERTIES
    #         MACOSX_PACKAGE_LOCATION "Resources/packs"
    #     )
    #     target_sources(Seelie PRIVATE ${PACKS_DIR}/${pack}.spk)
    # endforeach()

    # macOS: bundle filtered raw pack directories under
    # Contents/Resources/assets/packs. Runtime's discoverPacks() scans for
    # sub-directories, not .spk archives — same fix as Windows. Copying only
    # the LIVE2D_PACKS that survived the NSFW gate keeps the bundle SFW.
    foreach(pack ${LIVE2D_PACKS})
        add_custom_command(TARGET Seelie POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets/packs/${pack}"
                "$<TARGET_BUNDLE_DIR:Seelie>/Contents/Resources/assets/packs/${pack}"
            COMMENT "Bundling pack ${pack} into macOS app"
            VERBATIM)
    endforeach()

    # Copy the rest of assets/ that the runtime expects.
    add_custom_command(TARGET Seelie POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:Seelie>/Contents/Resources/assets"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/assets/animations.json"
            "$<TARGET_BUNDLE_DIR:Seelie>/Contents/Resources/assets/animations.json"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/fonts"
            "$<TARGET_BUNDLE_DIR:Seelie>/Contents/Resources/assets/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/i18n"
            "$<TARGET_BUNDLE_DIR:Seelie>/Contents/Resources/assets/i18n"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/lottie"
            "$<TARGET_BUNDLE_DIR:Seelie>/Contents/Resources/assets/lottie"
        COMMENT "Copying runtime assets into macOS bundle"
    )

    add_custom_command(TARGET Seelie POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env python3
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/hide_dock_icon.py"
            "$<TARGET_BUNDLE_DIR:Seelie>/Contents/Info.plist"
        COMMENT "Hiding Seelie from Dock (LSUIElement)"
    )
endif()

qt_generate_deploy_app_script(
    TARGET Seelie
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)

if(APPLE)
    # On Apple Silicon, the linker ad-hoc signs binaries. install_name_tool
    # (used by macdeployqt/qt_deploy_runtime_dependencies) refuses to modify
    # signed binaries, producing "malformed object" errors and corrupting
    # the bundle. Strip all signatures recursively before deployment, then
    # re-sign the final bundle.
    install(CODE "
        message(STATUS \"Stripping code signatures before deployment...\")
        execute_process(
            COMMAND find \"\${CMAKE_INSTALL_PREFIX}/Seelie.app\" -type f \\( -name \"*.dylib\" -o -name \"*.framework\" \\) -exec codesign --remove-signature {} +
            COMMAND find \"\${CMAKE_INSTALL_PREFIX}/Seelie.app\" -type f -exec sh -c \"file \\\"\$1\\\" | grep -q 'Mach-O' && codesign --remove-signature \\\"\$1\\\"\" _ {} \\;
            OUTPUT_VARIABLE _STRIP_OUT
            ERROR_VARIABLE _STRIP_ERR
            RESULT_VARIABLE _STRIP_RES
        )
        if(NOT _STRIP_RES EQUAL 0)
            message(WARNING \"Signature stripping had issues: \${_STRIP_ERR}\")
        endif()
    ")
endif()

install(SCRIPT ${deploy_script})

if(APPLE)
    # Re-sign the deployed bundle after all files are in place
    install(CODE "
        message(STATUS \"Re-signing deployed app bundle...\")
        execute_process(
            COMMAND codesign --force --deep --sign - \"\${CMAKE_INSTALL_PREFIX}/Seelie.app\"
            RESULT_VARIABLE _SIGN_RES
        )
        if(NOT _SIGN_RES EQUAL 0)
            message(WARNING \"codesign failed with exit code: \${_SIGN_RES}\")
        endif()
    ")
endif()

# --- Set Seelie as default target for Qt Creator -----------------------------
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Seelie)

# --- Tests -------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# --- Inno Setup staging ------------------------------------------------------
# Stages the deployed Seelie + bundled assets into ${CMAKE_BINARY_DIR}/staging/
# in the flat layout that ISCC packs into the final installer.
#
# Build with:  cmake --build . --target inno_stage
# Then run:    python scripts/build_inno.py --skip-stage
#
# The QtIFW (binarycreator) pipeline that used to live here was removed when
# we switched to Inno Setup — it had no working language picker, and Qt IFW
# 4.11 doesn't ship a zh_CN chrome translation. Inno does both natively.
set(SEELIE_STAGE_DIR  ${CMAKE_BINARY_DIR}/staging)
set(SEELIE_STAGE_TS   ${CMAKE_BINARY_DIR}/inno_stage.stamp)

# windeployqt assumes Seelie.exe lives in <root>/bin/ and writes
#   [Paths]
#   Prefix = ..
# so Qt walks up one level to find <root>/plugins/. We flatten bin/ into
# the staging root (Windows installers conventionally don't have a bin/
# subdir under TargetDir), which leaves `Prefix = ..` pointing at the
# *parent* of the install dir — Qt then can't find qwindows.dll and aborts
# with "no Qt platform plugin could be initialized." Overwrite qt.conf
# with a flat-layout version that points Prefix at the install dir itself.
file(WRITE ${CMAKE_BINARY_DIR}/qt.conf.flat "[Paths]\nPrefix = .\n")

add_custom_command(
    OUTPUT ${SEELIE_STAGE_TS}
    # Wipe + recreate so a previous-build's leftovers can't sneak into the
    # installer. cmake --install populates from scratch.
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${SEELIE_STAGE_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SEELIE_STAGE_DIR}
    COMMAND ${CMAKE_COMMAND}
            --install ${CMAKE_BINARY_DIR}
            --prefix  ${SEELIE_STAGE_DIR}
            --config  $<CONFIG>
    # Flatten the GNUInstallDirs bin/ layer so Seelie.exe sits at the
    # staging root.
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SEELIE_STAGE_DIR}/bin ${SEELIE_STAGE_DIR}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${SEELIE_STAGE_DIR}/bin
    # Replace the bin/-relative qt.conf windeployqt wrote (Prefix=..)
    # with the flat-layout version (Prefix=.).
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/qt.conf.flat
            ${SEELIE_STAGE_DIR}/qt.conf
    # Drop transitive install artifacts (rlottie headers, glew/.a stubs,
    # CMake configs, pkgconfig) — runtime install doesn't need them.
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${SEELIE_STAGE_DIR}/include ${SEELIE_STAGE_DIR}/lib
    # Ship the portable Seelie.ini template — ConfigManager reads it on
    # startup as a default-values layer underneath user-scope settings,
    # so the installer can pre-set updateServerEndpoint / language /
    # ipcEndpoint per build flavor without forcing every user to edit
    # their roaming config.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/Seelie.ini
            ${SEELIE_STAGE_DIR}/Seelie.ini
    COMMAND ${CMAKE_COMMAND} -E touch ${SEELIE_STAGE_TS}
    DEPENDS Seelie ${ALL_PACK_SPKS}
    COMMENT "Staging Seelie for Inno Setup packaging in ${SEELIE_STAGE_DIR}"
    VERBATIM)
add_custom_target(inno_stage DEPENDS ${SEELIE_STAGE_TS})

message(STATUS "Inno Setup staging: cmake --build . --target inno_stage  ->  ${SEELIE_STAGE_DIR}")
