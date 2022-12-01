Modified from LiTL: Library for Transparent Lock interposition

LiTL is a library that allows executing a program based on Pthread mutex locks with another locking algorithm.

Author : Hugo Guiroux <hugo.guiroux at gmail dot com>
Related Publication: Multicore Locks: the Case is Not Closed Yet, Hugo Guiroux, Renaud Lachaize, Vivien Quéma, USENIX ATC'16.

LiTL Repo: https://github.com/multicore-locks/litl

---

## CMake Migration

### Intro

The LiTL project was originally a GNU Make Project, 
since there hasn't been a commit to the source repository on GitHub since Sep 2018, I port it to a CMake Project but unfortunately it's only available on Linux.

### Changes

+ Configuration File
    + Makefile.config -> Config.cmake
    
    + Append your strategy with same syntax to the list `ALGORITHMS` in `Config.cmake`.


+ Build directory
    + The previous build system generate `binary` and `library` directory and all script files in the project directory which seems not very elegant?

    + Now in our CMake project, you can specify the output directory of them by modifying `TARGETS_DIR` in `Config.cmake`, if you specify it as `${CMAKE_CURRENT_SOURCE_DIR}`, it works exactly the same as the previous build system.


+ Generated Headers
    + `topology.h` is a generated header, the previous build system generate it after run `make` in project include directory, and it won't be removed even after run `make clean`.

    + Now in our CMake project, it will be generate after CMake Configure in a sub-directory in `CMAKE_BINARY_DIR` and the source files can include it right away. 
    
    + Additionlally, it does not affect the project source directory structure.

+ Linker Options
    + Use `target_link_libraries` to link `pthread` and `libpapi` instead of passing `-l` arguments to the linker.

### Commands

+ Configure

    ```sh
    cmake --no-warn-unused-cli -S. -Bbuild -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE
    ````

+ Build all targets

    ```sh
    cmake --build <build dir> --target all
    ````

+ Clean

    ```sh
    cmake --build <build dir> --target clean
    ````
    + This command only removes all target files and generated script files.


+ Purge

    ```sh
    cmake --build <build dir> --target distclean
    ````
    + This command removes all files or directories generated after CMake Configure, including the `binary` and `library` directory.

### Macros and Functions

+ split_by_first_underline
    + The macro simply split a string to two parts by the **first** underline as delimeter.

    + Example
        ```cmake
        split_by_first_underline(
            mcs_spin_then_park
            _name
            _strategy
        )
        ````

    + Result
        ```sh
        _name: mcs
        _strategy: spin_then_park
        ````

+ add_bench
    + Example
        ```cmake
        add_bench(xxx_bench
            SOURCES bench/xxx.c
            DEFINITIONS LIBXXX_INTERFACE
            LIBRARIES xxx_original
        )
        ````
    + Works the same as defining a target in Makefile as follows
        ```make
        $(BINDIR)/xxx_bench: bench/xxx.c $(DIR) $(SOS)
        gcc bench/xxx.c -lpapi -pthread -O3 -Iinclude/ -DLIBXXX_INTERFACE   -L./lib -lxxx_original -g -o $(BINDIR)/xxx_bench
        ````

    + Common compile options and linker options are set in this function, only source files and extra libraries should be specified.

    + If there are extra options, add options after `LINK_OPTIONS` or `COMPILE_OPTIONS`, or simply use `target_link_options` and `target_compile_options` after call this function.