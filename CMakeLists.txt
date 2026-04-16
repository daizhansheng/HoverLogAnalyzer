cmake_minimum_required(VERSION 3.16)

project(HoverLogAnalyzer VERSION 0.1 LANGUAGES CXX)

# --- 1. 基础设置 ---
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# [调试开关] Windows下如果程序闪退，请把 OFF 改为 ON，这样能看到控制台报错
option(ENABLE_CONSOLE_DEBUG "Enable console output on Windows for debugging" OFF)

# --- 2. macOS SDK 设置 (保持原样) ---
if(APPLE)
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE MACOSX_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(MACOSX_SDK_PATH AND EXISTS "${MACOSX_SDK_PATH}")
        set(CMAKE_OSX_SYSROOT "${MACOSX_SDK_PATH}" CACHE PATH "macOS SDK" FORCE)
    endif()

    # 自动处理 OpenGL 头文件路径问题
    set(_GL_HEADER "${CMAKE_OSX_SYSROOT}/System/Library/Frameworks/OpenGL.framework/Headers/gl.h")
    if(NOT EXISTS "${_GL_HEADER}")
        set(_CLT_SDK "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
        if(EXISTS "${_CLT_SDK}/System/Library/Frameworks/OpenGL.framework/Headers/gl.h")
            set(CMAKE_OSX_SYSROOT "${_CLT_SDK}" CACHE PATH "macOS SDK" FORCE)
        endif()
    endif()

    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures" FORCE)
    endif()
endif()

# --- 3. 查找 Qt 组件 ---
# 注意：你的文件名包含 Chart，如果你使用了 QtCharts，必须在这里加上 Charts
# 如果没有用 Charts 模块，请删除 "Charts" 字样
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Charts Sql Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Charts Sql Concurrent)

# --- 4. 源文件列表 (基于你提供的 ls 输出) ---
set(PROJECT_SOURCES
    # Main
    main.cpp

    # PressAnalyzer
    PressAnalyzer.cpp
    PressAnalyzer.h
    PressAnalyzer.ui

    # ChartStyleManager
    ChartStyleManager.cpp
    ChartStyleManager.h

    # SelectableListWidget
    SelectableListWidget.cpp
    SelectableListWidget.h

    # HLogParser
    HLogParser.cpp
    HLogParser.h

    # LogParserWorker (后台解析线程)
    LogParserWorker.cpp
    LogParserWorker.h

    # HLogBinaryParser
    HLogBinaryParser.cpp
    HLogBinaryParser.h

    # --- 只有头文件的类 (Header Only) ---
    # 注意：如果这些其实有 .cpp 但你没发出来，必须把 .cpp 也加上！
    # 即使只有 .h，为了让 MOC 正常工作，也必须列在这里。
    BatteryChartWidget.h
    CameraTempChartWidget.h
    ChartBaseWidget.h
    ChartSearchDialog.h
    ChartViewportMixin.h
    DockToggleButton.h
    DynamicChartWidget.h
    DynamicChartWindow.h
    DynamicChartManager.h
    EventTimelineWidget.h
    FlightAnimationWidget.h
    LogNumberHighlighter.h
    ModuleUsageChart.h
    SearchResultHighlighter.h
    SearchResultTextView.h
    SocTempChartWidget.h

    # 资源
    icon.qrc
)

# macOS 图标
set(APP_ICON_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/logo.icns")
set_source_files_properties(${APP_ICON_MACOS} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
)

# --- 5. 生成可执行文件 ---
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(HoverLogAnalyzer
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        ${APP_ICON_MACOS}
    )
else()
    if(ANDROID)
        add_library(HoverLogAnalyzer SHARED ${PROJECT_SOURCES})
    else()
        add_executable(HoverLogAnalyzer ${PROJECT_SOURCES} ${APP_ICON_MACOS})
    endif()
endif()

# --- 6. 链接库 ---
# 链接 Widgets 和 Charts
target_link_libraries(HoverLogAnalyzer PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Charts
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::Concurrent
)

# --- 7. 属性设置 ---
# Bundle ID
if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.HoverLogAnalyzer)
endif()

# 控制台设置
if(ENABLE_CONSOLE_DEBUG)
    set(WIN32_VAR FALSE) # 显示黑框
else()
    set(WIN32_VAR TRUE)  # 隐藏黑框
endif()

set_target_properties(HoverLogAnalyzer PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE ${WIN32_VAR}
    MACOSX_BUNDLE_ICON_FILE logo
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

# --- 8. 安装部署 ---
include(GNUInstallDirs)
install(TARGETS HoverLogAnalyzer
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(HoverLogAnalyzer)
endif()
