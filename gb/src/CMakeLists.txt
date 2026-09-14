add_library(gbemu_core
    core/cartridge.cpp
    core/bus.cpp
    core/cpu.cpp
    core/ppu.cpp
    core/timer.cpp
)

target_include_directories(gbemu_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(gbemu_core PUBLIC
    project_warnings
)

add_executable(emulator
    main.cpp
)

target_link_libraries(emulator PRIVATE
    gbemu_core
    SDL3::SDL3
)