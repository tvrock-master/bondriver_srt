cmake_minimum_required(VERSION 3.20)
project(BonDriver_SRT VERSION 1.0.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "BonDriver_SRT requires Windows and an MSVC-compatible C++ ABI.")
endif()
include(cmake/DetectMsvcIncludes.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(BONDRIVER_BUILD_TESTS "Build buffer, settings and loopback tests" OFF)
add_subdirectory(cmake/dependencies)

add_library(bondriver_options INTERFACE)
target_compile_definitions(bondriver_options INTERFACE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_compile_options(bondriver_options INTERFACE /utf-8 /W4 /permissive-)
target_include_directories(bondriver_options INTERFACE include src)

add_library(BonDriver_SRT SHARED src/BonDriver.cpp src/Config.cpp src/SrtReceiver.cpp src/TsBuffer.cpp)
target_compile_definitions(BonDriver_SRT PRIVATE BONDRIVER_SRT_EXPORTS)
target_link_libraries(BonDriver_SRT PRIVATE bondriver_options bondriver_srt_sdk)
set_target_properties(BonDriver_SRT PROPERTIES PREFIX "")

install(TARGETS BonDriver_SRT RUNTIME DESTINATION .)
install(FILES config/BonDriver_SRT.ini.example README.md LICENSE THIRD_PARTY.md DESTINATION .)
install(FILES "${SRT_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME SRT.txt)
install(FILES "${MBEDTLS_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME MbedTLS.txt)

if(BONDRIVER_BUILD_TESTS)
    enable_testing()
    add_executable(BufferTests tests/BufferTests.cpp src/TsBuffer.cpp)
    target_link_libraries(BufferTests PRIVATE bondriver_options)
    add_test(NAME Buffer COMMAND BufferTests)

    add_executable(ConfigTests tests/ConfigTests.cpp src/Config.cpp)
    target_link_libraries(ConfigTests PRIVATE bondriver_options)
    add_test(NAME Config COMMAND ConfigTests)

    add_executable(SrtIntegration tests/SrtIntegration.cpp)
    target_link_libraries(SrtIntegration PRIVATE bondriver_options bondriver_srt_sdk)
    add_dependencies(SrtIntegration BonDriver_SRT)
    add_test(NAME SrtIntegration COMMAND SrtIntegration "$<TARGET_FILE:BonDriver_SRT>")
    set_tests_properties(SrtIntegration PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)

    add_executable(MultiReceiverTests tests/MultiReceiverTests.cpp)
    target_link_libraries(MultiReceiverTests PRIVATE bondriver_options bondriver_srt_sdk)
    add_dependencies(MultiReceiverTests BonDriver_SRT)
    add_test(NAME MultiReceiver COMMAND MultiReceiverTests "$<TARGET_FILE:BonDriver_SRT>")
    set_tests_properties(MultiReceiver PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endif()
