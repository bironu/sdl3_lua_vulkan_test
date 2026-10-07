# モデル・モーションの読み込みと変換(PMX/VMD/VRM/VRMA/FBX)。ゲーム本体(engine)と、変換ツール(tools/)の両方が使う、独立したライブラリ。
# 描画(Vulkan)・ウィンドウ・シーンには依存しない(SDLはログとファイル・パスのためだけ)
file(GLOB model_files *.cpp)
add_library(model STATIC ${model_files})
target_include_directories(model PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(model PUBLIC
  SDL3::SDL3
  PkgConfig::BULLET # 物理演算(剛体・スプリングボーン)
  z                  # FBX・glTFの圧縮データ
  $<$<PLATFORM_ID:Darwin>:iconv>) # macOSはiconvを別ライブラリとしてリンクする(VMDのShift-JIS名の変換用)
