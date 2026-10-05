# pebear_update_core: the platform-independent half of the updater.
#
# Deliberately links only Qt Core and Qt Network. Nothing here may depend on
# QtWidgets -- the GUI layer lives in updater/gui and is compiled into the
# PE-bear target instead. tests/tst_no_widgets_dependency enforces that.

set(CMAKE_AUTOMOC ON)

set (update_core_srcs
	Version.cpp
	BuildProfile.cpp
	UpdateTypes.cpp
	AssetSelector.cpp
	InstallationDetector.cpp
	ReleaseClient.cpp
	UpdatePaths.cpp
	DownloadManager.cpp
	PackageVerifier.cpp
	UpdateSettings.cpp
	UpdateManager.cpp
	FileSystem.cpp
	TransactionTypes.cpp
	TransactionJournal.cpp
	Transaction.cpp
	ArchiveTypes.cpp
	ExtractionPolicy.cpp
	PackageExtractor.cpp
	LibArchiveReader.cpp
	Installer.cpp
	DirectoryInstaller.cpp
	StartupHandshake.cpp
	HelperHandoff.cpp
	ProcessControl.cpp
	UpdateHelper.cpp
)

set (update_core_hdrs
	Version.h
	BuildProfile.h
	UpdateTypes.h
	AssetSelector.h
	InstallationDetector.h
	ReleaseClient.h
	UpdatePaths.h
	DownloadManager.h
	PackageVerifier.h
	UpdateSettings.h
	UpdateManager.h
	FileSystem.h
	TransactionTypes.h
	TransactionJournal.h
	Transaction.h
	ArchiveTypes.h
	ExtractionPolicy.h
	PackageExtractor.h
	LibArchiveReader.h
	Installer.h
	PlatformInstaller.h
	DirectoryInstaller.h
	StartupHandshake.h
	HelperHandoff.h
	ProcessControl.h
	UpdateHelper.h
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

# libarchive decodes the published packages: ZIP for Windows and the macOS
# bundle, TAR.XZ for Linux. Optional on purpose -- a build without it still
# compiles and simply cannot install, which is the same notify-only
# degradation as a build that cannot state its own package type. That keeps a
# plain `cmake ..` from suddenly requiring a new system dependency.
#
# Found via PkgConfig where available (Linux, macOS via Homebrew) and via the
# CMake package otherwise (vcpkg on Windows exports LibArchive::LibArchive).
option(PEBEAR_WITH_LIBARCHIVE "Build the libarchive-backed archive reader" ON)
if(PEBEAR_WITH_LIBARCHIVE)
	set(PEBEAR_LIBARCHIVE_FOUND OFF)

	find_package(LibArchive QUIET)
	if(LibArchive_FOUND)
		target_link_libraries(pebear_update_core PUBLIC ${LibArchive_LIBRARIES})
		target_include_directories(pebear_update_core PUBLIC ${LibArchive_INCLUDE_DIRS})
		set(PEBEAR_LIBARCHIVE_FOUND ON)
		message(STATUS "libarchive: ${LibArchive_VERSION} (CMake package)")
	else()
		find_package(PkgConfig QUIET)
		if(PkgConfig_FOUND)
			pkg_check_modules(LIBARCHIVE QUIET libarchive)
			if(LIBARCHIVE_FOUND)
				target_link_libraries(pebear_update_core PUBLIC ${LIBARCHIVE_LIBRARIES})
				target_include_directories(pebear_update_core PUBLIC ${LIBARCHIVE_INCLUDE_DIRS})
				target_link_directories(pebear_update_core PUBLIC ${LIBARCHIVE_LIBRARY_DIRS})
				set(PEBEAR_LIBARCHIVE_FOUND ON)
				message(STATUS "libarchive: ${LIBARCHIVE_VERSION} (pkg-config)")
			endif()
		endif()
	endif()

	if(PEBEAR_LIBARCHIVE_FOUND)
		target_compile_definitions(pebear_update_core PUBLIC PEBEAR_WITH_LIBARCHIVE=1)
	else()
		# Stated rather than silent: without it the updater can check and
		# download but not install, and that should not be a surprise found
		# later at runtime.
		message(WARNING
			"libarchive not found -- the updater will be able to check for and "
			"download updates, but not install them. Install libarchive-dev "
			"(Linux), `brew install libarchive` (macOS) or "
			"`vcpkg install libarchive` (Windows), or set "
			"PEBEAR_WITH_LIBARCHIVE=OFF to silence this.")
	endif()
endif()

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

# pe-bear-updater: the process that replaces the installation.
#
# A separate executable because on Windows a running program's image cannot be
# renamed or deleted -- PE-bear cannot be the thing that replaces PE-bear. No
# Widgets: it must run without a display and must never be able to put a dialog
# in front of the user. Qt Network arrives with pebear_update_core rather than
# being asked for here; the helper does no networking of its own.
#
# Built only when a decoder is available. Without libarchive it could start,
# re-verify the package and then refuse at the point of unpacking it, which is
# a worse thing to ship than not shipping it: a build that cannot install
# updates should not place an installer next to the application.
if(PEBEAR_LIBARCHIVE_FOUND)
	if(MSVC)
		# The application is a GUI subsystem binary; this is not. Inherited
		# from the parent directory, so it has to be taken back off here, or
		# the helper gets /ENTRY:mainCRTStartup and no console to report to.
		string(REPLACE "/SUBSYSTEM:WINDOWS" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
		string(REPLACE "/ENTRY:mainCRTStartup" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
	endif()

	add_executable(pe-bear-updater helper/main.cpp)
	target_link_libraries(pe-bear-updater PRIVATE pebear_update_core)

	install(TARGETS pe-bear-updater
		RUNTIME DESTINATION bin
		BUNDLE DESTINATION .
		COMPONENT pe-bear-updater
	)
endif()

if(PEBEAR_BUILD_UPDATER_TESTS)
	add_subdirectory(tests)
endif()
