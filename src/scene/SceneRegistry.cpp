#include "scene/SceneRegistry.h"
#include <map>

namespace
{
std::map<std::string, SceneRegistry::Creator> &table()
{
	static std::map<std::string, SceneRegistry::Creator> creators;
	return creators;
}
}

void SceneRegistry::add(const std::string &name, Creator creator)
{
	table()[name] = std::move(creator);
}

std::shared_ptr<Scene> SceneRegistry::create(const std::string &name)
{
	const auto found = table().find(name);
	return found != table().end() ? found->second() : nullptr;
}
