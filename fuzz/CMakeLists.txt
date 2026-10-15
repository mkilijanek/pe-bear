# libFuzzer targets, built only with -DPEBEAR_BUILD_FUZZERS=ON and Clang
# (the root CMakeLists checks both and instruments the libraries).
#
#   cmake -S . -B build-fuzz -G Ninja -DUSE_QT5=ON -DPEBEAR_BUILD_FUZZERS=ON \
#         -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
#   cmake --build build-fuzz --target fuzz_pefile fuzz_make_seeds
#   build-fuzz/fuzz/fuzz_make_seeds corpus
#   build-fuzz/fuzz/fuzz_pefile corpus -max_total_time=60
#
# fuzz_pefile:     bearparser loading a PE from a buffer and every wrapper read
# fuzz_make_seeds: writes the seed corpus from the unit tests' MinimalPe fixture

cmake_minimum_required (VERSION 3.12)
project (pebear_fuzz)

if(USE_QT4)
	message(FATAL_ERROR "PEBEAR_BUILD_FUZZERS needs Qt5 or Qt6")
endif()
if(USE_QT5)
	find_package(QT NAMES Qt5 COMPONENTS Core REQUIRED)
else()
	find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
endif()
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core REQUIRED)

add_executable(fuzz_pefile fuzz_pefile.cpp)
target_include_directories(fuzz_pefile PRIVATE ${PARSER_INC})
target_link_libraries(fuzz_pefile bearparser Qt${QT_VERSION_MAJOR}::Core)
# libFuzzer's own main(); the libraries only carry its coverage hooks
set_property(TARGET fuzz_pefile APPEND_STRING PROPERTY LINK_FLAGS " -fsanitize=fuzzer")
add_dependencies(fuzz_pefile bearparser)

set(PB_TESTS "${CMAKE_SOURCE_DIR}/pe-bear/tests")
add_executable(fuzz_make_seeds make_seeds.cpp ${PB_TESTS}/MinimalPe.cpp ${PB_TESTS}/MinimalPe.h)
target_include_directories(fuzz_make_seeds PRIVATE ${PB_TESTS} ${PARSER_INC})
target_link_libraries(fuzz_make_seeds bearparser Qt${QT_VERSION_MAJOR}::Core)
add_dependencies(fuzz_make_seeds bearparser)
