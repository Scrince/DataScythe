# Bundle MinGW runtime DLLs next to a Windows executable target.
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

    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
                 HINTS "${Qt6_DIR}/../../../bin" "${Qt5_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:${target_name}>"
            COMMENT "Running windeployqt for ${target_name}")
    endif()
endfunction()