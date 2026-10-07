#include "task/LoopTask.h"
#include "task/TaskPool.h"

TaskPool<LoopTask> LoopTask::pool_;

LoopTask::LoopTask()
	: Task()
	, std::enable_shared_from_this<LoopTask>()
	, task_()
	, counter_(0)
	, initCounter_(0)
{
}

LoopTask::~LoopTask()
{
}

bool LoopTask::compute(uint32_t tick)
{
	// 完了済み(counter_ == 0)なら何もしない。そのまま進むと--counter_が-1
	// (INFINIT)になり、完了後のcompute()でループが再開してしまう
	if (counter_ == 0) {
		return true;
	}
	bool result = false;
	if (task_) {
		if (task_->compute(tick)) {
			if (counter_ == INFINIT || --counter_ > 0) {
				task_->reset(tick);
			}
			else {
				result = true;
			}
		}
	}
	else {
		counter_ = 0;
		result = true;
	}
	return result;
}

void LoopTask::stop(bool isFinishAction)
{
	if (task_) {
		task_->stop(isFinishAction);
	}
	counter_ = 0;
}

bool LoopTask::isFinished()
{
	return counter_ == 0;
}

void LoopTask::reset(uint32_t tick)
{
	if (task_) {
		task_->reset(tick);
	}
	counter_ = initCounter_;
}

void LoopTask::collect()
{
	if (task_) {
		task_->collect();
		task_.reset();
	}
	pool_.collect(shared_from_this());
}

void LoopTask::setTask(std::shared_ptr<Task> task)
{
	task_ = task;
}

void LoopTask::setLoopCount(int counter)
{
	counter_ = initCounter_ = counter;
}
