#ifndef RESOURCES_H_
#define RESOURCES_H_

#include "misc/Uncopyable.h"
#include "resources/AssetCache.h"
#include <cstdint>
#include <memory>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <string>
#include <vector>


namespace SDL_
{
class Image;
class Joystick;
class Gamepad;
namespace Mix_
{
class Audio;
class Mixer;
}
}

namespace model
{
struct ModelData;
struct HumanoidAnimation;
struct Motion;
}

struct SDL_JoyDeviceEvent;

// フォントの定義(lang/*.luaのfontsテーブル)。fileはリポジトリ直下からの相対パス、sizeは既定の大きさ(ピクセル)
struct FontInfo
{
	std::string file;
	float size = 48.0f;
};

class Resources final
{
public:
	UNCOPYABLE(Resources);
	Resources();
	~Resources();

	void setWindowWidth(int width) { windowWidth_ = width; }
	void setWindowHeight(int height) { windowHeight_ = height; }
	int getWindowWidth() const { return windowWidth_; }
	int getWindowHeight() const { return windowHeight_; }
	int getScreenWidth() const { return screenWidth_; }
	int getScreenHeight() const { return screenHeight_; }

	// Luaスクリプト(res/lua)から読み込んだデータ。reload()で読み直せる。
	// system.luaの言語設定に応じた言語ファイルの文字列・フォント、メニュー項目、画像パス
	void reload();
	// 言語ファイルの既定のフォント(fonts.default.file。リポジトリ直下からの相対パス)
	const std::string &getLuaFontName() const { return luaFontName_; }
	// 言語ファイルのstringsテーブルの文字列。無ければ空文字列
	std::string getString(const std::string &id) const;

	// ---- 多言語 ----
	// 文字列とフォントは、言語ごとのLua(res/lua/lang/<言語>.lua)にある。キー文字列で引く(enumは無い):
	//   strings = { Key = "文字列 {name}" }                  {name}は、translateのvarsで差し込む
	//   fonts = { default = { file = "res/font/x.ttf", size = 48 }, title = { ... } }
	// 既定の言語はsystem.luaのsystem.lang、system.fallback_lang(既定english)の言語は、現在の言語に無いキー・フォントの代わりに使う
	const std::string &language() const { return language_; }
	// 言語を切り替える(lang/<name>.luaが無ければfalse)。languageVersion()が増えるので、文字を描くもの(TextWidget)は自動で更新される
	bool setLanguage(const std::string &name);
	// 選べる言語(lang/*.luaのファイル名から拡張子を除いたもの。名前順)
	std::vector<std::string> availableLanguages() const;
	// 言語・文字列の定義が読み込み直されるたびに増える(文字列の更新の検出用)
	uint32_t languageVersion() const { return languageVersion_; }
	// キーの文字列を、現在の言語で。{名前}をvarsで置き換える。キーが無ければ、キーそのものを返して(1回だけ)ログに出す
	std::string translate(const std::string &key, const std::map<std::string, std::string> &vars = {}) const;
	// 名前のフォントの定義。無ければ"default"。それも無ければfileが空
	FontInfo getFont(const std::string &name = "default") const;

	// ---- データの読み込みとキャッシュ ----
	// 読んだデータは、パスをキーに共有される(AssetCache)。同じパスをもう一度読むと、読み込み済みのものが返る。
	// 持っている人(shared_ptr)が全員手放すと、自動で解放される。手動のunloadは無い。
	// シーンは、onCreateでResourceSet(Scene::resources())経由で読んで持っておけば、シーンの終わりで自動的に手放す(resources/ResourceSet.h)。
	// 共有されるので、返されたデータを書き換えないこと。別スレッドから呼んでよい(GPUの資源は作らない)。失敗したらログを出してnullptr

	// 画像ファイル。pathはリポジトリ直下からの相対パス(例: "res/image/tex1.bmp")
	std::shared_ptr<SDL_::Image> loadImage(const std::string &path) const;
	// モデルに埋め込まれた画像(VRM)をデコードしたもの。modelPathのモデルのembeddedImages[index]。ignoreAlphaなら不透明にする。
	// (モデル+番号+アルファの扱い)をキーに共有される。モデルが読めない/番号が範囲外/空ならnullptr
	std::shared_ptr<SDL_::Image> loadModelImage(const std::string &modelPath, size_t index, bool ignoreAlpha) const;

	// メモリ上の画像(モデルファイルに埋め込まれたPNG/JPEG等)を読む。失敗したらログを出してnullptr。ignoreAlphaなら不透明にする
	std::shared_ptr<SDL_::Image> loadImageFromMemory(const std::vector<uint8_t> &bytes, bool ignoreAlpha = false) const;

	// モデルファイル(PMXまたはVRM。拡張子で決める)を読む。pathはリポジトリ直下からの相対パス(例: "res/model/xxx/model.pmx")。
	// テクスチャは、PMXはModelData::texturePathsにパスだけが入り(読むのはloadImage)、VRMはModelData::embeddedImagesにバイト列が入る(読むのはloadImageFromMemory)。失敗したらnullptr
	std::shared_ptr<const model::ModelData> loadModel(const std::string &path) const;
	// リポジトリ直下からの相対パスのディレクトリにある、パターン(例: "*.vrm"。大文字小文字は区別しない)に合うファイルを、
	// リポジトリ直下からの相対パス(dir + "/" + 名前)で、名前順に返す。ディレクトリが無ければ空
	std::vector<std::string> listFiles(const std::string &dir, const std::string &pattern) const;
	// リポジトリ直下からの相対パスのファイルがあるか
	bool exists(const std::string &path) const;
	// モーション(VMD)を読む。pathはリポジトリ直下からの相対パス。失敗したらnullptr
	std::shared_ptr<const model::Motion> loadMotion(const std::string &path) const;

	// VRMA(VRMのアニメーション)を読む。拡張子が.fbxなら、FBX(Mixamoなど)のアニメーションを、同じ形に直して読む。
	// pathはリポジトリ直下からの相対パス。失敗したらnullptr
	std::shared_ptr<const model::HumanoidAnimation> loadVrma(const std::string &path) const;

	// 効果音(SE)・BGM。ミキサー(setMixer)が要る。デコード前の音声データをRAIIのAudioで返す(生のMIX_Audioは使わない)。
	// BGMはMIDI(SoundFont)用の読み方になる。同じパスでも、効果音とBGMは別のキャッシュ
	std::shared_ptr<SDL_::Mix_::Audio> loadSound(const std::string &path) const;
	std::shared_ptr<SDL_::Mix_::Audio> loadMusic(const std::string &path) const;
	// 音声の読み込みに使うミキサー(Applicationのもの)。アプリの開始時に設定する。Resourcesより長生きすること
	// ミキサーを渡す。system.luaのSoundFontがあれば、ミキサーに設定する
	void setMixer(SDL_::Mix_::Mixer *mixer);

	// ---- シーンの切り替えの橋渡しと、終了時の点検 ----
	// 手放されたデータを、次のシーンのonCreateが終わるまで、少しの間だけ持っておく(古いシーンの破棄 → 新しいシーンの読み込みの間に、
	// 同じデータを読み直さないため)。ResourceSet::release()が呼ぶ
	void retain(std::vector<std::shared_ptr<const void>> &&released) const;
	// retainで持っているものを手放す。SceneHostが、新しいシーンのonCreateの後に呼ぶ
	void collect() const;
	// まだ持たれているデータを、ログに出す(アプリ終了時。シーンが全部終わった後に残っていれば、手放し忘れの不具合)
	void reportLeaks() const;

	std::string getFontFileName() const;
	// MIDI(BGM)の音色のSoundFont(SF2)のパス(リポジトリ直下からの相対パス)。system.luaの system.soundfont。未設定なら空文字列
	const std::string &getSoundFontFileName() const { return soundFont_; }

	// ゲームパッド(標準配置のコントローラ)。接続されている最初の1台を使う。無ければnullptr。
	// ApplicationがSDL_EVENT_GAMEPAD_ADDED/REMOVEDで呼ぶ
	void addGamepad(uint32_t instanceId);
	void removeGamepad(uint32_t instanceId);
	std::shared_ptr<SDL_::Gamepad> getGamepad() const;

	void addJoyDevice(const SDL_JoyDeviceEvent &);
	void removeJoyDevice(const SDL_JoyDeviceEvent &);
	std::shared_ptr<SDL_::Joystick> getJoystick(uint32_t) const;
	// 接続されている最初のジョイスティック。無ければnullptr(SDL3のIDは0始まりではないため)
	std::shared_ptr<SDL_::Joystick> getFirstJoystick() const;

private:
	int windowWidth_;
	int windowHeight_;
	int screenWidth_;
	int screenHeight_;
	std::unordered_map<uint32_t, std::shared_ptr<SDL_::Joystick>> mapJoystick_;
	std::shared_ptr<SDL_::Gamepad> gamepad_;
	void loadLanguage();
	std::string language_;         // 現在の言語(setLanguageで切り替わる)
	std::string fallbackLanguage_; // system.luaのfallback_lang
	std::string soundFont_;        // system.luaのsoundfont
	uint32_t languageVersion_ = 0;
	std::map<std::string, FontInfo> fonts_;
	mutable std::mutex warnedMutex_;
	mutable std::set<std::string> warnedKeys_;
	std::string luaFontName_;
	std::unordered_map<std::string, std::string> strings_;

	mutable AssetCache<SDL_::Image> images_;
	mutable AssetCache<const model::ModelData> models_;
	mutable AssetCache<const model::HumanoidAnimation> animations_;
	mutable AssetCache<const model::Motion> motions_;
	mutable AssetCache<SDL_::Mix_::Audio> sounds_;
	mutable AssetCache<SDL_::Mix_::Audio> musics_;
	SDL_::Mix_::Mixer *mixer_ = nullptr;
	mutable std::mutex retainedMutex_;
	mutable std::vector<std::shared_ptr<const void>> retained_;
};

#endif // RESOURCES_H_
