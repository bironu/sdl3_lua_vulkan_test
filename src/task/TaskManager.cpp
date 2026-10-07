#include "task/TaskManager.h"
#include "task/Task.h"
#include "task/TaskPool.h"

TaskManager::TaskManager()
	: mapTask_()
{
}

TaskManager::~TaskManager()
{
	clear();
}

void TaskManager::registerTask(int id, std::shared_ptr<Task> task)
{
	mapTask_.emplace(id, task);
}

void TaskManager::unregisterTask(int id, bool isFinishAction)
{
	auto ret = mapTask_.equal_range(id);
	std::for_each(ret.first, ret.second, [isFinishAction](decltype(mapTask_)::value_type &p){
		auto task = p.second;
		task->stop(isFinishAction);
		task->collect();
	});
	mapTask_.erase(id);
}

bool TaskManager::compute(uint32_t tick)
{
	bool result = true;
	for(auto i = mapTask_.begin(); i != mapTask_.end();) {
		auto task = i->second;
		// アニメーション終了時
		if (task->compute(tick)) {
			task->collect();
			i = mapTask_.erase(i);
		}
		// アニメーション継続時
		else {
			result = false;
			++i;
		}
	}
	return result;
}

void TaskManager::clear()
{
	std::for_each(mapTask_.begin(), mapTask_.end(), [](decltype(mapTask_)::value_type &p){
		auto task = p.second;
		task->stop(true);
		task->collect();
	});
	mapTask_.clear();
}
