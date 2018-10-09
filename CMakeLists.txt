cmake_minimum_required(VERSION 3.8)
project(aegisbot VERSION 1.0 LANGUAGES CXX)

find_package(PkgConfig)

find_package(Aegis REQUIRED)

add_executable(aegisbot
src/main.cpp
src/AegisBot.cpp
src/Guild.cpp
src/Management.cpp
src/mod_announcer.cpp
src/mod_auction.cpp
src/mod_automod.cpp
src/mod_autoresponder.cpp
src/mod_autoroles.cpp
src/mod_log.cpp
src/mod_moderation.cpp
src/mod_music.cpp
src/mod_tag.cpp
src/Module.cpp
src/UserControl.cpp)

set_target_properties(aegisbot PROPERTIES CXX_STANDARD 17)
set_target_properties(aegisbot PROPERTIES CXX_STANDARD_REQUIRED ON)

target_compile_definitions(aegisbot PRIVATE AEGIS_PROFILING AEGIS_DEBUG_HISTORY)

target_link_libraries(aegisbot PRIVATE ${Aegis_LIBRARIES})
target_include_directories(aegisbot PUBLIC ${CMAKE_SOURCE_DIR}/src ${Aegis_INCLUDE_DIRS})
target_compile_options(aegisbot PUBLIC ${Aegis_CFLAGS_OTHER})