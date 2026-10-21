add_executable(datascythe_tests test_core.cpp)
target_link_libraries(datascythe_tests PRIVATE datascythe_core)
if(WIN32)
    target_link_libraries(datascythe_tests PRIVATE bcrypt)
endif()

add_test(NAME core_tests COMMAND datascythe_tests)