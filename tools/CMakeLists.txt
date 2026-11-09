add_executable(vf_calibrate vf_calibrate.cpp)
target_link_libraries(vf_calibrate PRIVATE vf::core)

add_executable(vf_fx_catalog vf_fx_catalog.cpp)
target_link_libraries(vf_fx_catalog PRIVATE vf::core)

add_executable(vf_cab_catalog vf_cab_catalog.cpp)
target_link_libraries(vf_cab_catalog PRIVATE vf::core)
