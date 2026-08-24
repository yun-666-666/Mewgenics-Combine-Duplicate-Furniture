cmake_minimum_required(VERSION 3.25)

project(CombineDuplicateFurniture VERSION 0.5.1 LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "CombineDuplicateFurniture supports Windows x64 only.")
endif()

option(CDF_BUILD_TESTS "Build focused unit tests" ON)

add_library(cdf_core STATIC
    src/catalog.cpp
    src/domain.cpp
)
target_include_directories(cdf_core PUBLIC include)
target_compile_features(cdf_core PUBLIC cxx_std_20)
target_compile_definitions(cdf_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

add_library(CombineDuplicateFurniture SHARED
    src/mod_main.cpp
    src/native_api.c
    src/native_runtime.cpp
)
target_include_directories(CombineDuplicateFurniture PRIVATE include src)
target_link_libraries(CombineDuplicateFurniture PRIVATE cdf_core user32 kernel32)
target_compile_features(CombineDuplicateFurniture PRIVATE cxx_std_20)
target_compile_definitions(CombineDuplicateFurniture PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN CDF_VERSION="${PROJECT_VERSION}")
set_target_properties(CombineDuplicateFurniture PROPERTIES
    OUTPUT_NAME "CombineDuplicateFurniture"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
)

if(MSVC)
    target_compile_options(cdf_core PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(CombineDuplicateFurniture PRIVATE /W4 /utf-8 /permissive-)
endif()

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(CombineDuplicateFurniture PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/out/${config}"
    )
endforeach()

if(CDF_BUILD_TESTS)
    enable_testing()
    add_executable(cdf_tests tests/cdf_tests.cpp)
    target_link_libraries(cdf_tests PRIVATE cdf_core)
    target_compile_features(cdf_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(cdf_tests PRIVATE /W4 /utf-8 /permissive-)
    endif()
    add_test(NAME cdf_focused_tests COMMAND cdf_tests)

    add_executable(cdf_catalog_probe tests/catalog_probe.cpp)
    target_link_libraries(cdf_catalog_probe PRIVATE cdf_core)
    target_compile_features(cdf_catalog_probe PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(cdf_catalog_probe PRIVATE /W4 /utf-8 /permissive-)
    endif()
endif()
