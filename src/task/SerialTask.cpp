#include "task/SerialTask.h"
#include "task/TaskPool.h"

TaskPool<SerialTask> SerialTask::pool_;

SerialTask::SerialTask()
	: Task()
	, std::enable_shared_from_this<SerialTask>()
	, listTask_()
	, iterator_(listTask_.end())
{
}

SerialTask::~SerialTask()
{
}

bool SerialTask::compute(uint32_t tick)
{
	if (listTask_.empty()) {
		return true;
	}

	if ((*iterator_)->compute(tick)) {
		++iterator_;
		if (iterator_ != listTask_.end()) {
			(*iterator_)->reset(tick);
		}
	}
	return isFinished();
}

void SerialTask::stop(bool isFinishAction)
{
	std::for_each(listTask_.begin(), listTask_.end(), [isFinishAction](std::shared_ptr<Task> task){
		task->stop(isFinishAction);
	});
	iterator_ = listTask_.end();
}

bool SerialTask::isFinished()
{
	return iterator_ == listTask_.end();
}

void SerialTask::reset(uint32_t tick)
{
	iterator_ = listTask_.begin();
	if (iterator_ != listTask_.end()) {
		(*iterator_)->reset(tick);
	}
}

void SerialTask::collect()
{
	std::for_each(listTask_.begin(), listTask_.end(), [](std::shared_ptr<Task> task){
		task->collect();
	});
	listTask_.clear();
	pool_.collect(shared_from_this());
}

void SerialTask::addTask(std::shared_ptr<Task> task)
{
	listTask_.push_back(task);
	iterator_ = listTask_.begin();
}

