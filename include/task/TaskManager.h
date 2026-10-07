#ifndef TASK_TASKMANAGER_H_
#define TASK_TASKMANAGER_H_

#include "misc/Uncopyable.h"
#include <memory>
#include <unordered_map>

class Task;
class TaskManager final
{
public:
	UNCOPYABLE(TaskManager);
	explicit TaskManager();
	~TaskManager();

	void registerTask(int, std::shared_ptr<Task>);
	void unregisterTask(int, bool);
	bool compute(uint32_t);
	void clear();

private:
	std::unordered_multimap<int, std::shared_ptr<Task>> mapTask_;
};

#endif // TASK_TASKMANAGER_H_
