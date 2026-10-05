# Unit tests for the updater core.
#
# Every case here runs offline and deterministically: the only socket involved
# is a loopback server started by the download test itself.

set(CMAKE_AUTOMOC ON)

set(updater_tests
	tst_version
	tst_buildprofile
	tst_assetselector
	tst_releaseclient
	tst_installationdetector
	tst_updatepaths
	tst_updatesettings
	tst_packageverifier
	tst_downloadmanager
	tst_updatemanager
	tst_liverelease
	tst_transactionjournal
	tst_transaction
	tst_extractionpolicy
	tst_packageextractor
	tst_libarchivereader
)

foreach(test_name ${updater_tests})
	# TestHttpServer.h declares a QObject, so it must be a source of the target
	# for AUTOMOC to generate its meta-object.
	set(extra_sources "")
	if(test_name STREQUAL "tst_downloadmanager")
		set(extra_sources TestHttpServer.h)
	endif()
	add_executable(${test_name} ${test_name}.cpp ${extra_sources})
	target_link_libraries(${test_name}
		pebear_update_core
		Qt${QT_VERSION_MAJOR}::Core
		Qt${QT_VERSION_MAJOR}::Network
		Qt${QT_VERSION_MAJOR}::Test
	)
	# the checked-in GitHub API fixture, located without a runtime search
	target_compile_definitions(${test_name} PRIVATE
		FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
	add_test(NAME ${test_name} COMMAND ${test_name})
	# QStandardPaths must not write into the developer's real config
	set_tests_properties(${test_name} PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	)
endforeach()

# pebear_update_core must never pull in QtWidgets: the GUI layer is compiled
# into the application instead. This is an architectural boundary, so it is
# checked rather than merely intended.
if(UNIX)
	add_test(NAME tst_no_widgets_dependency
		COMMAND ${CMAKE_COMMAND}
			-DLIB_FILE=$<TARGET_FILE:pebear_update_core>
			-P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_widgets.cmake
	)
endif()
