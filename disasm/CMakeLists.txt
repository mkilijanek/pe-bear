cmake_minimum_required (VERSION 3.12)
project (libdisasm)

# Offer the user the choice of overriding the installation directories
set(INSTALL_LIB_DIR lib CACHE PATH "Installation directory for libraries")
set(INSTALL_BIN_DIR bin CACHE PATH "Installation directory for executables")

if(USE_QT4)
	find_package (Qt4 REQUIRED)
	include_directories( ${QT_INCLUDE_DIR} ${QT_QTCORE_INCLUDE_DIR} )
	INCLUDE( ${QT_USE_FILE} )
	ADD_DEFINITIONS( ${QT_DEFINITIONS} )
else()
	find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
	find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core REQUIRED)
	get_target_property(QtCore_location Qt${QT_VERSION_MAJOR}::Core LOCATION)
endif()

include_directories ( ${CAPSTONE_INC} )
include_directories( ${PARSER_INC} )

set (disasm_wrappers_srcs
	cdis/CDisasm.cpp
)
set (disasm_wrappers_hdrs
	cdis/CDisasm.h
)
message(STATUS "capstone_includes='${CAPSTONE_INC}'")
message(STATUS "capstone_lib='${CAPSTONE_LIB}'")

# multi-processor compilation
if(MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()

set (disasm_srcs
	Disasm.cpp
	PeDisasm.cpp
	${disasm_wrappers_srcs}
)

set (disasm_hdrs
	bear_disasm.h
	Disasm.h
	PeDisasm.h
	MnemType.h
	${disasm_wrappers_hdrs}
)

add_library( ${PROJECT_NAME} STATIC ${disasm_srcs} ${disasm_hdrs} )

target_link_libraries( ${PROJECT_NAME} ${PARSER_LIB} capstone_static )

if(USE_QT4)
	target_link_libraries ( ${PROJECT_NAME} ${QT_QTCORE_LIBRARIES} )
else()
	target_link_libraries( ${PROJECT_NAME} Qt${QT_VERSION_MAJOR}::Core)
endif()

