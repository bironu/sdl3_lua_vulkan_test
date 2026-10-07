#include "resources/Resources.h"
#include "resources/ResourcePaths.h"
#include "model/FbxLoader.h"
#include "model/GltfLoader.h"
#include "model/PmxLoader.h"
#include "model/VmdLoader.h"
#include "model/Vrma.h"
#include "sdl/SDLImage.h"
#include "sdl/SDLGamepad.h"
#include "sdl/SDLJoystick.h"
#include "sdl/SDLMixAudio.h"
#include "sdl/SDLMixMixer.h"
#include <SDL3/SDL_events.h>
#include <sol/sol.hpp>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

Resources::Resources()
	: windowWidth_(1920/2)
	, windowHeight_(1080/2)
	, screenWidth_(1920)
	, screenHeight_(1080)
	, mapJoystick_()
{
	reload();
}

Resources::~Resources()
{
	collect();
	reportLeaks();
}

void Resources::retain(std::vector<std::shared_ptr<const void>> &&released) const
{
	std::lock_guard<std::mutex> lock(retainedMutex_);
	for(auto &item : released){
		retained_.push_back(std::move(item));
	}
}

void Resources::collect() const
{
	std::vector<std::shared_ptr<const void>> released;
	{
		std::lock_guard<std::mutex> lock(retainedMutex_);
		released.swap(retained_);
	}
	released.clear(); // ここで、他に持つ人のいないデータが解放される(ロックの外で)
	images_.purgeExpired();
	models_.purgeExpired();
	animations_.purgeExpired();
	motions_.purgeExpired();
	sounds_.purgeExpired();
	musics_.purgeExpired();
}

void Resources::reportLeaks() const
{
	const auto report = [](const char *kind, const auto &cache){
		for(const auto &entry : cache.aliveEntries()){
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Resource still referenced at exit: [%s] %s (%ld)", kind, entry.first.c_str(), entry.second);
		}
		return cache.aliveEntries().size();
	};
	size_t leaked = 0;
	leaked += report("image", images_);
	leaked += report("model", models_);
	leaked += report("animation", animations_);
	leaked += report("motion", motions_);
	leaked += report("sound", sounds_);
	leaked += report("music", musics_);
	SDL_Log("Resource cache: images %zu hit/%zu load, models %zu/%zu, animations %zu/%zu, motions %zu/%zu, sounds %zu/%zu, music %zu/%zu; %zu still referenced",
		images_.hits(), images_.misses(), models_.hits(), models_.misses(), animations_.hits(), animations_.misses(),
		motions_.hits(), motions_.misses(), sounds_.hits(), sounds_.misses(), musics_.hits(), musics_.misses(), leaked);
}

namespace
{
// 言語ファイル(lang/<lang>.lua)を読んで、文字列とフォントを、上書きで足す。ファイルが無い/読めなければfalse
bool readLanguageFile(const std::string &lang, std::unordered_map<std::string, std::string> &strings, std::map<std::string, FontInfo> &fonts)
{
	sol::state lua;
	lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
	const std::string path = ResourcePaths::resource(("res/lua/lang/" + lang + ".lua").c_str());
	const auto loaded = lua.safe_script_file(path, sol::script_pass_on_error);
	if (!loaded.valid()) {
		const sol::error error = loaded;
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Lua error (%s): %s", path.c_str(), error.what());
		return false;
	}
	if (const sol::optional<sol::table> table = lua["strings"]) {
		for (const auto &entry : *table) {
			if (entry.first.is<std::string>() && entry.second.is<std::string>()) {
				strings[entry.first.as<std::string>()] = entry.second.as<std::string>();
			}
		}
	}
	if (const sol::optional<sol::table> table = lua["fonts"]) {
		for (const auto &entry : *table) {
			if (!entry.first.is<std::string>() || !entry.second.is<sol::table>()) {
				continue;
			}
			const sol::table definition = entry.second.as<sol::table>();
			const auto file = definition.get<sol::optional<std::string>>("file");
			if (!file) {
				continue;
			}
			FontInfo info;
			info.file = *file;
			info.size = definition.get_or("size", info.size);
			fonts[entry.first.as<std::string>()] = info;
		}
	}
	// 旧形式(font_name): fontsにdefaultが無ければ、既定のフォントとして使う
	if (fonts.find("default") == fonts.end()) {
		const std::string legacy = lua.get_or<std::string>("font_name", "");
		if (!legacy.empty()) {
			fonts["default"].file = legacy;
		}
	}
	return true;
}
}

void Resources::loadLanguage()
{
	strings_.clear();
	fonts_.clear();
	// 現在の言語に無いキー・フォントは、フォールバックの言語で補う(先にフォールバック、後に現在の言語で上書き)
	if (!fallbackLanguage_.empty() && fallbackLanguage_ != language_) {
		readLanguageFile(fallbackLanguage_, strings_, fonts_);
	}
	if (!readLanguageFile(language_, strings_, fonts_)) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Language file load error: %s", language_.c_str());
	}
	const auto defaultFont = fonts_.find("default");
	luaFontName_ = defaultFont != fonts_.end() ? defaultFont->second.file : std::string();
	{
		std::lock_guard<std::mutex> lock(warnedMutex_);
		warnedKeys_.clear();
	}
	++languageVersion_;
}

bool Resources::setLanguage(const std::string &name)
{
	if (!exists("res/lua/lang/" + name + ".lua")) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Unknown language: %s", name.c_str());
		return false;
	}
	language_ = name;
	loadLanguage();
	return true;
}

std::vector<std::string> Resources::availableLanguages() const
{
	std::vector<std::string> result;
	for (const auto &path : listFiles("res/lua/lang", "*.lua")) {
		const auto slash = path.find_last_of('/');
		const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
		result.push_back(file.substr(0, file.size() - 4)); // ".lua"
	}
	return result;
}

std::string Resources::translate(const std::string &key, const std::map<std::string, std::string> &vars) const
{
	const auto found = strings_.find(key);
	if (found == strings_.end()) {
		bool first;
		{
			std::lock_guard<std::mutex> lock(warnedMutex_);
			first = warnedKeys_.insert(key).second;
		}
		if (first) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "String key not found (language %s): %s", language_.c_str(), key.c_str());
		}
		return key;
	}
	std::string text = found->second;
	for (const auto &var : vars) {
		const std::string placeholder = "{" + var.first + "}";
		for (size_t pos = text.find(placeholder); pos != std::string::npos; pos = text.find(placeholder, pos + var.second.size())) {
			text.replace(pos, placeholder.size(), var.second);
		}
	}
	return text;
}

FontInfo Resources::getFont(const std::string &name) const
{
	auto found = fonts_.find(name);
	if (found == fonts_.end()) {
		found = fonts_.find("default");
	}
	return found != fonts_.end() ? found->second : FontInfo{};
}

void Resources::reload()
{
	// 1. 言語設定(system.lua) → 2. その言語のファイル(loadLanguage)
	sol::state lua;
	lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
	const std::string path = ResourcePaths::resource("res/lua/system.lua");
	const auto loaded = lua.safe_script_file(path, sol::script_pass_on_error);
	if (!loaded.valid()) {
		const sol::error error = loaded;
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Lua error (%s): %s", path.c_str(), error.what());
	}
	const sol::optional<sol::table> system = lua["system"];
	if (language_.empty()) {
		language_ = system ? system->get_or<std::string>("lang", "japanese") : "japanese";
	}
	fallbackLanguage_ = system ? system->get_or<std::string>("fallback_lang", "english") : "english";
	soundFont_ = system ? system->get_or<std::string>("soundfont", "") : "";
	loadLanguage();
}

std::string Resources::getString(const std::string &id) const
{
	const auto i = strings_.find(id);
	return i != strings_.end() ? i->second : std::string();
}

std::shared_ptr<SDL_::Image> Resources::loadImage(const std::string &path) const
{
	return images_.acquire(path, [&]() -> std::shared_ptr<SDL_::Image> {
		const std::string fullPath = ResourcePaths::resource(path.c_str());
		auto image = std::make_shared<SDL_::Image>(fullPath.c_str());
		if (!image->isEnabled()) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Image load error. %s (%s)", SDL_GetError(), fullPath.c_str());
			return nullptr;
		}
		return image;
	});
}

std::shared_ptr<SDL_::Image> Resources::loadImageFromMemory(const std::vector<uint8_t> &bytes, bool ignoreAlpha) const
{
	auto image = SDL_::Image::fromMemory(bytes.data(), bytes.size(), ignoreAlpha);
	if (!image) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Embedded image load error. %s", SDL_GetError());
	}
	return image;
}

std::shared_ptr<const model::ModelData> Resources::loadModel(const std::string &path) const
{
	return models_.acquire(path, [&]() -> std::shared_ptr<const model::ModelData> {
		// 拡張子でPMX(MMD)かVRM(glTF)かを決める
		const auto dot = path.find_last_of('.');
		std::string extension = dot == std::string::npos ? std::string() : path.substr(dot + 1);
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
		if(extension == "vrm" || extension == "glb"){
			return model::loadVrm(ResourcePaths::resource(path.c_str()));
		}
		const auto slash = path.find_last_of('/');
		const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash);
		return model::loadPmx(ResourcePaths::resource(path.c_str()), dir);
	});
}

std::vector<std::string> Resources::listFiles(const std::string &dir, const std::string &pattern) const
{
	std::vector<std::string> result;
	int count = 0;
	char **names = ::SDL_GlobDirectory(ResourcePaths::resource(dir.c_str()).c_str(), pattern.c_str(), SDL_GLOB_CASEINSENSITIVE, &count);
	if (!names) {
		return result;
	}
	for (int i = 0; i < count; ++i) {
		result.push_back(dir + "/" + names[i]);
	}
	::SDL_free(names);
	std::sort(result.begin(), result.end());
	return result;
}

bool Resources::exists(const std::string &path) const
{
	return ::SDL_GetPathInfo(ResourcePaths::resource(path.c_str()).c_str(), nullptr);
}

std::shared_ptr<const model::Motion> Resources::loadMotion(const std::string &path) const
{
	return motions_.acquire(path, [&]() -> std::shared_ptr<const model::Motion> {
		return model::loadVmd(ResourcePaths::resource(path.c_str()));
	});
}

std::shared_ptr<const model::HumanoidAnimation> Resources::loadVrma(const std::string &path) const
{
	return animations_.acquire(path, [&]() -> std::shared_ptr<const model::HumanoidAnimation> {
		// 拡張子で、VRMAかFBX(Mixamoなど)かを決める
		const auto dot = path.find_last_of('.');
		std::string extension = dot == std::string::npos ? std::string() : path.substr(dot + 1);
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
		if(extension == "fbx"){
			return model::loadFbxAnimation(ResourcePaths::resource(path.c_str()));
		}
		return model::loadVrma(ResourcePaths::resource(path.c_str()));
	});
}

std::shared_ptr<SDL_::Mix_::Audio> Resources::loadSound(const std::string &path) const
{
	return sounds_.acquire(path, [&]() -> std::shared_ptr<SDL_::Mix_::Audio> {
		if(!mixer_){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Sound load error: no mixer. (%s)", path.c_str());
			return nullptr;
		}
		auto audio = std::make_shared<SDL_::Mix_::Audio>(*mixer_, ResourcePaths::resource(path.c_str()).c_str(), false);
		if(!audio->get()){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Sound load error. %s (%s)", SDL_GetError(), path.c_str());
			return nullptr;
		}
		return audio;
	});
}

std::shared_ptr<SDL_::Mix_::Audio> Resources::loadMusic(const std::string &path) const
{
	return musics_.acquire(path, [&]() -> std::shared_ptr<SDL_::Mix_::Audio> {
		if(!mixer_){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Music load error: no mixer. (%s)", path.c_str());
			return nullptr;
		}
		auto audio = std::make_shared<SDL_::Mix_::Audio>(*mixer_, ResourcePaths::resource(path.c_str()).c_str(), true);
		if(!audio->get()){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Music load error. %s (%s)", SDL_GetError(), path.c_str());
			return nullptr;
		}
		return audio;
	});
}

std::shared_ptr<SDL_::Image> Resources::loadModelImage(const std::string &modelPath, size_t index, bool ignoreAlpha) const
{
	const std::string key = "model:" + modelPath + "#" + std::to_string(index) + (ignoreAlpha ? "#opaque" : "");
	return images_.acquire(key, [&]() -> std::shared_ptr<SDL_::Image> {
		const auto data = loadModel(modelPath);
		if(!data || index >= data->embeddedImages.size() || data->embeddedImages[index].empty()){
			return nullptr;
		}
		return loadImageFromMemory(data->embeddedImages[index], ignoreAlpha);
	});
}

std::string Resources::getFontFileName() const
{
//	return (*luaString_)["font_name"].get<const char *>();
	return ResourcePaths::join(ResourcePaths::kFontDir, "ipag.ttf");
}

void Resources::setMixer(SDL_::Mix_::Mixer *mixer)
{
	mixer_ = mixer;
	if (mixer_ && !soundFont_.empty()) {
		const std::string path = ResourcePaths::resource(soundFont_.c_str());
		if (::SDL_GetPathInfo(path.c_str(), nullptr)) {
			mixer_->setSoundFonts(path.c_str());
		}
		else {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SoundFont not found (MIDI uses the default): %s", path.c_str());
		}
	}
}

void Resources::addGamepad(uint32_t instanceId)
{
	if (gamepad_) {
		return; // すでに1台使っている
	}
	auto pad = std::make_shared<SDL_::Gamepad>(instanceId);
	if (pad->isOpen()) {
		gamepad_ = std::move(pad);
	}
}

void Resources::removeGamepad(uint32_t instanceId)
{
	if (gamepad_ && gamepad_->instanceId() == instanceId) {
		gamepad_.reset();
		// 他に接続されているものがあれば、それを使う
		int count = 0;
		if (SDL_JoystickID *ids = ::SDL_GetGamepads(&count)) {
			for (int i = 0; i < count && !gamepad_; ++i) {
				if (ids[i] != instanceId) {
					addGamepad(ids[i]);
				}
			}
			::SDL_free(ids);
		}
	}
}

std::shared_ptr<SDL_::Gamepad> Resources::getGamepad() const
{
	return gamepad_;
}

void Resources::addJoyDevice(const SDL_JoyDeviceEvent &jdevice)
{
	mapJoystick_.insert(std::make_pair(jdevice.which, std::make_shared<SDL_::Joystick>(jdevice.which)));
}

void Resources::removeJoyDevice(const SDL_JoyDeviceEvent &jdevice)
{
	for (auto &pair : mapJoystick_) {
		if(pair.second->getInstanceID() == jdevice.which) {
			mapJoystick_.erase(pair.first);
			break;
		}
	}
}

std::shared_ptr<SDL_::Joystick> Resources::getJoystick(uint32_t index) const
{
	auto i = mapJoystick_.find(index);
	if (i != mapJoystick_.end()) {
		return i->second;
	}
	else {
		return nullptr;
	}
}

std::shared_ptr<SDL_::Joystick> Resources::getFirstJoystick() const
{
	for (const auto &pair : mapJoystick_) {
		if (pair.second && pair.second->isJoystick()) {
			return pair.second;
		}
	}
	return nullptr;
}
