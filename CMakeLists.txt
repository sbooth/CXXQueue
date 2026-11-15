cmake_minimum_required(VERSION 3.21)
project(CXXQueue VERSION 0.6.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(CXXQueue INTERFACE)
add_library(spsc::Queue ALIAS CXXQueue)
set_target_properties(CXXQueue PROPERTIES EXPORT_NAME Queue)

target_compile_features(CXXQueue INTERFACE cxx_std_20)

target_include_directories(CXXQueue INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Sources/CXXQueue/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

install(TARGETS CXXQueue EXPORT CXXQueueTargets)

install(DIRECTORY Sources/CXXQueue/include/spsc DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT CXXQueueTargets
    FILE CXXQueueTargets.cmake
    NAMESPACE spsc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CXXQueue
)

option(CXXQUEUE_BUILD_TESTING "Build the CXXQueue tests" ${PROJECT_IS_TOP_LEVEL})

if(BUILD_TESTING)
    find_package(GTest REQUIRED)

    enable_testing()

    add_executable(run_tests test/spsc_queue_test.cpp)

    target_link_libraries(run_tests
        PRIVATE
            spsc::Queue
            GTest::gtest_main
    )

    if(MSVC)
        target_compile_options(run_tests PRIVATE /W4)
    else()
        target_compile_options(run_tests PRIVATE -Wall -Wextra)
    endif()

    include(GoogleTest)
    gtest_discover_tests(run_tests)
endif()
