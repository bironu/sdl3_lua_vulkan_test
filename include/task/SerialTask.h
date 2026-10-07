#ifndef TASK_SERIALTASK_H_
#define TASK_SERIALTASK_H_

#include "Task.h"
#include "TaskPool.h"
#include <list>
#include <memory>

class SerialTask final : public Task, public std::enable_shared_from_this<SerialTask>
{
public:
	SerialTask();
	virtual ~SerialTask() override;

	static std::shared_ptr<SerialTask> get() {
		return pool_.get();
	}

	virtual bool compute(uint32_t) override;
	virtual void stop(bool) override;
	virtual bool isFinished() override;
	virtual void reset(uint32_t) override;
	virtual void collect() override;

	void addTask(std::shared_ptr<Task>);

private:
	static TaskPool<SerialTask> pool_;
	std::list<std::shared_ptr<Task>> listTask_;
	std::list<std::shared_ptr<Task>>::iterator iterator_;
};

#endif // TASK_SERIALTASK_H_
