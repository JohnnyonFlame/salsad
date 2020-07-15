cmake_minimum_required(VERSION 3.2)
project(salsad)

find_package(ALSA REQUIRED)

add_executable(${PROJECT_NAME} salsad.c) 
target_link_libraries(${PROJECT_NAME} ${ALSA_LIBRARY})
target_include_directories(${PROJECT_NAME} 
                           PRIVATE EXTERNAL ${ALSA_INCLUDE_DIR})

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
