#include "field/PropCatalog.h"
#include "resources/ResourcePaths.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace field
{

namespace
{
namespace fs = std::filesystem;

bool isModelFile(const fs::path &path)
{
	std::string extension = path.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
	return extension == ".glb" || extension == ".vrm";
}
}

std::vector<std::string> listProps()
{
	std::vector<std::string> result;
	std::error_code error;
	for(const auto &entry : fs::directory_iterator(ResourcePaths::resource(kPropDir), error)){
		if(entry.is_regular_file() && isModelFile(entry.path())){
			result.push_back(entry.path().filename().string());
		}
	}
	std::sort(result.begin(), result.end());
	return result;
}

std::string propPath(const std::string &name)
{
	return std::string(kPropDir) + "/" + name;
}

bool importProp(const std::string &sourcePath, std::string &name)
{
	const fs::path source(sourcePath);
	if(!isModelFile(source)){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "prop: not a .glb/.vrm file: %s", sourcePath.c_str());
		return false;
	}
#if defined(APP_RESOURCE_ROOT)
	const fs::path directory = fs::path(APP_RESOURCE_ROOT) / kPropDir;
#else
	const fs::path directory = ResourcePaths::resource(kPropDir);
#endif
	std::error_code error;
	fs::create_directories(directory, error);
	const fs::path destination = directory / source.filename();
	if(!fs::equivalent(source, destination, error)){
		error.clear();
		fs::copy_file(source, destination, fs::copy_options::overwrite_existing, error);
		if(error){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "prop: copy failed: %s", error.message().c_str());
			return false;
		}
	}
	name = source.filename().string();
	return true;
}

} // namespace field
