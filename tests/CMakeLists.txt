find_package(Catch2 3 CONFIG REQUIRED)

list(APPEND CMAKE_MODULE_PATH ${Catch2_DIR})
include(Catch)

add_executable(unit_tests
    test_memtable.cpp
    test_sstable.cpp
)

myproject_target_setup(unit_tests)

target_link_libraries(unit_tests PRIVATE
    Catch2::Catch2WithMain
    storage_engine_core
)

catch_discover_tests(unit_tests)
