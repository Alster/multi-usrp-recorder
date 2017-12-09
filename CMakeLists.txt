cmake_minimum_required (VERSION 2.6)
project (LLRecorder)

########################################################################
# Check Compiler Version
########################################################################
# Full C++11 came with GCC 4.7.
SET(GCC_MIN_VERSION "4.8.0")
# for c++0x or c++11 support, require:
#   Apple Clang >= 500
# or
#   Clang >= 3.3.0
SET(CLANG_MIN_VERSION "3.3.0")
SET(APPLECLANG_MIN_VERSION "500")

IF(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
    IF(DEFINED CMAKE_CXX_COMPILER_VERSION)
        IF(${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS ${GCC_MIN_VERSION})
            MESSAGE(WARNING "\nThe compiler selected to build UHD (GCC version ${CMAKE_CXX_COMPILER_VERSION} : ${CMAKE_CXX_COMPILER}) is older than that officially supported (${GCC_MIN_VERSION} minimum). This build may or not work. We highly recommend using a more recent GCC version.")
         ENDIF()
    ELSE()
        MESSAGE(WARNING "\nCannot determine the version of the compiler selected to build UHD (GCC : ${CMAKE_CXX_COMPILER}). This build may or not work. We highly recommend using GCC version ${GCC_MIN_VERSION} or more recent.")
    ENDIF()
    SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Og")
ELSEIF(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    EXECUTE_PROCESS(COMMAND
        ${CMAKE_CXX_COMPILER} -v
        RESULT_VARIABLE res ERROR_VARIABLE err
        ERROR_STRIP_TRAILING_WHITESPACE)
    IF(${res} STREQUAL "0")
        # output is in error stream
        STRING(REGEX MATCH "^Apple.*" IS_APPLE ${err})
        IF("${IS_APPLE}" STREQUAL "")
            SET(MIN_VERSION ${CLANG_MIN_VERSION})
            SET(APPLE_STR "")
            # retrieve the compiler's version from it
            STRING(REGEX MATCH "clang version [0-9.]+" CLANG_OTHER_VERSION ${err})
            STRING(REGEX MATCH "[0-9.]+" CLANG_VERSION ${CLANG_OTHER_VERSION})
        ELSE()
            SET(MIN_VERSION ${APPLECLANG_MIN_VERSION})
            SET(APPLE_STR "Apple ")
            # retrieve the compiler's version from it
            STRING(REGEX MATCH "(clang-[0-9.]+)" CLANG_APPLE_VERSION ${err})
            STRING(REGEX MATCH "[0-9.]+" CLANG_VERSION ${CLANG_APPLE_VERSION})
        ENDIF()
        IF(${CLANG_VERSION} VERSION_LESS "${MIN_VERSION}")
            MESSAGE(WARNING "\nThe compiler selected to build UHD (${APPLE_STR}Clang version ${CLANG_VERSION} : ${CMAKE_CXX_COMPILER}) is older than that officially supported (${MIN_VERSION} minimum). This build may or not work. We highly recommend using Apple Clang version ${APPLECLANG_MIN_VERSION} or more recent, or Clang version ${CLANG_MIN_VERSION} or more recent.")
        ENDIF()
    ELSE()
        MESSAGE(WARNING "\nCannot determine the version of the compiler selected to build UHD (${APPLE_STR}Clang : ${CMAKE_CXX_COMPILER}). This build may or not work. We highly recommend using Apple Clang version ${APPLECLANG_MIN_VERSION} or more recent, or Clang version ${CLANG_MIN_VERSION} or more recent.")
    ENDIF()
ENDIF()

IF(CMAKE_VERSION VERSION_LESS "3.1")
    IF(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
        SET(CMAKE_CXX_FLAGS "--std=gnu++11 ${CMAKE_CXX_FLAGS}")
    ELSEIF(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
        IF("${IS_APPLE}" STREQUAL "")
            SET(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}")
        ELSE()
            SET(CMAKE_CXX_FLAGS "-std=c++11 -stdlib=libc++ ${CMAKE_CXX_FLAGS}")
        ENDIF()
    ENDIF()
ELSE()
    SET(CMAKE_CXX_STANDARD 11)
ENDIF()

IF(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" AND ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    SET(CMAKE_EXE_LINKER_FLAGS "-lthr ${CMAKE_EXE_LINKER_FLAGS}")
    SET(CMAKE_CXX_FLAGS "-stdlib=libc++ ${CMAKE_CXX_FLAGS}")
ENDIF()

########################################################################
# Setup UHD
########################################################################
MESSAGE(STATUS "")
MESSAGE(STATUS "Search for UHD.. aazazazazaz")
FIND_PACKAGE(UHD 0.29.1)

########################################################################
# Setup Boost
########################################################################
MESSAGE(STATUS "")
MESSAGE(STATUS "Configuring Boost C++ Libraries...")
SET(BOOST_REQUIRED_COMPONENTS
    chrono
    date_time
    filesystem
    program_options
    regex
    system
    unit_test_framework
    serialization
)
IF(MINGW)
    LIST(APPEND BOOST_REQUIRED_COMPONENTS thread_win32)
ELSE()
    LIST(APPEND BOOST_REQUIRED_COMPONENTS thread)
ENDIF()

IF(UNIX AND NOT BOOST_ROOT AND EXISTS "/usr/lib64")
    LIST(APPEND BOOST_LIBRARYDIR "/usr/lib64") #fedora 64-bit fix
ENDIF(UNIX AND NOT BOOST_ROOT AND EXISTS "/usr/lib64")

IF(MSVC)
    SET(BOOST_ALL_DYN_LINK "${BOOST_ALL_DYN_LINK}" CACHE BOOL "boost enable dynamic linking")
    IF(BOOST_ALL_DYN_LINK)
        ADD_DEFINITIONS(-DBOOST_ALL_DYN_LINK) #setup boost auto-linking in msvc
    ELSE(BOOST_ALL_DYN_LINK)
        SET(BOOST_REQUIRED_COMPONENTS) #empty components list for static link
    ENDIF(BOOST_ALL_DYN_LINK)
ENDIF(MSVC)

SET(Boost_ADDITIONAL_VERSIONS
    "1.46.0" "1.46" "1.47.0" "1.47" "1.48.0" "1.48" "1.48.0" "1.49" "1.50.0" "1.50"
    "1.51.0" "1.51" "1.52.0" "1.52" "1.53.0" "1.53" "1.54.0" "1.54" "1.55.0" "1.55"
    "1.56.0" "1.56" "1.57" "1.57" "1.58" "1.59" "1.60" "1.61"
)
FIND_PACKAGE(Boost 1.53 COMPONENTS ${BOOST_REQUIRED_COMPONENTS})

INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})
LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})

MESSAGE(STATUS "Boost include directories: ${Boost_INCLUDE_DIRS}")
MESSAGE(STATUS "Boost library directories: ${Boost_LIBRARY_DIRS}")
MESSAGE(STATUS "Boost libraries: ${Boost_LIBRARIES}")

add_executable(llrecorder_1 llrecorder_1.cpp)

TARGET_LINK_LIBRARIES(llrecorder_1 uhd ${Boost_LIBRARIES})