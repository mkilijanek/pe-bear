# Assembles a runnable PE-bear directory on Windows: the executables, the Qt
# libraries and plugins they need, the C++ runtime, the libarchive DLLs that
# come with the updater, and the translation files the application looks for
# beside itself.
#
# Off unless PEBEAR_DEPLOY_DIR is set, so an ordinary build is unaffected.
#
#   cmake -S . -B build -DPEBEAR_DEPLOY_DIR=C:/test/pe-bear ...
#   cmake --build build          # the directory is filled at the end
#
# Why this exists rather than `cmake --install`: the install rules place files
# under a prefix in the layout a package manager wants. What is needed for
# testing an *update* is the layout the application actually runs from --
# everything beside the executable, nothing resolved from elsewhere on the
# machine -- because that is what the updater replaces. A deployment that
# borrowed a Qt DLL from the system PATH would still start after a bad update
# and prove nothing.
#
# The helper is placed beside the application, which is what the current
# install rules would ship. Note that this is precisely the layout question
# raised as B1 in issue #27: on Windows the running helper's own image then
# sits inside the directory being replaced, and a locked image is expected to
# make the backup removal fail. Keeping the deployment faithful to what would
# ship is what makes this directory the right place to observe that.

if(NOT WIN32)
	return()
endif()

set(PEBEAR_DEPLOY_DIR "" CACHE PATH
	"Assemble a runnable PE-bear directory here at the end of the build (Windows only)")
option(PEBEAR_DEPLOY_CLEAN
	"Empty PEBEAR_DEPLOY_DIR before filling it, so nothing stale survives" OFF)
option(PEBEAR_DEPLOY_LEAN
	"Leave out Qt's software OpenGL and Direct3D fallbacks (about 41 MB)" OFF)

if(NOT PEBEAR_DEPLOY_DIR)
	return()
endif()

get_filename_component(_deploy_dir "${PEBEAR_DEPLOY_DIR}" ABSOLUTE)

# Refusing rather than deploying into the source or build tree: a copy of the
# application inside either is confusing at best, and `PEBEAR_DEPLOY_CLEAN`
# would then be pointed at files that matter.
foreach(_forbidden "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
	get_filename_component(_f "${_forbidden}" ABSOLUTE)
	if(_deploy_dir STREQUAL "${_f}")
		message(FATAL_ERROR "PEBEAR_DEPLOY_DIR must not be the source or build directory")
	endif()
endforeach()

# ---------------------------------------------------------------- windeployqt
#
# It is what knows which Qt libraries, plugins and runtime a given executable
# needs; listing them by hand is a list that silently goes stale. Its absence
# is a hard error rather than a warning: a deployment missing the platform
# plugin starts no window, and finding that out by running it would look like
# a defect in the application.
set(_qt_bin_hint "")
if(TARGET Qt${QT_VERSION_MAJOR}::qmake)
	get_target_property(_qmake_path Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
	if(_qmake_path)
		get_filename_component(_qt_bin_hint "${_qmake_path}" DIRECTORY)
	endif()
endif()

find_program(PEBEAR_WINDEPLOYQT
	NAMES windeployqt windeployqt6 windeployqt.exe
	HINTS "${_qt_bin_hint}" "${QT_DIR}/../../../bin" "${Qt${QT_VERSION_MAJOR}_DIR}/../../../bin"
)
if(NOT PEBEAR_WINDEPLOYQT)
	message(FATAL_ERROR
		"PEBEAR_DEPLOY_DIR is set but windeployqt was not found. It ships with Qt; "
		"add its bin directory to PATH or set PEBEAR_WINDEPLOYQT explicitly.")
endif()

# ------------------------------------------------------- the C++ runtime
#
# windeployqt's --compiler-runtime does not copy the runtime: when it cannot
# find the redistributable DLLs it drops vc_redist.x64.exe instead, a 25 MB
# installer that makes the directory bigger without making it self-contained.
# Measured on the test host -- msvcp140.dll and both vcruntime140 DLLs were
# absent and the application started only because the host already had them.
# On a clean machine it would not have.
#
# This module resolves the actual DLLs. _SKIP stops it adding install rules;
# only the variable is wanted.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP ON)
set(CMAKE_INSTALL_UCRT_LIBRARIES ON)
include(InstallRequiredSystemLibraries)

if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
	list(LENGTH CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS _rt_count)
	message(STATUS "deploy: ${_rt_count} C++ runtime DLL(s)")
else()
	message(WARNING
		"deploy: the C++ runtime DLLs could not be located, so the deployment will "
		"rely on them being installed on whatever machine runs it.")
endif()

# ---------------------------------------------------------------- vcpkg DLLs
#
# windeployqt resolves Qt and the compiler runtime, and nothing else. The
# updater's libarchive -- with zlib, bzip2 and liblzma behind it -- arrives
# through vcpkg, so those are collected separately.
#
# Globbed at configure time on purpose: the set changes only when the
# dependency set changes, which is a reconfigure anyway. $<TARGET_RUNTIME_DLLS>
# would be more precise but needs CMake 3.21, and this project builds with 3.12.
set(_runtime_dlls "")
if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
	file(GLOB _runtime_dlls "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/*.dll")
endif()
if(_runtime_dlls)
	list(LENGTH _runtime_dlls _dll_count)
	message(STATUS "deploy: ${_dll_count} vcpkg DLL(s) from ${VCPKG_TARGET_TRIPLET}")
elseif(PEBEAR_LIBARCHIVE_FOUND)
	# Built against libarchive but with no vcpkg tree to take DLLs from. Said
	# plainly, because the helper will not start without them and the symptom
	# is a missing-DLL dialog rather than anything that names libarchive.
	message(WARNING
		"deploy: built with libarchive but VCPKG_INSTALLED_DIR is unset, so its DLLs "
		"will not be copied. pe-bear-updater.exe will not start from the deployment "
		"unless they are on PATH.")
endif()

# ---------------------------------------------------------------- translations
#
# The application looks for these beside itself, in Language/<locale>/, so a
# deployment without them silently falls back to English.
file(GLOB _qm_files "${CMAKE_SOURCE_DIR}/Language/*/PELanguage.qm")

# ---------------------------------------------------------------- the target
set(_deploy_commands
	COMMAND ${CMAKE_COMMAND} -E make_directory "${_deploy_dir}"
)
if(PEBEAR_DEPLOY_CLEAN)
	set(_deploy_commands
		COMMAND ${CMAKE_COMMAND} -E rm -rf "${_deploy_dir}"
		${_deploy_commands}
	)
endif()

list(APPEND _deploy_commands
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:${PROJECT_NAME}>" "${_deploy_dir}/"
)

set(_deploy_depends ${PROJECT_NAME})
if(TARGET pe-bear-updater)
	list(APPEND _deploy_depends pe-bear-updater)
	list(APPEND _deploy_commands
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"$<TARGET_FILE:pe-bear-updater>" "${_deploy_dir}/"
	)
endif()

# windeployqt is run against the application; it brings the platform plugin,
# the styles, the image formats and the compiler runtime with it. Then against
# the helper, which needs only Qt Core and Network but must not be left
# depending on the application's copies being found first.
set(_windeployqt_flags "")
if(PEBEAR_DEPLOY_LEAN)
	# Measured rather than assumed: with these four files removed by hand, the
	# deployed application still started and answered the startup handshake on
	# the Windows test host. They are not the default because opengl32sw.dll is
	# Qt's fallback when the desktop OpenGL path fails -- which happens on some
	# virtual machines and remote sessions -- and a deployment that will not
	# start is a worse outcome than one that is 41 MB larger.
	list(APPEND _windeployqt_flags --no-opengl-sw --no-system-d3d-compiler)
endif()

list(APPEND _deploy_commands
	COMMAND "${PEBEAR_WINDEPLOYQT}" ${_windeployqt_flags}
		--dir "${_deploy_dir}" "${_deploy_dir}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
)
if(TARGET pe-bear-updater)
	list(APPEND _deploy_commands
		COMMAND "${PEBEAR_WINDEPLOYQT}" ${_windeployqt_flags} --dir "${_deploy_dir}"
			"${_deploy_dir}/$<TARGET_FILE_NAME:pe-bear-updater>"
	)
endif()

foreach(_rt ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
	list(APPEND _deploy_commands
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_rt}" "${_deploy_dir}/"
	)
endforeach()

foreach(_dll ${_runtime_dlls})
	list(APPEND _deploy_commands
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "${_deploy_dir}/"
	)
endforeach()

foreach(_qm ${_qm_files})
	get_filename_component(_qm_dir "${_qm}" DIRECTORY)
	get_filename_component(_locale "${_qm_dir}" NAME)
	list(APPEND _deploy_commands
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${_qm}" "${_deploy_dir}/Language/${_locale}/PELanguage.qm"
	)
endforeach()

# An earlier run with --compiler-runtime may have left the installer here.
# Removed rather than ignored: 25 MB of something that does nothing in a
# directory meant to be copied about.
list(APPEND _deploy_commands
	COMMAND ${CMAKE_COMMAND} -E rm -f "${_deploy_dir}/vc_redist.x64.exe"
		"${_deploy_dir}/vc_redist.x86.exe"
)

add_custom_target(pebear_deploy ALL
	${_deploy_commands}
	DEPENDS ${_deploy_depends}
	COMMENT "Deploying PE-bear to ${_deploy_dir}"
	VERBATIM
)
add_dependencies(pebear_deploy ${_deploy_depends})

message(STATUS "deploy: ${_deploy_dir} (windeployqt: ${PEBEAR_WINDEPLOYQT})")
