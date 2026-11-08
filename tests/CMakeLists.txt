add_executable(vf_core_tests core_tests.cpp)
target_link_libraries(vf_core_tests PRIVATE vf::core)

if(MSVC)
    target_compile_options(vf_core_tests PRIVATE /W4)
else()
    target_compile_options(vf_core_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME vf_core_tests COMMAND vf_core_tests)

if(VF_WITH_NAM)
    add_executable(vf_nam_tests nam_tests.cpp)
    target_link_libraries(vf_nam_tests PRIVATE vf::nam vf::core)
    target_compile_definitions(vf_nam_tests PRIVATE VF_NAM_EXAMPLES="${VF_NAM_DIR}/example_models")
    add_test(NAME vf_nam_tests COMMAND vf_nam_tests)
endif()
