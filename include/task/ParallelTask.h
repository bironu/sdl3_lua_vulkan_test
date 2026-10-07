#ifndef TASK_PARALLELTASK_H_
#define TASK_PARALLELTASK_H_

#include "Task.h"
#include "TaskPool.h"
#include <list>
#include <memory>

class ParallelTask final : public Task, public std::enable_shared_from_this<ParallelTask>
{
public:
	ParallelTask();
	virtual ~ParallelTask() override;

	static std::shared_ptr<ParallelTask> get() {
		return pool_.get();
	}

	virtual bool compute(uint32_t) override;
	virtual void stop(bool) override;
	virtual bool isFinished() override;
	virtual void reset(uint32_t) override;
	virtual void collect() override;

	void addTask(std::shared_ptr<Task>);

private:
	static TaskPool<ParallelTask> pool_;
	std::list<std::shared_ptr<Task>> listTask_;
};

#endif // TASK_PARALLELTASK_H_
