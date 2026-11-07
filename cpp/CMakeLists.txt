cmake_minimum_required(VERSION 3.20)
project(WowErraiLauncher VERSION 3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS 
    Core 
    Widgets 
    Network 
    Multimedia
    Concurrent
    LinguistTools
)

# Source files
set(SOURCES
    src/main.cpp
    src/core/ConfigManager.cpp
    src/core/SoundManager.cpp
    src/core/NotesManager.cpp
    src/ui/WowButton.cpp
    src/ui/MainWindow.cpp
    src/ui/NotesWindow.cpp
    src/ui/RealmlistWindow.cpp
    src/ui/DownloadDialog.cpp
    src/ui/SettingsDialog.cpp
    src/ui/ServerEditDialog.cpp
    src/ui/AddonsWindow.cpp
    src/utils/PathUtils.cpp
    src/core/HdPatchManager.cpp
    src/core/AddonRegistry.cpp
    src/core/AddonManifest.cpp
    src/core/GameEdition.cpp
    src/core/ReminderService.cpp
    src/core/BuildInfoPatcher.cpp
    src/ui/ButtonStyles.cpp
    src/core/ErraiNetwork.cpp
    src/core/InstallProgress.cpp
)

# Header files
set(HEADERS
    src/core/ConfigManager.h
    src/core/SoundManager.h
    src/core/NotesManager.h
    src/ui/WowButton.h
    src/ui/MainWindow.h
    src/ui/NotesWindow.h
    src/ui/RealmlistWindow.h
    src/ui/DownloadDialog.h
    src/ui/SettingsDialog.h
    src/ui/ServerEditDialog.h
    src/ui/AddonsWindow.h
    src/utils/PathUtils.h
    src/core/HdPatchManager.h
    src/core/AddonRegistry.h
    src/core/AddonManifest.h
    src/core/GameEdition.h
    src/core/ReminderService.h
    src/core/BuildInfoPatcher.h
    src/ui/ButtonStyles.h
    src/core/ErraiNetwork.h
    src/core/InstallProgress.h
)

# Resources
set(RESOURCES
    resources/resources.qrc
)

# Translations
set(TS_FILES
    resources/translations/SylvaniaLauncher_en.ts
)
set_source_files_properties(${TS_FILES} PROPERTIES OUTPUT_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../Asset/translations)
qt_add_translation(QM_FILES ${TS_FILES})

# Create executable
add_executable(${PROJECT_NAME} WIN32
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
    ${TS_FILES}
    ${QM_FILES}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    Qt6::Concurrent
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
)

# Windows specific settings
if(WIN32)
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc")
    set(APP_ICON_OBJ "${CMAKE_CURRENT_BINARY_DIR}/app_icon.obj")
    
    add_custom_command(OUTPUT "${APP_ICON_OBJ}"
        COMMAND windres -J rc -O coff app.rc -o "${APP_ICON_OBJ}"
        DEPENDS "${APP_ICON_RESOURCE_WINDOWS}" "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        VERBATIM
    )
    
    set_source_files_properties("${APP_ICON_OBJ}" PROPERTIES EXTERNAL_OBJECT true GENERATED true)
    target_sources(${PROJECT_NAME} PRIVATE "${APP_ICON_OBJ}")
    
    # Copy Qt DLLs to output directory
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Qt6::Core>
        $<TARGET_FILE:Qt6::Widgets>
        $<TARGET_FILE:Qt6::Network>
        $<TARGET_FILE:Qt6::Multimedia>
        $<TARGET_FILE:Qt6::Concurrent>
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()

# Installation
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Copy assets to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../Asset"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Asset"
)
