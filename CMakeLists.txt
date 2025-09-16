cmake_minimum_required(VERSION 3.16)

project(HoverLogAnalyzer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------- macOS SDK/arch auto-detect --------
if(APPLE)
    # Prefer auto-detected SDK from xcrun to avoid stale/invalid sysroot paths
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE MACOSX_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(MACOSX_SDK_PATH AND EXISTS "${MACOSX_SDK_PATH}")
        set(CMAKE_OSX_SYSROOT "${MACOSX_SDK_PATH}" CACHE PATH "macOS SDK" FORCE)
    endif()
    # Fallback to CommandLineTools SDK if OpenGL headers are missing in primary SDK (Xcode 16/macOS 15)
    set(_GL_HEADER "${CMAKE_OSX_SYSROOT}/System/Library/Frameworks/OpenGL.framework/Headers/gl.h")
    if(NOT EXISTS "${_GL_HEADER}")
        set(_CLT_SDK "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
        if(EXISTS "${_CLT_SDK}/System/Library/Frameworks/OpenGL.framework/Headers/gl.h")
            message(STATUS "OpenGL headers not found in ${CMAKE_OSX_SYSROOT}, switching sysroot to CommandLineTools SDK: ${_CLT_SDK}")
            set(CMAKE_OSX_SYSROOT "${_CLT_SDK}" CACHE PATH "macOS SDK" FORCE)
        else()
            message(WARNING "OpenGL headers not found in current SDKs. Qt5 may fail to configure. Install Command Line Tools (xcode-select --install) or use an older SDK that contains OpenGL headers.")
        endif()
    endif()
    # Ensure correct arch for Apple Silicon kit
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for Mac OS X" FORCE)
    endif()
endif()

# 查找 Qt Widgets
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# 项目源文件
set(PROJECT_SOURCES
    main.cpp
    PressAnalyzer.cpp
    PressAnalyzer.h
    PressAnalyzer.ui
    ChartStyleManager.cpp
    ChartStyleManager.h
    icon.qrc          # 窗口图标和按钮图标资源
)

# macOS Dock 图标 (.icns)
set(APP_ICON_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/logo.icns")
set_source_files_properties(${APP_ICON_MACOS} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
)

# 可执行文件
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(HoverLogAnalyzer
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        ${APP_ICON_MACOS}   # 加入 Dock 图标
    )
else()
    if(ANDROID)
        add_library(HoverLogAnalyzer SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(HoverLogAnalyzer
            ${PROJECT_SOURCES}
            ${APP_ICON_MACOS}
            SearchResultHighlighter.h
            SearchResultTextView.h
            BatteryChartWidget.h
            SocTempChartWidget.h
            DockToggleButton.h
            CameraTempChartWidget.h

            ModuleUsageChart.h
            # 加入 Dock 图标
        )
    endif()
endif()

target_link_libraries(HoverLogAnalyzer PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# Bundle ID（Qt < 6.1 手动设置）
if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.HoverLogAnalyzer)
endif()

# 设置 macOS bundle 属性
set_target_properties(HoverLogAnalyzer PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_ICON_FILE logo         # 只写文件名，不写路径
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

# 安装规则
include(GNUInstallDirs)
install(TARGETS HoverLogAnalyzer
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Qt6 执行最终化
if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(HoverLogAnalyzer)
endif()

