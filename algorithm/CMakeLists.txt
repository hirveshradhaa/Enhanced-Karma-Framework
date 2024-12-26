cmake_minimum_required(VERSION 3.12)
project(Karma C CXX)

set(PROJECT_NAME "Karma")
set(PROJECT_VERSION_MAJOR "1")
set(PROJECT_VERSION_MINOR "0")
set(PROJECT_VERSION_PATCH "0")
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fPIC -O0 -Wall -pedantic -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fPIC -Wall -pedantic -Wextra")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -fPIC -O3 -g -Wall -pedantic -Wextra")

set(CMAKE_BUILD_TYPE RelWithDebInfo)

# Define source files
set(SOURCES
    src/karma_allocator.cpp
    src/bheap.cpp
    src/adaptive_credit_manager.cpp
)

# Define include directories
set(INCLUDE_DIRS
    include
)

# Create a static library
add_library(libkarma STATIC ${SOURCES})
target_include_directories(libkarma PUBLIC ${INCLUDE_DIRS})

# Create the example executable
add_executable(example example/example.cpp)
target_include_directories(example PUBLIC ${INCLUDE_DIRS})
target_link_libraries(example PRIVATE libkarma)

# Enable warnings
if(MSVC)
    target_compile_options(libkarma PRIVATE /W4 /WX)
    target_compile_options(example PRIVATE /W4 /WX)
else()
    target_compile_options(libkarma PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(example PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
