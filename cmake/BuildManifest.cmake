# pe-bear-build.json: what this build is, written at configure time.
#
# The same facts the compiled BuildProfile reports -- platform, architecture,
# Qt major, runtime tag, package type -- plus the version, the commit and the
# repository releases come from. It travels inside every published package,
# so a package can be identified without running it, and tst_buildmanifest
# checks that this file and the compiled profile never disagree.
#
# Deliberately computed here from the same inputs the compile definitions get
# (PEBEAR_PACKAGE_TYPE, PEBEAR_BUILD_RUNTIME, PEBEAR_FORK_PATCH, COMMIT_HASH)
# rather than by running the built program: a manifest that needed the
# program to run would be unavailable exactly when it is most wanted, on a
# package that does not start.

if(NOT PEBEAR_UPDATER_ACTIVE)
	return()
endif()

# ---- version, from the one place it lives
file(STRINGS "${CMAKE_SOURCE_DIR}/pe-bear/rebear_ver_short.h" _ver_lines REGEX "#define REBEAR_(MAJOR|MINOR|MICRO|PATCH)_VERSION")
foreach(_line ${_ver_lines})
	if(_line MATCHES "REBEAR_MAJOR_VERSION[ \t]+([0-9]+)")
		set(_v_major "${CMAKE_MATCH_1}")
	elseif(_line MATCHES "REBEAR_MINOR_VERSION[ \t]+([0-9]+)")
		set(_v_minor "${CMAKE_MATCH_1}")
	elseif(_line MATCHES "REBEAR_MICRO_VERSION[ \t]+([0-9]+)")
		set(_v_micro "${CMAKE_MATCH_1}")
	elseif(_line MATCHES "REBEAR_PATCH_VERSION[ \t]+([0-9]+)")
		set(_v_patch "${CMAKE_MATCH_1}")
	endif()
endforeach()
if(NOT DEFINED _v_major OR NOT DEFINED _v_minor OR NOT DEFINED _v_micro)
	message(FATAL_ERROR "BuildManifest: could not read the version from rebear_ver_short.h")
endif()
set(_base_version "${_v_major}.${_v_minor}.${_v_micro}")
if(_v_patch AND NOT _v_patch EQUAL 0)
	set(_base_version "${_base_version}.${_v_patch}")
endif()
# Rendered the way Version::toString() renders it: three digits after "-p".
set(_full_version "${_base_version}")
set(_fork_patch 0)
if(PEBEAR_FORK_PATCH AND PEBEAR_FORK_PATCH GREATER 0)
	set(_fork_patch "${PEBEAR_FORK_PATCH}")
	set(_fp "${PEBEAR_FORK_PATCH}")
	string(LENGTH "${_fp}" _fp_len)
	while(_fp_len LESS 3)
		set(_fp "0${_fp}")
		string(LENGTH "${_fp}" _fp_len)
	endwhile()
	set(_full_version "${_base_version}-p${_fp}")
endif()

# ---- platform and architecture, mirroring BuildProfile.cpp
if(WIN32)
	set(_platform "windows")
elseif(APPLE)
	set(_platform "macos")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(_platform "linux")
else()
	set(_platform "unknown")
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _proc)
if(_proc MATCHES "^(aarch64|arm64)$")
	set(_arch "arm64")
elseif(_proc MATCHES "^(x86_64|amd64)$")
	set(_arch "x64")
elseif(_proc MATCHES "^(i[3-6]86|x86)$")
	set(_arch "x86")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_arch "x64")
else()
	set(_arch "x86")
endif()
# On Windows the generator platform, not the host, decides.
if(WIN32 AND CMAKE_GENERATOR_PLATFORM)
	string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" _gp)
	if(_gp STREQUAL "arm64")
		set(_arch "arm64")
	elseif(_gp STREQUAL "win32")
		set(_arch "x86")
	elseif(_gp STREQUAL "x64")
		set(_arch "x64")
	endif()
endif()

# ---- runtime tag, the same thresholds as detectRuntime()
if(PEBEAR_BUILD_RUNTIME)
	set(_runtime "${PEBEAR_BUILD_RUNTIME}")
elseif(MSVC)
	if(MSVC_VERSION GREATER_EQUAL 1930)
		set(_runtime "vs22")
	elseif(MSVC_VERSION GREATER_EQUAL 1920)
		set(_runtime "vs17")
	elseif(MSVC_VERSION GREATER_EQUAL 1910)
		set(_runtime "vs15")
	elseif(MSVC_VERSION GREATER_EQUAL 1600)
		set(_runtime "vs10")
	else()
		set(_runtime "")
	endif()
else()
	set(_runtime "")
endif()

# ---- Qt
set(_qt_version "${Qt${QT_VERSION_MAJOR}_VERSION}")
if(NOT _qt_version)
	set(_qt_version "${QT_VERSION}")
endif()

set(_package_type "${PEBEAR_PACKAGE_TYPE}")
set(_min_os "${PEBEAR_MIN_OS_VERSION}")
set(_repository "${PEBEAR_UPDATE_REPOSITORY}")
if(NOT _repository)
	set(_repository "hasherezade/pe-bear")
endif()
set(_helper "false")
if(PEBEAR_LIBARCHIVE_FOUND)
	set(_helper "true")
endif()

set(PEBEAR_BUILD_MANIFEST "${CMAKE_BINARY_DIR}/pe-bear-build.json" CACHE INTERNAL "Path of the generated build manifest")
file(WRITE "${PEBEAR_BUILD_MANIFEST}"
"{
  \"manifestVersion\": 1,
  \"name\": \"PE-bear\",
  \"version\": \"${_full_version}\",
  \"baseVersion\": \"${_base_version}\",
  \"forkPatch\": ${_fork_patch},
  \"commit\": \"${PEBEAR_COMMIT}\",
  \"platform\": \"${_platform}\",
  \"arch\": \"${_arch}\",
  \"qtMajor\": ${QT_VERSION_MAJOR},
  \"qtVersion\": \"${_qt_version}\",
  \"runtime\": \"${_runtime}\",
  \"packageType\": \"${_package_type}\",
  \"minOsVersion\": \"${_min_os}\",
  \"updateRepository\": \"${_repository}\",
  \"updaterHelper\": ${_helper}
}
")
message(STATUS "manifest: ${_full_version} ${_platform}/${_arch}/qt${QT_VERSION_MAJOR}${_runtime} package='${_package_type}' -> ${PEBEAR_BUILD_MANIFEST}")
