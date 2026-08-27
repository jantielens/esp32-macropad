include(FetchContent)
include(GoogleTest)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

function(add_host_gtest target)
    add_executable(${target} ${ARGN})
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/tests
        ${PROJECT_SOURCE_DIR}/src/app
    )
    target_link_libraries(${target} PRIVATE GTest::gtest_main)
    gtest_discover_tests(${target})
endfunction()

add_host_gtest(test_epaper_carousel
    test_epaper_carousel.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper/epaper_carousel.cpp
)
target_compile_definitions(test_epaper_carousel PRIVATE HAS_EPAPER=1)

add_host_gtest(test_epaper_schedule
    test_epaper_schedule.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper/epaper_schedule.cpp
)
target_compile_definitions(test_epaper_schedule PRIVATE HAS_EPAPER=1)

add_host_gtest(test_button_shadow_color
    test_button_shadow_color.cpp
)