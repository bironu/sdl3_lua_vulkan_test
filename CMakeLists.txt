cmake_minimum_required(VERSION 3.20)
project(SDL3_Lua5_Vulkan LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(SDL3_mixer REQUIRED CONFIG)

# Lua 5.5 (Homebrew: lua / lua@5.5)。pkg-config 経由で取得する
# sol2(C++のLuaバインディング。Homebrew: sol2)
find_package(sol2 CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LUA REQUIRED IMPORTED_TARGET lua-5.5)
# Bullet(物理演算。Homebrew: bullet)
pkg_check_modules(BULLET REQUIRED IMPORTED_TARGET bullet)

# Vulkan (Homebrew: vulkan-headers / vulkan-loader / molten-vk)
find_package(Vulkan REQUIRED)

# 開発中のデータ(res/)の探し方: ONなら、実行ファイルの横に res/ が無いとき、ビルド元のリポジトリの res/ を使う(ResourcePaths::resource)。
# フィールドエディタ・置物の取り込みも、ビルド元の res/ へ直接書く。ビルド環境の絶対パスが実行ファイルに埋め込まれるので、
# 配布用のビルドでは OFF にする(その場合は、実行ファイルの横の res/ だけを使う)
option(APP_DEV_RESOURCE_ROOT "Use the source tree's res/ when res/ is not next to the executable (development only)" ON)
if(APP_DEV_RESOURCE_ROOT)
  add_compile_definitions(APP_RESOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/")
endif()

# 実行ファイルの出力先: リポジトリ直下の dist/(.gitignore 済み)。インストール後と同じ並びにする:
#   dist/sdl3_lua5_vulkan  dist/field_editor ...  dist/shaders/*.spv   (res/ は、開発中はビルド元のものを使うので、ここには置かない)
# ビルドの中間ファイルは、これまでどおり build/ に残る
set(APP_DIST_DIR ${PROJECT_SOURCE_DIR}/dist CACHE PATH "Where the executables and shaders/ are written")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${APP_DIST_DIR})

add_subdirectory(src)
# 変換ツールなどの、ゲーム本体とは別の実行ファイル(必要なライブラリだけをリンクする)
add_subdirectory(tools)
