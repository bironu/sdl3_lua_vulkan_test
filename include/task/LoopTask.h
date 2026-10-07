#ifndef TASK_LOOPTASK_H_
#define TASK_LOOPTASK_H_

#include "Task.h"
#include "TaskPool.h"
#include <memory>

class LoopTask final : public Task, public std::enable_shared_from_this<LoopTask>
{
public:
	LoopTask();
	virtual ~LoopTask() override;

	static std::shared_ptr<LoopTask> get() {
		return pool_.get();
	}

	virtual bool compute(uint32_t) override;
	virtual void stop(bool) override;
	virtual bool isFinished() override;
	virtual void reset(uint32_t) override;
	virtual void collect() override;

	void setTask(std::shared_ptr<Task>);
	void setLoopCount(int);

	static const int INFINIT = -1;

private:
	static TaskPool<LoopTask> pool_;
	std::shared_ptr<Task> task_;
	int counter_;
	int initCounter_;
};

#endif // TASK_LOOPTASK_H_
