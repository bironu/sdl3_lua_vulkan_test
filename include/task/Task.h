#ifndef TASK_TASK_H_
#define TASK_TASK_H_

#include "misc/Uncopyable.h"
#include <cstdint>

class Task
{
public:
	UNCOPYABLE(Task);
	Task() = default;
	virtual ~Task() = default;

	virtual bool compute(uint32_t) = 0; // Task終了でtrueを返す
	virtual void stop(bool) = 0;
	virtual bool isFinished() = 0;
	virtual void reset(uint32_t) = 0;
	virtual void collect() = 0;
};

#endif // TASK_TASK_H_
