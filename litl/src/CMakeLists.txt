macro(split_by_first_underline _str _first _second)
    string(LENGTH ${_str} _len)
    string(FIND ${_algo} _ _index)
    string(SUBSTRING ${_algo} 0 ${_index} ${_first})

    math(EXPR _index "${_index}+1")
    math(EXPR _len "${_len}-${_index}")
    string(SUBSTRING ${_algo} ${_index} ${_len} ${_second})
endmacro()

# -- Build SOS and SHS
set(COND_VAR 0)

set(_link_options -Wl,--whole-archive -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/interpose.map
                  -Wl,--no-whole-archive -lrt -lm -ldl
)

set(_compile_options -fPIC -O3 -g)

foreach(_algo ${ALGORITHMS})
    # SOS
    split_by_first_underline(${_algo} _name _strategy)

    string(TOUPPER ${_name} _name_upper)
    string(TOUPPER ${_strategy} _strategy_upper)

    # Add target
    add_library(${_algo} SHARED ${_name}.c interpose.c utils.h utils.c)

    target_link_options(${_algo} PRIVATE ${_link_options})
    target_compile_options(${_algo} PRIVATE ${_compile_options})

    target_compile_definitions(${_algo} PUBLIC COND_VAR=${COND_VAR})
    target_compile_definitions(${_algo} PUBLIC FCT_LINK_SUFFIX=${_name})
    target_compile_definitions(${_algo} PUBLIC ${_name_upper})
    target_compile_definitions(${_algo} PUBLIC WAITING_${_strategy_upper})

    # Explicitly link papi and pthread
    target_link_libraries(${_algo} PUBLIC papi pthread)

    # SHS
    set(_script_name "$<TARGET_FILE_PREFIX:${_algo}>$<TARGET_FILE_BASE_NAME:${_algo}>.sh")
    set(_script_in_name _tmp_lib${_algo}.sh.in)

    # Configure script
    set(abs_top_srcdir ${_build_env_dir})
    set(lib $<TARGET_FILE_NAME:${_algo}>)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/liblock.in ${_config_scripts_dir}/${_script_in_name})

    # Generate script
    file(GENERATE OUTPUT ${_config_scripts_dir}/${_script_name} INPUT ${_config_scripts_dir}/${_script_in_name})

    # Copy script after build
    add_custom_command(
        TARGET ${_algo} POST_BUILD #
        COMMAND ${CMAKE_COMMAND} -E copy ${_config_scripts_dir}/${_script_name} ${_build_env_dir}
        COMMAND bash -c "chmod a+x ${_build_env_dir}/${_script_name}"
    )

    # Add script file to "clean" target
    set_property(TARGET ${_algo} APPEND PROPERTY ADDITIONAL_CLEAN_FILES ${_build_env_dir}/${_script_name})

endforeach()
