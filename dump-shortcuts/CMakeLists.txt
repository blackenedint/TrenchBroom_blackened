set(DUMP_SHORTCUTS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(DUMP_SHORTCUTS_SOURCE
        "${DUMP_SHORTCUTS_SOURCE_DIR}/KeyStrings.h"
        "${DUMP_SHORTCUTS_SOURCE_DIR}/KeyStrings.cpp"
        "${DUMP_SHORTCUTS_SOURCE_DIR}/Main.cpp")
set(DUMP_SHORTCUTS_SOURCE ${DUMP_SHORTCUTS_SOURCE} ${INDEX_OUTPUT_PATH} ${DOC_MANUAL_TARGET_FILES_ABSOLUTE} ${DOC_MANUAL_SHORTCUTS_JS_TARGET_ABSOLUTE} ${DOC_MANUAL_TARGET_IMAGE_FILES_ABSOLUTE})

add_executable(dump-shortcuts ${DUMP_SHORTCUTS_SOURCE})
target_include_directories(dump-shortcuts PRIVATE ${DUMP_SHORTCUTS_SOURCE_DIR})
target_link_libraries(dump-shortcuts PRIVATE common fmt::fmt-header-only)
set_target_properties(dump-shortcuts PROPERTIES AUTOMOC TRUE)

set_compiler_config(dump-shortcuts)

# Organize files into IDE folders
source_group(TREE "${COMMON_TEST_SOURCE_DIR}" FILES ${COMMON_TEST_SOURCE})


if(WIN32)
    # add Qt deployment stuff for dump-shortcuts; todo; add a way to check if it's already available?
	# this only matters when Qt isn't in a system path.
    get_target_property(TB_QMAKE_PATH Qt6::qmake IMPORTED_LOCATION)
    string(REPLACE "qmake" "windeployqt" TB_WINDEPLOYQT_PATH "${TB_QMAKE_PATH}")

    if (NOT TB_SKIP_WINDEPLOYQT)
        message(STATUS "windeployqt (dump-shortcuts) requested: ${TB_WINDEPLOYQT_PATH}")
    
        # Run windeployqt on the dump-shortcuts binary so it copies Qt6*.dll and plugins next to it
        add_custom_command(TARGET dump-shortcuts POST_BUILD
            COMMAND "${TB_WINDEPLOYQT_PATH}"
                --no-compiler-runtime
                $<$<CONFIG:Debug>:--no-translations>
                "$<TARGET_FILE:dump-shortcuts>"
            COMMENT "Deploying Qt runtime for dump-shortcuts"
        )
    else()
        message(STATUS "windeployqt (dump-shortcuts) skipped")
    endif()

    # Copy DLLs to app directory
    add_custom_command(TARGET dump-shortcuts POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:assimp::assimp>" "$<TARGET_FILE_DIR:dump-shortcuts>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:freeimage::FreeImage>" "$<TARGET_FILE_DIR:dump-shortcuts>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:freetype>" "$<TARGET_FILE_DIR:dump-shortcuts>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:tinyxml2::tinyxml2>" "$<TARGET_FILE_DIR:dump-shortcuts>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:miniz::miniz>" "$<TARGET_FILE_DIR:dump-shortcuts>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:GLEW::GLEW>" "$<TARGET_FILE_DIR:dump-shortcuts>")
endif()
