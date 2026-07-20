cmake_minimum_required(VERSION 3.15)
project(CXXQueue VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(CXXQueue
    Sources/CXXQueue/spsc/Queue.cpp
    Sources/CXXQueue/include/spsc/Queue.hpp
)
add_library(spsc::Queue ALIAS CXXQueue)

target_include_directories(CXXQueue PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Sources/CXXQueue/include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(CXXQueue PRIVATE /W4)
else()
    target_compile_options(CXXQueue PRIVATE -Wall -Wextra)
endif()

include(GNUInstallDirs)

install(TARGETS CXXQueue
    EXPORT CXXQueueTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY Sources/CXXQueue/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT CXXQueueTargets
    FILE CXXQueueTargets.cmake
    NAMESPACE spsc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CXXQueue
)

option(BUILD_TESTING "Build tests" ON)

if(BUILD_TESTING)
    find_package(GTest REQUIRED)

    enable_testing()

    add_executable(run_tests test/spsc_queue_test.cpp)

    target_link_libraries(run_tests
        PRIVATE
            spsc::Queue
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(run_tests)
endif()
