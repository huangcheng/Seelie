# Source files shared between the app and tests (excluding main.cpp)
set(SEELIEPET_LIB_SOURCES
    ${CMAKE_SOURCE_DIR}/src/IPCServer.cpp
    ${CMAKE_SOURCE_DIR}/src/IPCServer.h
    ${CMAKE_SOURCE_DIR}/src/UDPWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/UDPWorker.h
    ${CMAKE_SOURCE_DIR}/src/EventRouter.cpp
    ${CMAKE_SOURCE_DIR}/src/EventRouter.h
    ${CMAKE_SOURCE_DIR}/src/SpriteAnimationEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/SpriteAnimationEngine.h
    ${CMAKE_SOURCE_DIR}/src/LottieAnimationEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/LottieAnimationEngine.h
    ${CMAKE_SOURCE_DIR}/src/LottieEffectOverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/LottieEffectOverlay.h
    ${CMAKE_SOURCE_DIR}/src/CharacterPack.cpp
    ${CMAKE_SOURCE_DIR}/src/CharacterPack.h
    ${CMAKE_SOURCE_DIR}/src/CharacterPackManager.cpp
    ${CMAKE_SOURCE_DIR}/src/CharacterPackManager.h
    ${CMAKE_SOURCE_DIR}/src/TipWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/TipWidget.h
    ${CMAKE_SOURCE_DIR}/src/ECGWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ECGWidget.h
    ${CMAKE_SOURCE_DIR}/src/MacFocusFix.h
    $<$<PLATFORM_ID:Darwin>:${CMAKE_SOURCE_DIR}/src/MacFocusFix.mm>
    ${CMAKE_SOURCE_DIR}/src/TipsEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/TipsEngine.h
    ${CMAKE_SOURCE_DIR}/src/TipsCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/TipsCatalog.h
    ${CMAKE_SOURCE_DIR}/src/SayingPool.cpp
    ${CMAKE_SOURCE_DIR}/src/SayingPool.h
    ${CMAKE_SOURCE_DIR}/src/IdleBehaviorEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/IdleBehaviorEngine.h
    ${CMAKE_SOURCE_DIR}/src/ConfigManager.cpp
    ${CMAKE_SOURCE_DIR}/src/ConfigManager.h
    ${CMAKE_SOURCE_DIR}/src/StatisticsPersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/StatisticsPersistence.h
    ${CMAKE_SOURCE_DIR}/src/StatisticsManager.cpp
    ${CMAKE_SOURCE_DIR}/src/StatisticsManager.h
    ${CMAKE_SOURCE_DIR}/src/AutoStartManager.cpp
    ${CMAKE_SOURCE_DIR}/src/AutoStartManager.h
    ${CMAKE_SOURCE_DIR}/src/PlatformWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/PlatformWindow.h
    ${CMAKE_SOURCE_DIR}/src/StyleUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/StyleUtils.h
    ${CMAKE_SOURCE_DIR}/src/AnimationEngine.h
    ${CMAKE_SOURCE_DIR}/src/CanonicalEvents.h
    ${CMAKE_SOURCE_DIR}/src/CharacterPackLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/CharacterPackLoader.h
    ${CMAKE_SOURCE_DIR}/src/FullscreenWatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/FullscreenWatcher.h
    ${CMAKE_SOURCE_DIR}/src/SystemContextEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/SystemContextEngine.h
    ${CMAKE_SOURCE_DIR}/src/MoodEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/MoodEngine.h
    ${CMAKE_SOURCE_DIR}/src/StrokeDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/StrokeDetector.h
    ${CMAKE_SOURCE_DIR}/src/RecallDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/RecallDialog.h
    ${CMAKE_SOURCE_DIR}/src/RecallFilter.h
    ${CMAKE_SOURCE_DIR}/src/PersonaDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/PersonaDialog.h
    ${CMAKE_SOURCE_DIR}/src/PetStateMachine.cpp
    ${CMAKE_SOURCE_DIR}/src/PetStateMachine.h
    ${CMAKE_SOURCE_DIR}/src/MemoryManager.cpp
    ${CMAKE_SOURCE_DIR}/src/MemoryManager.h
    ${CMAKE_SOURCE_DIR}/src/EmbeddingService.cpp
    ${CMAKE_SOURCE_DIR}/src/EmbeddingService.h
    ${CMAKE_SOURCE_DIR}/thirdparty/miniz/miniz.c
    ${CMAKE_SOURCE_DIR}/src/tts/ITTSProvider.h
    ${CMAKE_SOURCE_DIR}/src/tts/ProviderConfig.h
    ${CMAKE_SOURCE_DIR}/src/tts/TTSProviderRegistry.h
    ${CMAKE_SOURCE_DIR}/src/tts/TTSProviderRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/tts/StepFunHTTPProvider.h
    ${CMAKE_SOURCE_DIR}/src/tts/StepFunHTTPProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/tts/MiniMaxHTTPProvider.h
    ${CMAKE_SOURCE_DIR}/src/tts/MiniMaxHTTPProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/tts/AzureSpeechProvider.h
    ${CMAKE_SOURCE_DIR}/src/tts/AzureSpeechProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/tts/OpenAITTSProvider.h
    ${CMAKE_SOURCE_DIR}/src/tts/OpenAITTSProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/tts/TTSVoiceCache.h
    ${CMAKE_SOURCE_DIR}/src/tts/TTSVoiceCache.cpp
    ${CMAKE_SOURCE_DIR}/src/llm/LLMProfile.h
    ${CMAKE_SOURCE_DIR}/src/llm/LLMProvider.h
    ${CMAKE_SOURCE_DIR}/src/llm/LLMProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/PersonaPool.h
    ${CMAKE_SOURCE_DIR}/src/PersonaPool.cpp
    ${CMAKE_SOURCE_DIR}/src/PersonaEngine.h
    ${CMAKE_SOURCE_DIR}/src/PersonaEngine.cpp
)

# Manual live tests. Compiled by `cmake --build . --target test_tts_live`
# but not added to ctest — they require real credentials and network.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/manual)
qt_add_executable(test_tts_live
    manual/test_tts_live.cpp
    ${SEELIEPET_LIB_SOURCES}
)
target_link_libraries(test_tts_live PRIVATE
    Qt::Core Qt::Gui Qt::Widgets Qt::Network Qt::Multimedia Qt::Sql Qt::Test rlottie::rlottie)
target_include_directories(test_tts_live PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${rlottie_SOURCE_DIR}/inc)
target_compile_definitions(test_tts_live PRIVATE
    SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    PROJECT_VERSION="${PROJECT_VERSION}"
    SEELIE_DEFAULT_UPDATE_ENDPOINT="${SEELIE_DEFAULT_UPDATE_ENDPOINT}")
if(WIN32)
    target_link_libraries(test_tts_live PRIVATE dwmapi)
endif()
if(APPLE)
    target_link_libraries(test_tts_live PRIVATE "-framework AppKit" "-framework CoreGraphics")
endif()

# Manual live LLM probe. Compiled by `cmake --build . --target test_llm_live`
# but not added to ctest — requires real credentials and network.
qt_add_executable(test_llm_live
    manual/test_llm_live.cpp
    ${SEELIEPET_LIB_SOURCES}
)
target_link_libraries(test_llm_live PRIVATE
    Qt::Core Qt::Gui Qt::Widgets Qt::Network Qt::Multimedia Qt::Sql Qt::Test rlottie::rlottie)
target_include_directories(test_llm_live PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${rlottie_SOURCE_DIR}/inc)
target_compile_definitions(test_llm_live PRIVATE
    SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    PROJECT_VERSION="${PROJECT_VERSION}"
    SEELIE_DEFAULT_UPDATE_ENDPOINT="${SEELIE_DEFAULT_UPDATE_ENDPOINT}")
if(WIN32)
    target_link_libraries(test_llm_live PRIVATE dwmapi)
endif()
if(APPLE)
    target_link_libraries(test_llm_live PRIVATE "-framework AppKit" "-framework CoreGraphics")
endif()

set(TEST_SOURCES
    test_ipc_animations.cpp
    test_ecg.cpp
    test_gaming_mode.cpp
    test_system_context.cpp
    test_mood_engine.cpp
    test_stroke_detector.cpp
    test_recall_dialog.cpp
    test_pet_state_machine.cpp
    test_tts_providers.cpp
    test_tts_engine.cpp
    test_tts_voice_cache.cpp
    test_autostart_manager.cpp
    test_platform_window.cpp
    test_character_pack_manager_errors.cpp
    test_character_pack_persona.cpp
    test_llm_profile.cpp
    test_llm_provider.cpp
    test_persona_pool.cpp
    test_persona_engine.cpp
    test_idle_behavior.cpp
)

foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    qt_add_executable(${test_name}
        ${test_src}
        ${SEELIEPET_LIB_SOURCES}
    )
    target_link_libraries(${test_name}
        PRIVATE
            Qt::Core
            Qt::Gui
            Qt::Widgets
            Qt::Network
            Qt::Multimedia
            Qt::Sql
            Qt::Test
            rlottie::rlottie
    )
    if(WIN32)
        # TipWidget.cpp is shared with the main app and uses
        # DwmSetWindowAttribute to opt out of Win11 DWM chrome.
        target_link_libraries(${test_name} PRIVATE dwmapi)
    endif()
    if(APPLE)
        # MacFocusFix.mm uses NSWindow / NSPanel from AppKit.
        # FullscreenWatcher.cpp uses CGWindowListCopyWindowInfo from CoreGraphics.
        # SystemContextEngine.cpp uses IOPSCopyPowerSourcesInfo from IOKit.
        target_link_libraries(${test_name} PRIVATE "-framework AppKit" "-framework CoreGraphics" "-framework IOKit")
    endif()
    if(UNIX AND NOT APPLE)
        # X11: FullscreenWatcher (Spec 2) and SystemContextEngine OS-idle probe.
        # Optional: absent X11 dev files keep both features as runtime no-ops.
        find_package(X11)
        if(X11_FOUND)
            target_compile_definitions(${test_name} PRIVATE SEELIE_HAS_X11=1)
            target_include_directories(${test_name} PRIVATE ${X11_INCLUDE_DIR})
            target_link_libraries(${test_name} PRIVATE ${X11_LIBRARIES})
            if(X11_Xss_FOUND)
                target_compile_definitions(${test_name} PRIVATE SEELIE_HAS_XSS=1)
                target_link_libraries(${test_name} PRIVATE ${X11_Xss_LIB})
            endif()
        else()
            message(STATUS "X11 not found — FullscreenWatcher and OS-idle probe will be no-ops")
        endif()
    endif()
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/thirdparty/cgltf
        ${rlottie_SOURCE_DIR}/inc
    )
    target_compile_definitions(${test_name} PRIVATE
        SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        PROJECT_VERSION="${PROJECT_VERSION}"
        SEELIE_DEFAULT_UPDATE_ENDPOINT="${SEELIE_DEFAULT_UPDATE_ENDPOINT}"
    )
    add_test(NAME ${test_name} COMMAND ${test_name})

    # TipsCatalog resource bundle — needed by tests that resolve tip text.
    qt_add_resources(${test_name} "tips_i18n"
        PREFIX "/i18n"
        BASE "${CMAKE_SOURCE_DIR}/assets"
        FILES
            ${CMAKE_SOURCE_DIR}/assets/i18n/tips.en.json
            ${CMAKE_SOURCE_DIR}/assets/i18n/tips.zh_CN.json
    )

    find_package(Qt6 6.5 COMPONENTS HttpServer QUIET)
    if(Qt6HttpServer_FOUND)
        target_link_libraries(${test_name} PRIVATE Qt6::HttpServer)
        target_compile_definitions(${test_name} PRIVATE SEELIE_HAS_QHTTPSERVER=1)
    endif()

    # On Windows the Qt6 DLLs live in Qt's bin/, which is not on the PATH that
    # ctest inherits — without this, the test exe exits 0xc0000135 (DLL not
    # found) before main() runs. Prepend Qt's bin dir to PATH for the test.
    if(WIN32)
        get_target_property(_qt_qmake_loc Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin_dir "${_qt_qmake_loc}" DIRECTORY)
        # Escape ';' in $ENV{PATH} so CMake doesn't split it as a list when
        # serialising the ENVIRONMENT property.
        string(REPLACE ";" "\\;" _ctest_path "$ENV{PATH}")
        set_tests_properties(${test_name} PROPERTIES
            ENVIRONMENT "PATH=${_qt_bin_dir}\\;${_ctest_path}")
    endif()
endforeach()

# --- MemoryManager test (standalone — does not need the full app lib) --------
add_executable(test_memory_manager
    test_memory_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/MemoryManager.cpp
)
target_link_libraries(test_memory_manager PRIVATE Qt::Sql Qt::Core Qt::Test)
target_include_directories(test_memory_manager PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_memory_manager COMMAND test_memory_manager)
if(WIN32)
    get_target_property(_qt_qmake_loc Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake_loc}" DIRECTORY)
    string(REPLACE ";" "\\;" _ctest_path "$ENV{PATH}")
    set_tests_properties(test_memory_manager PROPERTIES
        ENVIRONMENT "PATH=${_qt_bin_dir}\\;${_ctest_path}")
endif()

# --- MemoryManager v2 migration test (standalone — mirrors block above) -----
add_executable(test_memory2
    test_memory2.cpp
    ${CMAKE_SOURCE_DIR}/src/MemoryManager.cpp
    ${CMAKE_SOURCE_DIR}/src/EmbeddingService.cpp)
target_link_libraries(test_memory2 PRIVATE Qt::Core Qt::Sql Qt::Network Qt::Test)
target_include_directories(test_memory2 PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_memory2 COMMAND test_memory2)
if(WIN32)
    get_target_property(_qt_qmake_loc Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake_loc}" DIRECTORY)
    string(REPLACE ";" "\\;" _ctest_path "$ENV{PATH}")
    set_tests_properties(test_memory2 PROPERTIES
        ENVIRONMENT "PATH=${_qt_bin_dir}\\;${_ctest_path}")
endif()
