find_program(CTAGS ctags)

if(CTAGS)
    # Name of the build directory (for exclusion)
    get_filename_component(_build_dir_name "${CMAKE_CURRENT_BINARY_DIR}" NAME)

    add_custom_target(libfyaml-ctags
        COMMAND ${CTAGS}
            -R
            --exclude=${_build_dir_name}
            --exclude=.git
            --exclude=.cache
            --extra=+q
            --c-kinds=+lpx
            --fields=afikmsSzt
            -f "${CMAKE_CURRENT_BINARY_DIR}/tags"
            "${CMAKE_CURRENT_SOURCE_DIR}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )

    if(PROJECT_IS_TOP_LEVEL AND NOT TARGET ctags)
        add_custom_target(ctags DEPENDS libfyaml-ctags)
    endif()
endif()
