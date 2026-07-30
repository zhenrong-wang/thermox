cmake_minimum_required(VERSION 3.20)

project(thermox_native_extension_conformance LANGUAGES CXX)

find_package(Thermox CONFIG REQUIRED)

add_executable(
    thermox_native_extension_conformance
    main.cpp
)
target_link_libraries(
    thermox_native_extension_conformance
    PRIVATE thermox::service_native
)
target_compile_features(
    thermox_native_extension_conformance PRIVATE cxx_std_20)
