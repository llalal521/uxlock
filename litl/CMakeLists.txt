cmake_minimum_required(VERSION 3.16)

project(litl LANGUAGES C)

# -- Read configuration
include(Config.cmake)

# Set default targets dir
if(NOT DEFINED TARGETS_DIR)
    set(TARGETS_DIR ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Set basic directories
set(_build_env_dir ${TARGETS_DIR})
set(_config_inc_dir ${CMAKE_BINARY_DIR}/_tmp_inc)
set(_config_scripts_dir ${CMAKE_BINARY_DIR}/_tmp_scripts)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${_build_env_dir}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${_build_env_dir}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${_build_env_dir}/lib)

# -- Check requried libraries
find_library(LIB_PAPI papi)
if(NOT LIB_PAPI)
    message(FATAL_ERROR "libpapi not found!!!")
endif()
message(STATUS "Find libpapi at ${LIB_PAPI}")

# Remove previous temp dirs
function(clean_dir _dir)
    if(EXISTS ${_dir})
        file(REMOVE_RECURSE ${_dir})
    endif()
    make_directory(${_dir})
endfunction()

clean_dir(${_config_inc_dir})
clean_dir(${_config_scripts_dir})

# -- Setup globe
include_directories(include ${_config_inc_dir})

# Add target "distclean" to remove all output dirs
add_custom_target(
    distclean #
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean # Run clean first
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build_env_dir}/bin #
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build_env_dir}/lib #
)

# -- Build SOS SHS
add_subdirectory(src)

# -- Configure topology.h
if(TRUE)
    # nodes
    execute_process(
        COMMAND bash -c "numactl -H | head -1 | cut -f 2 -d' '" TIMEOUT 5 OUTPUT_VARIABLE nodes
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    # cpus
    execute_process(COMMAND bash -c "nproc" TIMEOUT 5 OUTPUT_VARIABLE cpus OUTPUT_STRIP_TRAILING_WHITESPACE)

    # cachelinesize
    set(cachelinesize 128)

    # pagesize
    execute_process(
        COMMAND bash -c "getconf PAGESIZE" TIMEOUT 5 OUTPUT_VARIABLE pagesize OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    # cpufreq
    execute_process(
        COMMAND bash -c "cat /proc/cpuinfo | grep MHz | head -1 | awk '{ x = \$4/1000; printf(\"%0.2g\", x); }'"
        TIMEOUT 5 OUTPUT_VARIABLE cpufreq OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    configure_file(include/topology.in ${_config_inc_dir}/topology.h @ONLY)
endif()

# -- Add micro benchmarks
set(_link_options -Wl,--no-as-needed -ldl)
set(_compile_options -O3 -g)

function(add_bench _name)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs SOURCES DEFINITIONS LIBRARIES LINK_OPTIONS COMPILE_OPTIONS)
    cmake_parse_arguments(FUNC "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${_name} ${FUNC_SOURCES})
    target_link_libraries(${_name} PRIVATE papi pthread ${FUNC_LIBRARIES})
    target_link_options(${_name} PRIVATE ${_link_options} ${FUNC_LINK_OPTIONS})
    target_compile_definitions(${_name} PRIVATE ${FUNC_DEFINITIONS})
    target_compile_options(${_name} PRIVATE ${_compile_options} ${FUNC_COMPILE_OPTIONS})
endfunction()

add_bench(bench SOURCES bench/bench.c)

add_bench(bench_block SOURCES bench/bench_block.c)

add_bench(nest_bench SOURCES bench/nest_bench.c)

add_bench(check SOURCES bench/check.c)

add_bench(rwbench SOURCES bench/rwbench.c)

# add_bench(ux_cs_bench_uta SOURCES bench/ux_cs_bench.c DEFINITIONS LIBUTA_INTERFACE LIBRARIES uta_original)

add_bench(asl_bench_block SOURCES bench/bench_block.c DEFINITIONS LIBASLBLOCK_INTERFACE
          LIBRARIES aslblock_spin_then_park
)

add_bench(uxactive_bench SOURCES bench/bench.c DEFINITIONS LIBUXACTIVE_INTERFACE LIBRARIES uxactive_original)

add_bench(uta_bench SOURCES bench/bench.c DEFINITIONS LIBUTA_INTERFACE LIBRARIES uta_original)

add_bench(utafts_bench SOURCES bench/bench.c DEFINITIONS LIBUTA_INTERFACE LIBRARIES utafts_original)

add_bench(utascl_bench SOURCES bench/bench.c DEFINITIONS LIBUTA_INTERFACE LIBRARIES utascl_original)

add_bench(utaspc_bench SOURCES bench/bench.c DEFINITIONS LIBUTA_INTERFACE LIBUTASCL_INTERFACE LIBRARIES utaspc_original)

add_bench(nest_bench_uta SOURCES bench/nest_bench.c DEFINITIONS LIBUTA_INTERFACE LIBRARIES uta_original)

add_bench(uta_check SOURCES bench/check.c DEFINITIONS LIBUTA_INTERFACE LIBRARIES uta_original)

add_bench(bind_check SOURCES bench/check.c DEFINITIONS LIBUTABIND_INTERFACE LIBRARIES utabind_original)
