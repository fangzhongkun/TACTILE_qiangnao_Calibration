cmake_minimum_required(VERSION 3.22)

# =====================================================================================================================
# Project
# =====================================================================================================================
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "TZ=UTC-8" bash -c "date +'%H%M'"
    OUTPUT_VARIABLE TIME_SUFFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT TIME_SUFFIX)
    string(TIMESTAMP TIME_SUFFIX "%H%M")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "TZ=UTC-8" bash -c "date +'%y%m%d'"
    OUTPUT_VARIABLE BUILD_DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT BUILD_DATE)
    string(TIMESTAMP BUILD_DATE "%y%m%d")
endif()

project(forceTactileCalibration VERSION 0.1.${BUILD_DATE}${TIME_SUFFIX} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Default build type: Release." FORCE)
endif()

# =====================================================================================================================
# Dependencies
# =====================================================================================================================
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/3rdparty/cmake-modules)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
set(WITH_GFLAGS OFF CACHE BOOL "Use gflags for google/glog" FORCE)
set(PROBE_BUILD_WITH_QT ON CACHE BOOL "Build probe with Qt" FORCE)
set(QT_DEFAULT_MAJOR_VERSION 6 CACHE STRING "Qt major version to use" FORCE)

# Keep Qt requirements minimal and explicit. ShaderTools/LinguistTools are optional and not needed for core build.
# Find Qt before adding probe so missing Qt errors point to the top-level project.
find_package(Qt6 COMPONENTS Core Gui Widgets Charts SerialPort REQUIRED)
find_package(Qt6 OPTIONAL_COMPONENTS LinguistTools)

add_subdirectory(3rdparty/glog EXCLUDE_FROM_ALL)
add_subdirectory(3rdparty/fmt EXCLUDE_FROM_ALL)
add_subdirectory(3rdparty/probe EXCLUDE_FROM_ALL)
add_subdirectory(3rdparty/json EXCLUDE_FROM_ALL)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(WIN32)
    set(CMAKE_WIN32_EXECUTABLE $<IF:$<CONFIG:DEBUG>,OFF,ON>)
endif()

# =====================================================================================================================
# Sources
# =====================================================================================================================
configure_file(src/version.h.in ${PROJECT_BINARY_DIR}/version.h @ONLY)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/*.cpp
    ${PROJECT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE LIBTOUCH_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/3rdparty/libtouch/*.cpp
)

file(GLOB_RECURSE WHISPER_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/3rdparty/whisper/*.cpp
)

set(RESOURCE_FILES scanner.qrc)
set(EXTRA_PLATFORM_SOURCES)
if(WIN32 AND EXISTS ${PROJECT_SOURCE_DIR}/ico_win32.rc)
    list(APPEND EXTRA_PLATFORM_SOURCES ico_win32.rc)
endif()

set(QM_FILES)
if(TARGET Qt6::lrelease AND EXISTS ${PROJECT_SOURCE_DIR}/languages)
    file(GLOB TS_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/languages/*.ts)
    if(TS_FILES)
        # qt6_create_translation feeds lupdate an @response file. lupdate reads that
        # file with the Windows ANSI code page, so UTF-8 paths containing non-ASCII
        # characters (this tree is under 强脑) are reported as missing. Release the
        # existing catalogs instead of rescanning sources on every build.
        qt6_add_translation(QM_FILES ${TS_FILES})
    endif()
endif()

add_executable(${PROJECT_NAME} WIN32
    ${APP_SOURCES}
    ${LIBTOUCH_SOURCES}
    ${WHISPER_SOURCES}
    ${RESOURCE_FILES}
    ${QM_FILES}
    ${EXTRA_PLATFORM_SOURCES}
)

# Keep runtime CSV mappings beside the executable for both single- and multi-config generators.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/resources/config
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/config
    COMMENT "Copying runtime mapping and calibration CSV files"
)

if(QM_FILES)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/languages"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${QM_FILES} "$<TARGET_FILE_DIR:${PROJECT_NAME}>/languages"
        COMMENT "Copying Qt translation catalogs"
    )
endif()

target_compile_options(${PROJECT_NAME}
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Zc:preprocessor /Zc:__cplusplus /wd5054 /bigobj>
        $<$<CXX_COMPILER_ID:MSVC>:/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Wno-deprecated-enum-enum-conversion>
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Charts
        Qt6::SerialPort
        glog::glog
        fmt::fmt
        probe::probe
        nlohmann_json::nlohmann_json
        $<$<PLATFORM_ID:Windows>:Winmm>
        $<$<PLATFORM_ID:Windows>:user32>
        $<$<PLATFORM_ID:Windows>:runtimeobject>
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/3rdparty
        ${PROJECT_SOURCE_DIR}/src/utils
        ${PROJECT_SOURCE_DIR}/src/widgets
        ${PROJECT_SOURCE_DIR}/src/pages
        ${PROJECT_BINARY_DIR}
)

# =====================================================================================================================
# Install/package
# =====================================================================================================================
if(WIN32)
    install(TARGETS ${PROJECT_NAME} DESTINATION . COMPONENT Applications)
    if(QM_FILES)
        install(FILES ${QM_FILES} DESTINATION languages COMPONENT Applications)
    endif()

    if(EXISTS ${PROJECT_SOURCE_DIR}/resources/config/mapping.csv)
        install(FILES resources/config/mapping.csv DESTINATION config COMPONENT Applications)
    endif()
    if(EXISTS ${PROJECT_SOURCE_DIR}/resources/config/region.csv)
        install(FILES resources/config/region.csv DESTINATION config COMPONENT Applications)
    endif()
    if(EXISTS ${PROJECT_SOURCE_DIR}/resources/config/load_steps.csv)
        install(FILES resources/config/load_steps.csv DESTINATION config COMPONENT Applications)
    endif()
    install(DIRECTORY resources/config/ DESTINATION config COMPONENT Applications
            FILES_MATCHING PATTERN "mapping_*.csv" PATTERN "region_*.csv")
    install(TARGETS ${PROJECT_NAME}
        COMPONENT Runtime
        RUNTIME_DEPENDENCIES
        PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-"
        POST_EXCLUDE_REGEXES ".*system32/.*\\.dll" "Qt.*.dll"
        DIRECTORIES $ENV{PATH}
        DESTINATION .
    )

    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION . COMPONENT Runtime)

    qt_generate_deploy_script(
        TARGET ${PROJECT_NAME}
        OUTPUT_SCRIPT qt_deploy_script
        CONTENT "
            qt_deploy_runtime_dependencies(
                EXECUTABLE \"$<TARGET_FILE:${PROJECT_NAME}>\"
                BIN_DIR .
                PLUGINS_DIR .
                NO_TRANSLATIONS
                NO_COMPILER_RUNTIME
                DEPLOY_TOOL_OPTIONS \"--no-opengl-sw\" \"--no-system-d3d-compiler\"
            )"
    )
    install(SCRIPT ${qt_deploy_script})

    set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_VENDOR "武汉华威科智能技术有限公司")
    set(CPACK_PACKAGE_HOMEPAGE_URL "EmbosenX-手套标定")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "EmbosenX-手套标定")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY ${PROJECT_NAME})
    set(CPACK_PACKAGE_CHECKSUM SHA512)
    set(CPACK_GENERATOR NSIS)
    set(CPACK_NSIS_PACKAGE_NAME "ScannerPT ${PROJECT_VERSION}")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "${PROJECT_NAME}.exe")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_DISPLAY_NAME ${PROJECT_NAME})

    if(EXISTS ${CMAKE_SOURCE_DIR}/resources/icons/icon.ico)
        set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/resources/icons/icon.ico")
        set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/resources/icons/icon.ico")
    endif()

    set(CPACK_NSIS_IGNORE_LICENSE_PAGE ON)
    set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
    set(CPACK_NSIS_MODIFY_PATH ON)
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "${PROJECT_NAME}.exe")
    set(CPACK_PACKAGE_EXECUTABLES ${PROJECT_NAME} "EmbosenX-手套标定")
    set(CPACK_CREATE_DESKTOP_LINKS ${PROJECT_NAME})
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-Windows-${CMAKE_HOST_SYSTEM_PROCESSOR}")
    include(CPack)
endif()
