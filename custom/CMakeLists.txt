# ============================================================================
# QGroundPixel Custom Plugin
# QGroundControl + native WFB-NG (wifibroadcast) video reception via
# RTL8812AU USB adapters (OpenIPC ecosystem).
# ============================================================================

message(STATUS "QGC: Adding QGroundPixel Custom Plugin")

# ============================================================================
# Custom Android Package Template (overlay of android/ dir)
# ============================================================================

if(ANDROID)
    set(CUSTOM_ANDROID_DIR "${CMAKE_SOURCE_DIR}/custom/android")
    if(EXISTS "${CUSTOM_ANDROID_DIR}")
        file(GLOB CUSTOM_ANDROID_FILES "${CUSTOM_ANDROID_DIR}/*")
        if(CUSTOM_ANDROID_FILES)
            message(STATUS "QGC: Custom Android package template found. Overlaying custom files...")
            set(DEFAULT_ANDROID_DIR "${CMAKE_SOURCE_DIR}/android")
            set(FINAL_ANDROID_DIR "${CMAKE_BINARY_DIR}/custom/android")

            file(COPY "${DEFAULT_ANDROID_DIR}/." DESTINATION "${FINAL_ANDROID_DIR}")

            execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory "${CUSTOM_ANDROID_DIR}" "${FINAL_ANDROID_DIR}")

            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                "${DEFAULT_ANDROID_DIR}/"
                "${CUSTOM_ANDROID_DIR}/"
            )

            set(QGC_ANDROID_PACKAGE_SOURCE_DIR "${FINAL_ANDROID_DIR}" CACHE PATH "Path to a custom Android package template" FORCE)
            message(STATUS "QGC: Android package template path will be set to: ${QGC_ANDROID_PACKAGE_SOURCE_DIR}")
        endif()
    endif()
endif()

# ============================================================================
# Custom Resources
# ============================================================================

list(APPEND CUSTOM_RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/custom.qrc
)
set(QGC_RESOURCES ${QGC_RESOURCES} ${CUSTOM_RESOURCES} CACHE STRING "Paths to .qrc Resources" FORCE)

# ============================================================================
# Custom Plugin Sources
# ============================================================================

# VideonativeReceiver: PixelPilot's MediaCodec decoder as an alternative to the
# GStreamer pipeline (opt-in via the WFB-NG settings page, "native decoder").
set(CUSTOM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/QGroundPixelPlugin.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/QGroundPixelPlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VideonativeReceiver.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VideonativeReceiver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VtxHttpProxy.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VtxHttpProxy.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WfbngManager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WfbngManager.h
    CACHE INTERNAL "" FORCE
)

# Qt6::WebView: embedded Android WebView for the "VTX Web UI" settings page
# (Majestic / custom busybox-httpd link settings on the VTX).
# Qt6::CorePrivate: QtAndroidPrivate::startActivity for the VPN consent dialog (wfb-ng tunnel).
set(CUSTOM_LIBRARIES
    Qt6::WebView
    Qt6::CorePrivate
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_DEFINITIONS
    QGC_CUSTOM_BUILD
    CUSTOMHEADER="QGroundPixelPlugin.h"
    CUSTOMCLASS=QGroundPixelPlugin
    QGC_ENABLE_WEBVIEW
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_QT_COMPONENTS
    Core
    Multimedia
    WebView
    CACHE INTERNAL "" FORCE
)
