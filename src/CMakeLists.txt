# ライブラリの構成:
#   model  ... モデル・モーションの読み込みと変換(独立した静的ライブラリ。src/model/CMakeLists.txt)
#   engine ... ゲームの土台(app, field, geo, resources, scene, sdl, task, ui, vk)をまとめた静的ライブラリ。modelに依存する
# 実行ファイルは、必要なライブラリだけをリンクする: ゲーム本体(下のapp_main)はengine、変換ツール(tools/)はmodelだけ。
# 今後のツール(フィールドエディタなど)も、engineをリンクして同じ部品を使える。
# engineの各モジュールは、フォルダごとにOBJECTライブラリとして管理する(各フォルダのCMakeLists.txtで、file(GLOB)でそのフォルダの*.cppを集めている)
add_subdirectory(model)
add_subdirectory(app)
add_subdirectory(field)
add_subdirectory(geo)
add_subdirectory(resources)
add_subdirectory(scene)
add_subdirectory(sdl)
add_subdirectory(task)
add_subdirectory(ui)
add_subdirectory(vk)

set(ENGINE_MODULES app field geo resources scene sdl task ui vk)

foreach(module ${ENGINE_MODULES})
  target_include_directories(${module} PUBLIC ${PROJECT_SOURCE_DIR}/include)
  target_link_libraries(${module} PUBLIC
    model
    SDL3_image::SDL3_image SDL3_ttf::SDL3_ttf SDL3_mixer::SDL3_mixer
    PkgConfig::LUA Vulkan::Vulkan sol2::sol2)
endforeach()

set(ENGINE_OBJECTS)
foreach(module ${ENGINE_MODULES})
  list(APPEND ENGINE_OBJECTS $<TARGET_OBJECTS:${module}>)
endforeach()
add_library(engine STATIC ${ENGINE_OBJECTS})
target_include_directories(engine PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(engine PUBLIC
  model
  SDL3_image::SDL3_image SDL3_ttf::SDL3_ttf SDL3_mixer::SDL3_mixer
  PkgConfig::LUA Vulkan::Vulkan sol2::sol2)

add_executable(app_main main.cpp)
set_target_properties(app_main PROPERTIES OUTPUT_NAME sdl3_lua5_vulkan)
target_link_libraries(app_main PRIVATE engine)

# res/shaders/ の GLSL → SPIR-V。実行ファイルと同じ場所の shaders/ に出力する（実行時は SDL_GetBasePath()/shaders/ から読む）
find_program(GLSLC glslc REQUIRED)
set(SHADER_SOURCES triangle.vert triangle.frag sprite.vert sprite.frag shadow.vert triangle_pre.vert shadow_pre.vert outline.vert outline.frag outline_opaque.frag outline_pre.vert post.vert post.frag skin.comp)
set(SHADER_SPVS)
foreach(shader ${SHADER_SOURCES})
  set(spv ${APP_DIST_DIR}/shaders/${shader}.spv)
  add_custom_command(
    OUTPUT ${spv}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_DIST_DIR}/shaders
    COMMAND ${GLSLC} ${PROJECT_SOURCE_DIR}/res/shaders/${shader} -o ${spv}
    DEPENDS ${PROJECT_SOURCE_DIR}/res/shaders/${shader}
    COMMENT "glslc ${shader}"
    VERBATIM)
  list(APPEND SHADER_SPVS ${spv})
endforeach()
# 同じ triangle.frag を -DOPAQUE_PASS でコンパイルした、不透明の材質用の版(discardなし)
set(opaque_spv ${APP_DIST_DIR}/shaders/triangle_opaque.frag.spv)
add_custom_command(
  OUTPUT ${opaque_spv}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_DIST_DIR}/shaders
  COMMAND ${GLSLC} -DOPAQUE_PASS ${PROJECT_SOURCE_DIR}/res/shaders/triangle.frag -o ${opaque_spv}
  DEPENDS ${PROJECT_SOURCE_DIR}/res/shaders/triangle.frag
  COMMENT "glslc triangle.frag (OPAQUE_PASS)"
  VERBATIM)
list(APPEND SHADER_SPVS ${opaque_spv})
# アルファの切り抜き(MASK)の材質用の版(-DMASK_PASS)
set(mask_spv ${APP_DIST_DIR}/shaders/triangle_mask.frag.spv)
add_custom_command(
  OUTPUT ${mask_spv}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_DIST_DIR}/shaders
  COMMAND ${GLSLC} -DMASK_PASS ${PROJECT_SOURCE_DIR}/res/shaders/triangle.frag -o ${mask_spv}
  DEPENDS ${PROJECT_SOURCE_DIR}/res/shaders/triangle.frag
  COMMENT "glslc triangle.frag (MASK_PASS)"
  VERBATIM)
list(APPEND SHADER_SPVS ${mask_spv})
add_custom_target(shaders DEPENDS ${SHADER_SPVS})
add_dependencies(app_main shaders)

# 実行時のデータ(res/: 画像・フォント・音・lua・model・motion)は、実行ファイルの横のres/を先に探し(配布時はinstallでコピーする)、
# 無ければビルド元のリポジトリのres/を使う(ResourcePaths::resource。APP_DEV_RESOURCE_ROOTがONのときだけ。最上位のCMakeLists.txtで設定)。開発中はシンボリックリンクもコピーも不要

# インストール(cmake --install build --prefix <出力先>): 配布する並びは、実行ファイルと同じ場所に shaders/ と res/ を置く
#   <prefix>/sdl3_lua5_vulkan  <prefix>/shaders/*.spv  <prefix>/res/...
# (macOSの.appにするときは、実行ファイルをContents/MacOS、shaders/とres/をContents/Resourcesへ。SDL_GetBasePathがResourcesを返すので、探し方はそのまま)
# フィールドエディタ・変換ツールは開発用なので、インストールしない。res/shaders/ の GLSL(ソース)も、実行時には要らないので除く
install(TARGETS app_main RUNTIME DESTINATION .)
install(FILES ${SHADER_SPVS} DESTINATION shaders)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/res/ DESTINATION res
  PATTERN ".DS_Store" EXCLUDE
  PATTERN "shaders" EXCLUDE)
