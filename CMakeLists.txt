cmake_minimum_required(VERSION 3.21)

project(comfyui_cpp_ui VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Network)

qt_standard_project_setup(REQUIRES 6.8)

# these three files must sit next to this CMakeLists.txt
foreach(f json.hpp httplib.h ComfyUI_00994_2.json)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${f})
        message(FATAL_ERROR "Missing file: ${f}  (copy it next to CMakeLists.txt)")
    endif()
endforeach()

qt_add_executable(appcomfyui_cpp_ui
    main.cpp
    comfyclient.h
    comfyclient.cpp
    progresssocket.h
    progresssocket.cpp
    workflowbuilder.h
)

qt_add_qml_module(appcomfyui_cpp_ui
    URI comfyui_cpp_ui
    VERSION 1.0
    QML_FILES
        Main.qml
    RESOURCES
        ComfyUI_00994_2.json      # the workflow is packed INSIDE the app (works on Android too)
)

target_link_libraries(appcomfyui_cpp_ui
    PRIVATE Qt6::Quick Qt6::Network
)

# Android: use our manifest (allows internet + plain http to your PC)
if(ANDROID)
    set_property(TARGET appcomfyui_cpp_ui APPEND PROPERTY
        QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android)
endif()

# httplib needs the Windows network library
if(WIN32)
    target_link_libraries(appcomfyui_cpp_ui PRIVATE ws2_32)
endif()

include(GNUInstallDirs)
install(TARGETS appcomfyui_cpp_ui
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
