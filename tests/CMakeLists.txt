add_executable(vf_core_tests core_tests.cpp)
target_link_libraries(vf_core_tests PRIVATE vf::core)

if(MSVC)
    target_compile_options(vf_core_tests PRIVATE /W4)
else()
    target_compile_options(vf_core_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME vf_core_tests COMMAND vf_core_tests)
