# Warnings and exploit mitigations for PE-bear's own code.
#
# Included from the root CMakeLists after add_subdirectory(capstone) and
# before the other subdirectories: directory-level compile options and the
# CMAKE_*_LINKER_FLAGS variables are copied into a subdirectory when it is
# added, so everything added after this -- bearparser, libdisasm,
# sig_finder, PE-bear, the updater, the tests -- gets them, and capstone,
# third-party code with its own warning policy, does not.
#
# PE-bear parses untrusted files. None of this is a substitute for correct
# bounds checks; it makes a missed one harder to turn into code execution.

if(NOT PEBEAR_HARDENING)
	return()
endif()

# "Optimised" = the configurations that pass -O: _FORTIFY_SOURCE does
# nothing without optimisation, and glibc warns about it.
set(_pebear_optimised "$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>")

if(MSVC)
	# Control Flow Guard needs both halves: /guard:cf on the compiler marks
	# the call targets, on the linker it emits the table. /DYNAMICBASE and
	# /HIGHENTROPYVA (ASLR, 64-bit address space) are link.exe's defaults
	# already; stated so that a toolchain or a flag elsewhere cannot quietly
	# turn them off.
	add_compile_options(/guard:cf)
	set(_pebear_link "/guard:cf /DYNAMICBASE")
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(_pebear_link "${_pebear_link} /HIGHENTROPYVA")
	endif()
	# CET shadow stack compatibility: link.exe knows /CETCOMPAT from Visual
	# Studio 2019 16.7 (MSVC 19.27), and only for x86 and x64 images.
	if(NOT MSVC_VERSION LESS 1927 AND MSVC_CXX_ARCHITECTURE_ID MATCHES "^(x64|X86)$")
		set(_pebear_link "${_pebear_link} /CETCOMPAT")
	endif()
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${_pebear_link}")
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_pebear_link}")

elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	# Warnings are reported, not fatal: the code base has not been built
	# with them before, and a new compiler's new warning must not break a
	# release build.
	add_compile_options(-Wall -Wextra)

	# Not on MinGW: there the stack protector needs libssp at link time,
	# and _FORTIFY_SOURCE depends on the CRT in use.
	if(NOT WIN32)
		add_compile_options(-fstack-protector-strong)
	endif()

	if(NOT APPLE AND NOT WIN32)
		# Apple's SDK sets its own _FORTIFY_SOURCE level; on glibc undefine
		# first, as distribution compilers may predefine a level and
		# a second definition is a warning in every file. Level 3 needs
		# __builtin_dynamic_object_size: GCC 12, Clang 9.
		set(_pebear_fortify 2)
		if((CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12)
				OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9))
			set(_pebear_fortify 3)
		endif()
		add_compile_options("$<${_pebear_optimised}:-U_FORTIFY_SOURCE>" "$<${_pebear_optimised}:-D_FORTIFY_SOURCE=${_pebear_fortify}>")

		# Full RELRO: the GOT is read-only once the loader has resolved it.
		# ELF only; Apple's ld64 has no -z and Mach-O has no equivalent flag.
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro,-z,now")
		set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,relro,-z,now")
	endif()
endif()

# PIE is not set here but in the root CMakeLists, before capstone: a
# position-independent executable may only link position-independent
# objects, so it is the one setting that has to reach capstone as well.
