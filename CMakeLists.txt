cmake_minimum_required(VERSION 3.20)
project(MIDIMonitor VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "MIDI Monitor est une application Windows native.")
endif()

add_library(midi_core STATIC
    src/MidiDecoder.cpp
    src/MidiByteStreamParser.cpp
    src/MidiLog.cpp
    src/CsvExporter.cpp
    src/MidiView.cpp
)
target_include_directories(midi_core PUBLIC src)

add_executable(MIDIMonitor WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/MidiInput.cpp
    assets/MIDIMonitor.rc
)
target_include_directories(MIDIMonitor PRIVATE src)
target_compile_definitions(MIDIMonitor PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(MIDIMonitor PRIVATE midi_core winmm comctl32 comdlg32 shell32)
set_target_properties(MIDIMonitor PROPERTIES OUTPUT_NAME "MIDI Monitor")

if(MSVC)
    foreach(target midi_core MIDIMonitor)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()
    # Le manifeste DPI/Common Controls est déjà compilé dans MIDIMonitor.rc.
    target_link_options(MIDIMonitor PRIVATE /MANIFEST:NO)
else()
    target_compile_options(midi_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(MIDIMonitor PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(MIDIMonitor PRIVATE -static -static-libgcc -static-libstdc++)
endif()

add_custom_command(TARGET MIDIMonitor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/dist/windows"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:MIDIMonitor>"
            "${CMAKE_SOURCE_DIR}/dist/windows/MIDI Monitor.exe"
    VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    add_executable(MIDIMonitorTests tests/MidiTests.cpp)
    target_link_libraries(MIDIMonitorTests PRIVATE midi_core)
    if(MSVC)
        target_compile_options(MIDIMonitorTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
        set_property(TARGET MIDIMonitorTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_compile_options(MIDIMonitorTests PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(MIDIMonitorTests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME MIDIMonitorTests COMMAND MIDIMonitorTests)

    add_executable(WinMMIntegrationTests
        tests/WinMMIntegrationTests.cpp
        src/MidiInput.cpp
    )
    target_include_directories(WinMMIntegrationTests PRIVATE src)
    target_compile_definitions(WinMMIntegrationTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(WinMMIntegrationTests PRIVATE midi_core winmm)
    if(MSVC)
        target_compile_options(WinMMIntegrationTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
        set_property(TARGET WinMMIntegrationTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_compile_options(WinMMIntegrationTests PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(WinMMIntegrationTests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME WinMMIntegrationTests COMMAND WinMMIntegrationTests)
    set_tests_properties(WinMMIntegrationTests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 10)

    add_executable(MainWindowStressTests
        tests/MainWindowStressTests.cpp
        src/MainWindow.cpp
        src/MidiInput.cpp
    )
    target_include_directories(MainWindowStressTests PRIVATE src)
    target_compile_definitions(MainWindowStressTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN MIDI_MONITOR_TESTING)
    target_link_libraries(MainWindowStressTests PRIVATE midi_core winmm comctl32 comdlg32 shell32 ole32)
    if(MSVC)
        target_compile_options(MainWindowStressTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
        set_property(TARGET MainWindowStressTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_compile_options(MainWindowStressTests PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(MainWindowStressTests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME MainWindowStressTests COMMAND MainWindowStressTests)
    set_tests_properties(MainWindowStressTests PROPERTIES TIMEOUT 10)
endif()
