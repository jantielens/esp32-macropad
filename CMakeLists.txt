cmake_minimum_required(VERSION 3.20)

project(esp32_macropad_host_tests LANGUAGES CXX)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()