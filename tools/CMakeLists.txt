# ゲーム本体(sdl3_lua5_vulkan)とは別の、コマンドラインのツール。ウィンドウ・Vulkanは使わず、modelライブラリ(読み込みと変換)だけをリンクする。
# 実行ファイルは、ビルドディレクトリの tools/ に作られる。パスの引数は、リポジトリ直下からの相対パス
# (今後のツール(フィールドエディタなど)は、ここに足して、必要ならengineライブラリもリンクする)

add_executable(vmd2vrma vmd2vrma.cpp)
target_link_libraries(vmd2vrma PRIVATE model)

add_executable(fbx2vrma fbx2vrma.cpp)
target_link_libraries(fbx2vrma PRIVATE model)

# フィールドエディタ(ウィンドウ・Vulkanを使うので、engineライブラリをリンクする)。シェーダー(SPIR-V)は、実行ファイルの横のshaders/から読むので、ゲーム本体のものをコピーする
add_executable(field_editor field_editor/main.cpp field_editor/FieldEditorScene.cpp)
target_link_libraries(field_editor PRIVATE engine)
add_dependencies(field_editor shaders)
add_custom_command(TARGET field_editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/src/shaders $<TARGET_FILE_DIR:field_editor>/shaders
  COMMENT "copy shaders for field_editor")
