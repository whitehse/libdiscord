cmake_minimum_required(VERSION 3.20)
project(libdiscord LANGUAGES C VERSION 0.1.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# libdiscord - Discord bot support library on top of librest
add_library(discord STATIC src/discord.c)

target_include_directories(discord PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Dependency on librest - located in sibling location
find_path(REST_INCLUDE_DIR rest.h PATHS /home/dwhite/librest/include)
find_library(REST_LIBRARY NAMES rest PATHS /home/dwhite/librest/build)
if(REST_INCLUDE_DIR AND REST_LIBRARY)
    target_include_directories(discord PUBLIC ${REST_INCLUDE_DIR})
    target_link_libraries(discord PUBLIC ${REST_LIBRARY})
    message(STATUS "Found librest: ${REST_LIBRARY}")
else()
    message(WARNING "librest not found at expected paths; Discord REST/Gateway integration will be added later. Skeleton builds standalone for now.")
endif()

# Dependency on shaggy (libhttp2) - transitively via librest, but also direct for headers
find_path(SHAGGY_INCLUDE_DIR http2.h PATHS /home/dwhite/shaggy/include)
find_library(SHAGGY_LIBRARY NAMES http2 PATHS /home/dwhite/shaggy/build)
if(SHAGGY_INCLUDE_DIR AND SHAGGY_LIBRARY)
    target_include_directories(discord PUBLIC ${SHAGGY_INCLUDE_DIR})
    target_link_libraries(discord PUBLIC ${SHAGGY_LIBRARY})
    message(STATUS "Found shaggy: ${SHAGGY_LIBRARY}")
else()
    message(WARNING "shaggy not found at expected paths; HTTP/2 transport will be added later.")
endif()

include(GNUInstallDirs)
install(TARGETS discord EXPORT discord-targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT discord-targets FILE discord-config.cmake NAMESPACE discord:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/discord)

enable_testing()

# Smoke test (ADR 004 style)
add_executable(discord_smoke_test tests/test_discord_smoke.c)
target_link_libraries(discord_smoke_test PRIVATE discord)
add_test(NAME discord_smoke COMMAND discord_smoke_test)

# Dialectic client/server test (paired buffer exchange, ADR 004)
add_executable(discord_dialectic_test tests/test_discord_dialectic.c)
target_link_libraries(discord_dialectic_test PRIVATE discord)
add_test(NAME discord_dialectic COMMAND discord_dialectic_test)

# Fuzz harness (optional — requires -fsanitize=fuzzer)
# Build with: cmake -B build -S . -DENABLE_FUZZ=ON && cmake --build build
option(ENABLE_FUZZ "Build fuzz harness (requires libFuzzer)" OFF)
if(ENABLE_FUZZ)
    add_executable(fuzz_discord tests/fuzz_discord.c)
    target_link_libraries(fuzz_discord PRIVATE discord)
    target_compile_options(fuzz_discord PRIVATE -fsanitize=fuzzer,address -g -O1)
    target_link_options(fuzz_discord PRIVATE -fsanitize=fuzzer,address)
endif()

# Example bot program (pure C, no sockets required)
add_executable(discord_bot_example examples/discord_bot_example.c)
target_link_libraries(discord_bot_example PRIVATE discord)
target_include_directories(discord_bot_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
