# find_package(fmt CONFIG REQUIRED)

add_library(storage_engine_core STATIC
    memtable.cpp
    sstable.cpp
    engine.cpp
    length_encoding.cpp
    wal.cpp
)

myproject_target_setup(storage_engine_core)

add_executable(storage_engine main.cpp)
myproject_target_setup(storage_engine)

target_link_libraries(storage_engine PRIVATE
    storage_engine_core
)
