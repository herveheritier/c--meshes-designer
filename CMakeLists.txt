cmake_minimum_required(VERSION 3.16)
project(MeshEditor2D CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Dépendances système (SDL2 + OpenGL) --------------------------------
# Linux/macOS : paquet SDL2 (libsdl2-dev / brew sdl2).
# Windows     : installer SDL2 (vcpkg ou https://libsdl.org) et définir SDL2_DIR.
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

# --- Cœur du modèle (sans GUI) ------------------------------------------
add_library(meshcore STATIC
  src/io.cpp
  src/mesh.cpp
  src/pngexport.cpp
  src/svgparse.cpp
  src/triangulate.cpp
)
target_include_directories(meshcore PUBLIC src)
target_compile_options(meshcore PRIVATE -Wall -Wextra)

# --- Application (GUI) ---------------------------------------------------
add_executable(mesheditor
  src/main.cpp
  src/app.cpp
  src/camera.cpp
  src/io.cpp
  src/renderer.cpp
  src/svgicon.cpp
  src/ui.cpp
  external/imgui/imgui.cpp
  external/imgui/imgui_draw.cpp
  external/imgui/imgui_tables.cpp
  external/imgui/imgui_widgets.cpp
  external/imgui/backends/imgui_impl_sdl2.cpp
  external/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(mesheditor PRIVATE
  src
  external
  external/imgui
  external/imgui/backends
)
target_compile_options(mesheditor PRIVATE -Wall -Wextra)
target_link_libraries(mesheditor PRIVATE meshcore SDL2::SDL2 OpenGL::GL)

# --- Tests headless (opérations mesh + triangulation) --------------------
add_executable(meshtest src/meshtest.cpp)
target_compile_options(meshtest PRIVATE -Wall -Wextra)
target_link_libraries(meshtest PRIVATE meshcore)

# std::filesystem : lien explicite requis par certaines chaînes GCC (< 9).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(mesheditor PRIVATE stdc++fs)
  target_link_libraries(meshtest PRIVATE stdc++fs)
endif()
