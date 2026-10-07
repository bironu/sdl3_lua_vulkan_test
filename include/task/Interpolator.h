#ifndef TASK_INTERPOLATOR_H_
#define TASK_INTERPOLATOR_H_

#include "misc/Uncopyable.h"
#include <functional>

enum class InterpolatorType
{
	AccelerateDecelerate = 0,
	Accelerate = 1,
	Anticipate = 2,
	AnticipateOvershoot = 3,
	Bounce = 4,
	Cycle = 5,
	Decelerate = 6,
	Linear = 7,
	Overshoot = 8,
};

class InterpolatorFactory
{
public:
	static std::function<float(float)> create(InterpolatorType);
};

#endif // TASK_INTERPOLATOR_H_
