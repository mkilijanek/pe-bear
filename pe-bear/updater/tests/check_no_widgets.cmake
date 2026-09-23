# Fails if the updater core library references QtWidgets.
#
# Run as a CTest case; LIB_FILE is the static library to inspect.

if(NOT EXISTS "${LIB_FILE}")
	message(FATAL_ERROR "library not found: ${LIB_FILE}")
endif()

find_program(NM_EXECUTABLE NAMES nm llvm-nm)
if(NOT NM_EXECUTABLE)
	message(STATUS "nm not available, skipping the QtWidgets boundary check")
	return()
endif()

execute_process(
	COMMAND ${NM_EXECUTABLE} --undefined-only "${LIB_FILE}"
	OUTPUT_VARIABLE symbols
	ERROR_VARIABLE nm_errors
	RESULT_VARIABLE nm_result
)
if(NOT nm_result EQUAL 0)
	message(STATUS "nm failed (${nm_errors}), skipping the QtWidgets boundary check")
	return()
endif()

# Widget class names as they appear in mangled C++ symbols.
set(forbidden "QWidget" "QApplication" "QDialog" "QMessageBox" "QLayout" "QAbstractButton")
set(found "")
foreach(name ${forbidden})
	if(symbols MATCHES "${name}")
		list(APPEND found ${name})
	endif()
endforeach()

if(found)
	string(REPLACE ";" ", " found_str "${found}")
	message(FATAL_ERROR
		"pebear_update_core depends on QtWidgets (${found_str}). "
		"Widget code belongs in updater/gui, which is built into the application.")
endif()

message(STATUS "pebear_update_core is free of QtWidgets dependencies")
