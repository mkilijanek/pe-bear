# Unit tests and model checks for PE-bear's own pre-existing code.
#
# The application is built as a single executable, so there is no library for a
# test to link against. Rather than restructure the production build, each test
# target compiles the sources it needs, reusing the source lists already
# defined in the parent CMakeLists (CMake variables reach subdirectories).
#
# Guarded by PEBEAR_BUILD_TESTS so the default build is untouched.

set(CMAKE_AUTOMOC ON)

set(PB "${CMAKE_CURRENT_SOURCE_DIR}/..")

# --- the fixture every PE-dependent suite uses ---
set(fixture_srcs
	${CMAKE_CURRENT_SOURCE_DIR}/MinimalPe.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/MinimalPe.h
)

# --- dependency groups, smallest first ---

# pure logic, needs nothing but Qt Core
set(deps_bearvers
	${PB}/base/BearVers.cpp
)

# operates on a bearparser buffer; no GUI, no PE file
set(deps_modification
	${PB}/base/Modification.cpp
)

set(deps_commenthandler
	${PB}/base/CommentHandler.cpp
)

# header-only Q_OBJECT: AUTOMOC only processes headers listed as target sources
set(deps_stringscollection
	${PB}/base/StringsCollection.h
)

# the generic tree model: QtWidgets, but no PE
set(deps_treemodel
	${PB}/gui/TreeModel.cpp
)

# the concrete PE views pull in the whole non-window half of the application
set(deps_pemodels
	${PB}/base/PeHandler.cpp
	${PB}/base/PeHandlerFactory.cpp
	${PB}/base/PeHandlersManager.cpp
	${PB}/base/Modification.cpp
	${PB}/base/CommentHandler.cpp
	${PB}/base/MainSettings.cpp
	${PB}/base/RegKeyManager.cpp
	${PB}/base/threads/CalcThread.cpp
	${PB}/base/threads/SignFinderThread.cpp
	${PB}/base/threads/StringExtThread.cpp
	${PB}/gui/TreeModel.cpp
	${PB}/gui/PeTreeModel.cpp
	${PB}/gui/PeWrapperModel.cpp
	${PB}/gui/DosHdrTableModel.cpp
	${PB}/gui/PackersTableModel.cpp
	${PB}/gui_base/PeGuiItem.cpp
	${PB}/gui_base/PeTableModel.cpp
	${PB}/gui_base/WrapperTableModel.cpp
	${PB}/gui_base/WrapperInterface.cpp
	${PB}/ViewSettings.cpp
)

# The parent's lists are relative to pe-bear/; make them absolute so they
# resolve from this subdirectory.
foreach(rel ${pebear_gui_pe_mod_srcs})
	list(APPEND deps_pemodels "${PB}/${rel}")
endforeach()

# Four classes reachable from here are header-only Q_OBJECTs, so AUTOMOC needs
# their headers as target sources to emit a meta-object at all -- the same
# reason the application passes ${pebear_hdrs} through qt_wrap_cpp. Listed
# individually rather than adding whole header groups: mocking a widget header
# whose .cpp is not compiled here only produces undefined references.
list(APPEND deps_pemodels
	${PB}/base/StringsCollection.h
	${PB}/base/threads/CollectorThread.h
	${PB}/gui/followable_table/FollowableOffsetedView.h
	${PB}/gui/followable_table/MouseTrackingTableView.h
	${PB}/gui_base/FollowablePeTreeView.cpp
	${PB}/gui_base/PeTreeView.cpp
	${PB}/gui_base/ExtTableView.cpp
	${PB}/gui_base/TreeCpView.cpp
	${PB}/gui_base/ClipboardUtil.cpp
	${PB}/gui/followable_table/OffsetedView.cpp
	${PB}/ExeDependentAction.cpp
	${PB}/DateDisplay.cpp
)

# --- test targets: name -> extra sources ---
set(pebear_tests
	tst_minimalpe
	tst_bearvers
	tst_modification
	tst_commenthandler
	tst_stringscollection
	tst_treemodel
	tst_pemodels
)

foreach(test_name ${pebear_tests})
	if(test_name STREQUAL "tst_bearvers")
		set(test_deps ${deps_bearvers})
	elseif(test_name STREQUAL "tst_modification")
		set(test_deps ${deps_modification})
	elseif(test_name STREQUAL "tst_commenthandler")
		set(test_deps ${deps_commenthandler})
	elseif(test_name STREQUAL "tst_stringscollection")
		set(test_deps ${deps_stringscollection})
	elseif(test_name STREQUAL "tst_treemodel")
		set(test_deps ${deps_treemodel})
	elseif(test_name STREQUAL "tst_pemodels")
		set(test_deps ${deps_pemodels})
	else()
		set(test_deps "")
	endif()

	add_executable(${test_name} ${test_name}.cpp ${fixture_srcs} ${test_deps})

	target_include_directories(${test_name} PRIVATE
		${PB}
		${PARSER_INC}
		${SIGFIND_INC}
		${DISASM_DIR}
		${CAPSTONE_INC}
	)
	target_link_libraries(${test_name}
		${SIGFIND_LIB}
		${PARSER_LIB}
		${DISASM_LIB}
		capstone_static
		Qt${QT_VERSION_MAJOR}::Core
		Qt${QT_VERSION_MAJOR}::Widgets
		Qt${QT_VERSION_MAJOR}::Test
	)
	add_dependencies(${test_name} bearparser sig_finder libdisasm capstone_static)
	add_test(NAME ${test_name} COMMAND ${test_name})
	# the model checks instantiate widgets-adjacent classes; keep them headless
	set_tests_properties(${test_name} PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	)
endforeach()
