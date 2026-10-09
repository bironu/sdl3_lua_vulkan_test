#if !defined(RESOURCE_PATHS_H_)
#define RESOURCE_PATHS_H_

#include <SDL3/SDL_filesystem.h>
#include <string>

// 実行ファイルからの相対パスで表したデータディレクトリ。
// 旧xanadu.hのIMAGE_DIR/LEVEL_DIR/USERS_DIRマクロとResources.cppの
// IMAGE_ROOT/AUDIO_ROOT/FONT_ROOTマクロを統一したもの
namespace ResourcePaths {
inline constexpr const char kImageDir[] = "../bmp";
inline constexpr const char kAudioDir[] = "../audio";
inline constexpr const char kFontDir[]  = "../font";
inline constexpr const char kLevelDir[] = "../map";
inline constexpr const char kUsersDir[] = "../users";
// 外部ファイル化したデータ(ranks.lua等)は、散らばらないようここへ集約する
inline constexpr const char kDataDir[]  = "../data";

// リポジトリ直下からの相対パス("res/image/xxx.png"等)を、実際のパスに解決する。
// 実行ファイルのあるディレクトリ基準のパス(配布時はinstallでresをコピーする)を先に探し、無ければ
// ビルド元のリポジトリ直下(APP_RESOURCE_ROOT。CMakeが設定)のパスを返す。開発中は、実行ファイルの横へresのコピーやリンクを置かなくてよい
inline std::string resource(const char *relative)
{
	const char *base = ::SDL_GetBasePath();
	std::string path = std::string(base ? base : "") + relative;
#if defined(APP_RESOURCE_ROOT)
	if (!::SDL_GetPathInfo(path.c_str(), nullptr)) {
		return std::string(APP_RESOURCE_ROOT) + relative;
	}
#endif
	return path;
}

// dirとrelativeを"/"で連結する(例: join(kImageDir, "xa1/field.bmp"))
inline std::string join(const char *dir, const char *relative)
{
	return std::string(dir) + "/" + relative;
}

// スクリプトパスから ".assets.lua" の名前を生成する(例: "res/lua/ui/hud.lua" -> "res/lua/ui/hud.assets.lua")
inline std::string assetsManifestFor(const std::string &script)
{
	std::string result = script;
	const auto dot = result.rfind(".lua");
	if(dot != std::string::npos){
		result.replace(dot, 4, ".assets.lua");
	}
	return result;
}
}

#endif // RESOURCE_PATHS_H_
