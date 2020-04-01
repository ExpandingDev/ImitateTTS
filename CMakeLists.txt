cmake_minimum_required(VERSION 3.00)

project(Imitate VERSION 0.1)
#Pretty sure that dbus-cxx-1.0 requires C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(imitate main.cpp src/ImitateTTSService.cpp src/cmu/mimic_voice_list.c)

target_include_directories(imitate PUBLIC "${PROJECT_BINARY_DIR}"
						  "${PROJECT_BINARY_DIR}/include")

#We're gonna use pkg-config to include all of our libraries
include(FindPkgConfig)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(MIMIC REQUIRED mimic>=1.0)
pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GST_AUDIO REQUIRED gstreamer-audio-1.0)
pkg_check_modules(DBUSCXX REQUIRED dbus-cxx-1.0)
pkg_check_modules(BTTS REQUIRED buckey-tts)
pkg_check_modules(BUIO REQUIRED buckey-uio)

#Set ENABLE_DEBUG to ON to enable debug log messages
option(ENABLE_DEBUG "Enable debugging messages" OFF)

#Below is the default voice name
set(DEFAULT_VOICE_NAME ap)

#Below enables compilation with desired voices. To save space/memory you can disable ones that you don't use.
option(ENABLE_CMU_TIME_AWB "Compile with the CMU_TIME_AWB voice" 1)
option(ENABLE_CMU_US_AWB  "Compile with the CMU_US_AWB voice" 1)
option(ENABLE_CMU_US_KAL  "Compile with the CMU_US_KAL voice" 1)
option(ENABLE_CMU_US_KAL16  "Compile with the CMU_US_KAL16 voice" 1)
option(ENABLE_CMU_US_SLT  "Compile with the CMU_US_SLT voice" 1)
option(ENABLE_CMU_US_RMS  "Compile with the CMU_US_RMS voice" 1)
option(ENABLE_CMU_US_SLT_HTS  "Compile with the CMU_US_SLT_HTS voice" 1)
option(ENABLE_CMU_VID_GB_AP  "Compile with the CMU_VID_GB_AP voice" 1)

configure_file(config.h.in config.h)
configure_file(res/imitate.conf.in res/imitate.conf)

if (ENABLE_DEBUG)
    #add_compile_definitions(ENABLE_DEBUG)
    add_definitions(-DENABLE_DEBUG)
endif()

target_include_directories(imitate PUBLIC "${GLIB_INCLUDE_DIRS}"  "${MIMIC_INCLUDE_DIRS}" "${GST_INCLUDE_DIRS}" "${GST_AUDIO_INCLUDE_DIRS}" "${DBUSCXX_INCLUDE_DIRS}" "${BTTS_INCLUDE_DIRS}" "${BUIO_INCLUDE_DIRS}")

target_link_libraries(imitate PUBLIC "${GLIB_LDFLAGS}" "${MIMIC_LDFLAGS}" "${GST_LDFLAGS}" "${GST_AUDIO_LDFLAGS}" "${DBUSCXX_LDFLAGS}" "${BTTS_LDFLAGS}" "${BUIO_LDFLAGS}")

#Install the binary
install(TARGETS imitate DESTINATION /usr/bin)

#Install the default config file
install(FILES res/imitate.conf DESTINATION /etc/imitate)
install(FILES res/prepared.txt DESTINATION /etc/imitate/prepared)
