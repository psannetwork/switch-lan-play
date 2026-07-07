find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui")

add_executable(lan-play-gui 
    main.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(lan-play-gui PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_compile_definitions(lan-play-gui PRIVATE IMGUI_IMPL_OPENGL_LOADER_GL3W)

# 警告抑制を追加
if (${CMAKE_CXX_COMPILER_ID} STREQUAL GNU)
    target_compile_options(lan-play-gui PRIVATE "-Wno-error" "-Wno-maybe-uninitialized" "-Wno-unused-but-set-variable" "-Wno-cast-function-type")
endif()

target_link_libraries(lan-play-gui PRIVATE SDL2::SDL2main SDL2::SDL2 OpenGL::GL)
