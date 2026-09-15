cmake_minimum_required(VERSION 3.5)
project(qmdnsengine)

set(PROJECT_NAME "QMdnsEngine")
set(PROJECT_DESCRIPTION "Multicast DNS library for Qt applications")
set(PROJECT_AUTHOR "Nathan Osman")
set(PROJECT_URL "https://github.com/nitroshare/qmdnsengine")

set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 2)
set(PROJECT_VERSION_PATCH 1)
set(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})

# Build a shared library by default
option(BUILD_SHARED_LIBS "Build QMdnsEngine as a shared library" ON)

set(BIN_INSTALL_DIR bin CACHE STRING "Binary installation directory relative to the install prefix")
set(LIB_INSTALL_DIR lib CACHE STRING "Library installation directory relative to the install prefix")
set(INCLUDE_INSTALL_DIR include CACHE STRING "Header installation directory relative to the install prefix")

set(DOC_INSTALL_DIR share/doc/qmdnsengine CACHE STRING "Documentation installation directory relative to the install prefix")
set(EXAMPLES_INSTALL_DIR "${LIB_INSTALL_DIR}/qmdnsengine/examples" CACHE STRING "Examples installation directory relative to the install prefix")

if (CMAKE_PREFIX_PATH)
	message( STATUS "QMdnsEngine: Use CMAKE_PREFIX_PATH to locate Qt version to be used: ${CMAKE_PREFIX_PATH}" )
endif()

find_package(QT NAMES Qt6 Qt5 COMPONENTS Network REQUIRED)
message( STATUS "QMdnsEngine: Found Qt Version: ${QT_VERSION}" )

if (${QT_VERSION_MAJOR} GREATER_EQUAL 6 )
	SET(QT_MIN_VERSION "6.0.0")
else()
	SET(QT_MIN_VERSION "5.7.0")
endif()

if ( "${QT_VERSION}" VERSION_LESS "${QT_MIN_VERSION}" )
	message( FATAL_ERROR "Your Qt version is to old! Minimum required ${QT_MIN_VERSION}" )
endif()

find_package(Qt${QT_VERSION_MAJOR} ${QT_VERSION} COMPONENTS Network REQUIRED)

message( STATUS "QMdnsEngine: Qt version used: ${QT_VERSION}" )

set(CMAKE_AUTOMOC ON)

# Add compilation flags for warnings
if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

add_subdirectory(src)

option(BUILD_DOC "Build Doxygen documentation" OFF)
if(BUILD_DOC)
    find_package(Doxygen REQUIRED)
    add_subdirectory(doc)
endif()

option(BUILD_EXAMPLES "Build example applications" OFF)
if(BUILD_EXAMPLES)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)
    add_subdirectory(examples)
endif()

option(BUILD_TESTS "Build test suite" OFF)
if(BUILD_TESTS)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test REQUIRED)
    enable_testing()
    add_subdirectory(tests)
endif()

set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "${PROJECT_AUTHOR}")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})

set(CPACK_COMPONENT_DOCUMENTATION_DISPLAY_NAME "Documentation")
set(CPACK_COMPONENT_DOCUMENTATION_DESCRIPTION "Documentation generated for the library")
set(CPACK_COMPONENT_EXAMPLES_DISPLAY_NAME "Examples")
set(CPACK_COMPONENT_EXAMPLES_DESCRIPTION "Sample applications using the library")

include(CPack)
