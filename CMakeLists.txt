cmake_minimum_required(VERSION 3.2)
project(salsad)

find_package(ALSA REQUIRED)

add_executable(salsad salsad.c) 
target_link_libraries(salsad ${ALSA_LIBRARY})
target_include_directories(salsad PUBLIC EXTERNAL ${ALSA_INCLUDE_DIR})
target_compile_definitions(salsad PUBLIC)