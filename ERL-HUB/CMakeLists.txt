cmake_minimum_required(VERSION 3.16)
project(ERL_HUB VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ERL_HUB_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /sdl)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -fstack-protector-strong)
    add_link_options(-fstack-protector-strong)
endif()

if(ERL_HUB_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "ERL_HUB_ENABLE_SANITIZERS requires GCC or Clang")
    endif()
endif()

add_library(erl_hub_core
    src/types.cpp
    src/inventory.cpp
    src/vehicles.cpp
    src/logistics.cpp
    src/persistence.cpp
    src/application.cpp
)

target_include_directories(erl_hub_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(erl_hub src/main.cpp)
target_link_libraries(erl_hub PRIVATE erl_hub_core)

include(CTest)
if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/tests.cpp")
    add_executable(erl_hub_tests tests/tests.cpp)
    target_link_libraries(erl_hub_tests PRIVATE erl_hub_core)
    add_test(NAME erl_hub_tests COMMAND erl_hub_tests)
elseif(BUILD_TESTING)
    message(STATUS "Test sources are not included in this clean source archive; skipping test target.")
endif()
