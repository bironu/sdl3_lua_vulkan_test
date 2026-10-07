#include "task/ActionTask.h"
#include "task/TaskPool.h"

TaskPool<ActionTask> ActionTask::pool_;

ActionTask::ActionTask()
	: Task()
	, std::enable_shared_from_this<ActionTask>()
	, interpolator_()
	, finished_(true)
	, duration_()
	, currValue_(1.0)
	, startValue_()
	, stopValue_(1.0)
	, deltaValue_()
	, durationReciprocal_()
	, startTime_()
{
}

ActionTask::~ActionTask()
{
}

bool ActionTask::compute(uint32_t tick) {
	if (finished_) {
		return true;
	}

	const int timePassed = tick - startTime_;

	if (timePassed < duration_) {
		const double x = interpolator_(timePassed * durationReciprocal_);
		currValue_ = startValue_ + x * deltaValue_;
	}
	else {
		currValue_ = stopValue_;
		finished_ = true;
	}

	if (action_) {
		action_(currValue_);
	}
	return finished_;
}

void ActionTask::stop(bool isFinishAction)
{
	if (finished_) {
		return;
	}

	currValue_ = stopValue_;
	finished_ = true;
	if (isFinishAction && action_) {
		action_(currValue_);
	}
}

bool ActionTask::isFinished()
{
	return finished_;
}

void ActionTask::reset(uint32_t tick)
{
	startTime_ = tick;
	finished_ = false;
}

void ActionTask::collect()
{
	finished_ = true;
	action_ = nullptr;
	interpolator_ = nullptr;
	pool_.collect(shared_from_this());
}

void ActionTask::setAction(std::function<void(float)> action)
{
	action_ = action;
}


void ActionTask::setup(float startValue, float stopValue, uint32_t duration, uint32_t tick)
{
	finished_ = false;
	duration_ = duration;
	startTime_ = tick;
	startValue_ = startValue;
	stopValue_ = stopValue;
	deltaValue_ = stopValue - startValue;
	durationReciprocal_ = 1.0 / static_cast<double>(duration_);
}

void ActionTask::setInterpolator(std::function<float(float)> interpolator)
{
	interpolator_ = interpolator;
}
