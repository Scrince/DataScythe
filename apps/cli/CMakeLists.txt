add_executable(datascythe main.cpp)
set_target_properties(datascythe PROPERTIES OUTPUT_NAME "datascythe")

target_link_libraries(datascythe PRIVATE datascythe_core)

if(WIN32)
    target_link_libraries(datascythe PRIVATE setupapi)
    include(${CMAKE_SOURCE_DIR}/cmake/DeployWindows.cmake)
    datascythe_deploy_mingw(datascythe)
endif()

install(TARGETS datascythe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})