# ゲーム本体(sdl3_lua5_vulkan)とは別の、コマンドラインのツール。ウィンドウ・Vulkanは使わず、modelライブラリ(読み込みと変換)だけをリンクする。
# 実行ファイルは、ゲーム本体と同じ dist/ に作られる(最上位のCMakeLists.txt)。パスの引数は、リポジトリ直下からの相対パス
# (今後のツール(フィールドエディタなど)は、ここに足して、必要ならengineライブラリもリンクする)

add_executable(vmd2vrma vmd2vrma.cpp)
target_link_libraries(vmd2vrma PRIVATE model)

add_executable(fbx2vrma fbx2vrma.cpp)
target_link_libraries(fbx2vrma PRIVATE model)

# フィールドエディタ(ウィンドウ・Vulkanを使うので、engineライブラリをリンクする)。シェーダー(SPIR-V)は、実行ファイルの横のshaders/(dist/shaders/。ゲーム本体と共通)から読む
add_executable(field_editor field_editor/main.cpp field_editor/FieldEditorScene.cpp)
target_link_libraries(field_editor PRIVATE engine)
add_dependencies(field_editor shaders)

# 丸い生き物の敵(形とモーション)を、glTF 2.0 のバイナリ(.glb)に書き出す(敵の定義の読み込み・CreatureAnimator を使うので、engineライブラリをリンクする)
add_executable(creature2glb creature2glb.cpp)
target_link_libraries(creature2glb PRIVATE engine)
