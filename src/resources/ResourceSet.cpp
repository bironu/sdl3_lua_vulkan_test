#include "resources/ResourceSet.h"
#include "resources/ResourcePaths.h"
#include "resources/Resources.h"
#include "model/ModelData.h"
#include <sol/sol.hpp>
#include <SDL3/SDL_log.h>

ResourceSet::ResourceSet(const Resources &resources)
	: resources_(resources)
{
}

ResourceSet::~ResourceSet()
{
	release();
}

namespace
{
template<typename T>
std::shared_ptr<T> hold(std::vector<std::shared_ptr<const void>> &held, std::shared_ptr<T> item)
{
	if(item){
		held.push_back(item);
	}
	return item;
}
}

std::shared_ptr<SDL_::Image> ResourceSet::image(const std::string &path)
{
	return hold(held_, resources_.loadImage(path));
}

std::shared_ptr<const model::ModelData> ResourceSet::model(const std::string &path)
{
	return hold(held_, resources_.loadModel(path));
}

std::shared_ptr<const model::ModelData> ResourceSet::modelWithImages(const std::string &path)
{
	const auto data = model(path);
	if(!data){
		return nullptr;
	}
	// VulkanModel::createが、材質ごとに、(基本色のテクスチャ=材質のignoreTextureAlpha、影の色・発光のテクスチャ=アルファ無視)で使う組み合わせ
	for(const auto &material : data->materials){
		if(material.texture >= 0){
			modelImage(path, static_cast<size_t>(material.texture), material.ignoreTextureAlpha);
		}
		if(material.mtoon.shadeTexture >= 0){
			modelImage(path, static_cast<size_t>(material.mtoon.shadeTexture), true);
		}
		if(material.mtoon.emissiveTexture >= 0){
			modelImage(path, static_cast<size_t>(material.mtoon.emissiveTexture), true);
		}
	}
	return data;
}

std::shared_ptr<SDL_::Image> ResourceSet::modelImage(const std::string &modelPath, size_t index, bool ignoreAlpha)
{
	return hold(held_, resources_.loadModelImage(modelPath, index, ignoreAlpha));
}

std::shared_ptr<const model::HumanoidAnimation> ResourceSet::animation(const std::string &path)
{
	return hold(held_, resources_.loadVrma(path));
}

std::shared_ptr<const model::Motion> ResourceSet::motion(const std::string &path)
{
	return hold(held_, resources_.loadMotion(path));
}

std::shared_ptr<SDL_::Mix_::Audio> ResourceSet::sound(const std::string &path)
{
	return hold(held_, resources_.loadSound(path));
}

std::shared_ptr<SDL_::Mix_::Audio> ResourceSet::music(const std::string &path)
{
	return hold(held_, resources_.loadMusic(path));
}

size_t ResourceSet::loadManifest(const std::string &path)
{
	if(!resources_.exists(path)){
		return 0;
	}
	sol::state lua;
	lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
	const auto result = lua.safe_script_file(ResourcePaths::resource(path.c_str()), sol::script_pass_on_error);
	if(!result.valid()){
		const sol::error error = result;
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "manifest %s: script error: %s", path.c_str(), error.what());
		return 0;
	}
	const sol::optional<sol::table> assets = lua["assets"];
	if(!assets){
		return 0;
	}
	size_t loaded = 0;
	const auto loadAll = [&](const char *key, const char *kind, const auto &loader){
		const sol::optional<sol::table> list = (*assets)[key];
		if(!list){
			return;
		}
		for(size_t i = 1; i <= list->size(); ++i){
			const sol::optional<std::string> item = (*list)[i];
			if(!item){
				continue;
			}
			if(loader(*item)){
				++loaded;
			}
			else{
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "manifest %s: failed to load %s: %s", path.c_str(), kind, item->c_str());
			}
		}
	};
	loadAll("images", "image", [this](const std::string &p){ return image(p) != nullptr; });
	loadAll("models", "model", [this](const std::string &p){ return modelWithImages(p) != nullptr; });
	loadAll("animations", "animation", [this](const std::string &p){ return animation(p) != nullptr; });
	loadAll("motions", "motion", [this](const std::string &p){ return motion(p) != nullptr; });
	loadAll("sounds", "sound", [this](const std::string &p){ return sound(p) != nullptr; });
	loadAll("music", "music", [this](const std::string &p){ return music(p) != nullptr; });
	return loaded;
}

void ResourceSet::release()
{
	if(!held_.empty()){
		resources_.retain(std::move(held_));
		held_.clear();
	}
}
