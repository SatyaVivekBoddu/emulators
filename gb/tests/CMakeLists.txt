add_executable(unit_tests
    test_main.cpp
)

target_link_libraries(unit_tests PRIVATE
    gbemu_core
    doctest::doctest
    project_warnings
)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(unit_tests)