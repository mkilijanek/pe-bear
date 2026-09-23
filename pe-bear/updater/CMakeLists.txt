# pebear_update_core: the platform-independent half of the updater.
#
# Deliberately links only Qt Core and Qt Network. Nothing here may depend on
# QtWidgets -- the GUI layer lives in updater/gui and is compiled into the
# PE-bear target instead. tests/tst_no_widgets_dependency enforces that.

set(CMAKE_AUTOMOC ON)

set (update_core_srcs
	Version.cpp
)

set (update_core_hdrs
	Version.h
)

add_library(pebear_update_core STATIC ${update_core_srcs} ${update_core_hdrs})

target_include_directories(pebear_update_core PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(pebear_update_core PUBLIC
	Qt${QT_VERSION_MAJOR}::Core
	Qt${QT_VERSION_MAJOR}::Network
)

target_compile_definitions(pebear_update_core PUBLIC PEBEAR_WITH_UPDATER=1)

# Build-time description of this package. Empty values simply stay unknown;
# the asset selector treats an unknown field as "cannot narrow the match".
if(PEBEAR_PACKAGE_TYPE)
	target_compile_definitions(pebear_update_core PUBLIC
		PEBEAR_PACKAGE_TYPE="${PEBEAR_PACKAGE_TYPE}")
endif()
if(PEBEAR_BUILD_RUNTIME)
	target_compile_definitions(pebear_update_core PUBLIC
		PEBEAR_BUILD_RUNTIME="${PEBEAR_BUILD_RUNTIME}")
endif()
if(PEBEAR_MIN_OS_VERSION)
	target_compile_definitions(pebear_update_core PUBLIC
		PEBEAR_MIN_OS_VERSION="${PEBEAR_MIN_OS_VERSION}")
endif()
if(COMMIT_HASH)
	target_compile_definitions(pebear_update_core PUBLIC
		PEBEAR_BUILD_ID="${COMMIT_HASH}")
endif()

SOURCE_GROUP("Source Files\\updater" FILES ${update_core_srcs})
SOURCE_GROUP("Header Files\\updater" FILES ${update_core_hdrs})

if(PEBEAR_BUILD_UPDATER_TESTS)
	add_subdirectory(tests)
endif()
