
function(datascythe_deploy_mingw target_name)
    if(NOT WIN32 OR NOT MINGW)
        return()
    endif()

    set(MINGW_BIN_DIR "C:/Qt/Tools/mingw1310_64/bin")
    if(NOT EXISTS "${MINGW_BIN_DIR}")
        return()
    endif()

    foreach(runtime IN ITEMS libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
        if(EXISTS "${MINGW_BIN_DIR}/${runtime}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${MINGW_BIN_DIR}/${runtime}"
                        "$<TARGET_FILE_DIR:${target_name}>"
                COMMENT "Deploy ${runtime} for ${target_name}")
        endif()
    endforeach()
endfunction()

function(datascythe_deploy_qt target_name)
    if(NOT WIN32 OR NOT TARGET ${target_name})
        return()
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(DATASCYTHE_DXC_ARCH x64)
    else()
        set(DATASCYTHE_DXC_ARCH x86)
    endif()

    set(DATASCYTHE_DXC_SEARCH_DIRS
        "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/${DATASCYTHE_DXC_ARCH}"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/${DATASCYTHE_DXC_ARCH}"
        "C:/Windows/System32/Microsoft-Edge-WebView"
    )

    find_file(DATASCYTHE_DXCOMPILER_DLL dxcompiler.dll PATHS ${DATASCYTHE_DXC_SEARCH_DIRS} NO_DEFAULT_PATH)
    find_file(DATASCYTHE_DXIL_DLL dxil.dll PATHS ${DATASCYTHE_DXC_SEARCH_DIRS} NO_DEFAULT_PATH)

    foreach(dxc_dll IN ITEMS "${DATASCYTHE_DXCOMPILER_DLL}" "${DATASCYTHE_DXIL_DLL}")
        if(dxc_dll AND EXISTS "${dxc_dll}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${dxc_dll}"
                        "$<TARGET_FILE_DIR:${target_name}>"
                COMMENT "Deploy ${dxc_dll} for ${target_name}")
        endif()
    endforeach()

    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
                 HINTS "${Qt6_DIR}/../../../bin" "${Qt5_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-system-dxc-compiler "$<TARGET_FILE:${target_name}>"
            COMMENT "Running windeployqt for ${target_name}")
    endif()
endfunction()
