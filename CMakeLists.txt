cmake_minimum_required(VERSION 3.24)

project(GiflerNative
    VERSION 0.2.0
    DESCRIPTION "Compact native Windows screen recorder"
    LANGUAGES CXX
)

if (NOT WIN32)
    message(FATAL_ERROR "Gifler is a Windows/Win32 project. Configure it on Windows with MSVC.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GIFLER_BUILD_SPIKES "Build disposable proof-of-concept spike executables" ON)
option(GIFLER_BUILD_TESTS "Build unit tests" ON)

add_compile_definitions(
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

if (MSVC)
    add_compile_options(/permissive- /W4 /utf-8 /EHsc)
    add_compile_options(
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/Gy>
    )
    add_link_options(
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()

add_library(gifler_core STATIC
    src/gifler_core/FrameDiffer.cpp
    src/gifler_core/Settings.cpp
)
target_include_directories(gifler_core PUBLIC src)

target_compile_features(gifler_core PUBLIC cxx_std_20)

add_library(gifler_win32 STATIC
    src/gifler_win32/Clipboard.cpp
    src/gifler_win32/Dpi.cpp
    src/gifler_win32/Process.cpp
    src/gifler_win32/ResizeOverlay.cpp
    src/gifler_win32/WindowRegion.cpp
    src/gifler_win32/WicImageWriter.cpp
    src/gifler_win32/Win32Util.cpp
)
target_link_libraries(gifler_win32
    PUBLIC gifler_core
    PRIVATE user32 gdi32 shell32 shlwapi ole32 windowscodecs
)

add_library(gifler_capture_dxgi STATIC
    src/gifler_capture_dxgi/DxgiCaptureProvider.cpp
)
target_link_libraries(gifler_capture_dxgi
    PUBLIC gifler_core gifler_win32
    PRIVATE d3d11 dxgi
)

add_library(gifler_record STATIC
    src/gifler_record/LoopbackAudio.cpp
    src/gifler_record/RecorderSession.cpp
)
target_link_libraries(gifler_record PUBLIC gifler_core gifler_capture_dxgi PRIVATE ole32 uuid)

add_library(gifler_export STATIC
    src/gifler_export/AudioWave.cpp
    src/gifler_export/EncoderDiscovery.cpp
    src/gifler_export/GifExportPlanner.cpp
    src/gifler_export/GifRecordingExporter.cpp
)
target_link_libraries(gifler_export PUBLIC gifler_core gifler_win32 PRIVATE shlwapi)

add_library(gifler_editor STATIC
    src/gifler_editor/EditorWindow.cpp
)
target_link_libraries(gifler_editor PUBLIC gifler_core gifler_win32)

add_executable(Gifler WIN32
    src/gifler_app/main.cpp
    src/gifler_app/MainWindow.cpp
    src/gifler_app/MainWindowChrome.cpp
    src/gifler_app/FpsDialog.cpp
)
target_link_libraries(Gifler
    PRIVATE gifler_core gifler_win32 gifler_capture_dxgi gifler_record gifler_export gifler_editor
    PRIVATE comctl32 comdlg32
)

if (MSVC)
    target_link_options(Gifler PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/gifler_app/app.manifest")
endif()

if (GIFLER_BUILD_SPIKES)
    add_executable(spike_s01_window_hole WIN32 spikes/s01_window_hole/main.cpp)
    target_link_libraries(spike_s01_window_hole PRIVATE gifler_core gifler_win32)

    add_executable(spike_s02_dxgi_capture_alignment spikes/s02_dxgi_capture_alignment/main.cpp)
    target_link_libraries(spike_s02_dxgi_capture_alignment PRIVATE gifler_core gifler_win32 gifler_capture_dxgi)

    add_executable(spike_s03_pointer_shape spikes/s03_pointer_shape/main.cpp)
    target_link_libraries(spike_s03_pointer_shape PRIVATE gifler_core gifler_capture_dxgi)

    add_executable(spike_s04_recording_pipeline spikes/s04_recording_pipeline/main.cpp)
    target_link_libraries(spike_s04_recording_pipeline PRIVATE gifler_core gifler_record)

    add_executable(spike_s05_gif_export_planner spikes/s05_gif_export_planner/main.cpp)
    target_link_libraries(spike_s05_gif_export_planner PRIVATE gifler_core gifler_export)

    add_executable(spike_s06_clipboard_filedrop spikes/s06_clipboard_filedrop/main.cpp)
    target_link_libraries(spike_s06_clipboard_filedrop PRIVATE gifler_core gifler_win32)

    add_executable(spike_s07_preview_rendering WIN32 spikes/s07_preview_rendering/main.cpp)
    target_link_libraries(spike_s07_preview_rendering PRIVATE gifler_core gifler_win32)

    add_executable(spike_s08_editor_timeline WIN32 spikes/s08_editor_timeline/main.cpp)
    target_link_libraries(spike_s08_editor_timeline PRIVATE gifler_core gifler_win32 gifler_editor)

    add_executable(spike_s09_video_alternates spikes/s09_video_alternates/main.cpp)
    target_link_libraries(spike_s09_video_alternates PRIVATE gifler_core gifler_export)
endif()

if (GIFLER_BUILD_TESTS)
    enable_testing()
    add_executable(gifler_tests
        tests/main.cpp
    )
    target_link_libraries(gifler_tests PRIVATE gifler_core gifler_export gifler_record)
    add_test(NAME gifler_tests COMMAND gifler_tests)
    add_executable(gifler_export_smoke tests/export_smoke.cpp)
    add_executable(gifler_audio_packet_probe tests/audio_packet_probe.cpp)
    target_link_libraries(gifler_audio_packet_probe PRIVATE gifler_record ole32 uuid)
    target_link_libraries(gifler_export_smoke PRIVATE gifler_export)
    add_executable(gifler_app_smoke tests/app_smoke.cpp src/gifler_app/MainWindow.cpp
        src/gifler_app/MainWindowChrome.cpp src/gifler_app/FpsDialog.cpp)
    target_link_libraries(gifler_app_smoke PRIVATE gifler_core gifler_win32 gifler_capture_dxgi
        gifler_record gifler_export gifler_editor comctl32 comdlg32)
    if (MSVC)
        target_link_options(gifler_app_smoke PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/gifler_app/app.manifest")
    endif()
endif()

install(TARGETS Gifler RUNTIME DESTINATION .)
