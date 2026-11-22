cmake_minimum_required(VERSION 3.22)
project(ksystemstats_bridge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(KF6CoreAddons REQUIRED)
find_package(KSysGuard REQUIRED)

add_library(ksystemstats_plugin_bridge MODULE
    src/bridgeplugin.cpp
    src/bridgeplugin.h
)

set_target_properties(ksystemstats_plugin_bridge PROPERTIES
    PREFIX ""
)

target_link_libraries(ksystemstats_plugin_bridge PRIVATE
    Qt6::Core
    KF6::CoreAddons
    KSysGuard::Formatter
    KSysGuard::SystemStats
)

install(TARGETS ksystemstats_plugin_bridge
    LIBRARY DESTINATION lib/qt6/plugins/ksystemstats
)
