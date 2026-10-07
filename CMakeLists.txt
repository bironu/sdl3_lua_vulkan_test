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

# 開発中にデータ(res/)を探すためのビルド元のパス(ResourcePaths::resource)
add_compile_definitions(APP_RESOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/")

add_subdirectory(src)
# 変換ツールなどの、ゲーム本体とは別の実行ファイル(必要なライブラリだけをリンクする)
add_subdirectory(tools)
