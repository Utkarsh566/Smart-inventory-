cmake_minimum_required(VERSION 3.21)
project(inventory_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Crow CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Everything except main.cpp, so the tests can link the same code.
add_library(inventory_core STATIC
    src/repository.cpp
    src/inventory_service.cpp)
target_include_directories(inventory_core PUBLIC src)
target_link_libraries(inventory_core PUBLIC
    unofficial::sqlite3::sqlite3
    nlohmann_json::nlohmann_json)

add_executable(inventory_server src/main.cpp)
target_link_libraries(inventory_server PRIVATE inventory_core Crow::Crow)

add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE inventory_core)

if (MSVC)
    target_compile_options(inventory_core PUBLIC /utf-8 /W4)
endif()
if (WIN32)
    target_compile_definitions(inventory_server PRIVATE _WIN32_WINNT=0x0A00)
endif()

enable_testing()
add_test(NAME core_tests
         COMMAND core_tests ${CMAKE_SOURCE_DIR}/data
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
