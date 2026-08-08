cmake_minimum_required(VERSION 3.16)

project(zhijing_edge_gateway LANGUAGES CXX)

add_executable(zhijing_edge_gateway
    src/main.cpp
)

target_compile_features(zhijing_edge_gateway PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(zhijing_edge_gateway PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(zhijing_edge_gateway PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(zhijing_edge_gateway PRIVATE ws2_32)
endif()
