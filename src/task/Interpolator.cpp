#include "task/Interpolator.h"
#define _USE_MATH_DEFINES
#include <cmath>

std::function<float(float)> InterpolatorFactory::create(InterpolatorType type)
{
	switch(type) {
	case InterpolatorType::AccelerateDecelerate:{
		return [](float input) -> float {
			return std::cos((input + 1.0f) * M_PI) * 0.5f + 0.5f;
		};
	}
	case InterpolatorType::Accelerate:{
		const float factor2_ = 2.0f;
		return [factor2_](float input) -> float {
			if (factor2_ == 2.0f) {
				return input * input;
			}
			else {
				return std::pow(input, factor2_);
			}
		};
	}
	case InterpolatorType::Anticipate:{
		const float tension_ = 1.0f;
		return [tension_](float input) -> float {
			return input * input * ((tension_ + 1) * input - tension_);
		};
	}
	case InterpolatorType::AnticipateOvershoot:{
		const float tension_ = 1.5f;
		return [tension_](float input) -> float {
			if (input < 0.5f) {
				auto a = [](float t, float s) -> float {
					return t * t * ((s + 1.0f) * t - s);
				};
				return 0.5f * a(input * 2.0f, tension_);
			}
			else {
				auto o = [](float t, float s) -> float {
					return t * t * ((s + 1) * t + s);
				};
				return 0.5f * (o(input * 2.0f - 2.0f, tension_) + 2.0f);
			}
		};
	}
	case InterpolatorType::Bounce:{
		return [](float input) -> float {
			auto bounce = [](float t) -> float {
		        return t * t * 8.0f;
		    };

			input *= 1.1226f;
		    if (input < 0.3535f) return bounce(input);
		    else if (input < 0.7408f) return bounce(input - 0.54719f) + 0.7f;
		    else if (input < 0.9644f) return bounce(input - 0.8526f) + 0.9f;
		    else return bounce(input - 1.0435f) + 0.95f;
		};
	}
	case InterpolatorType::Cycle:{
		const float cycles_ = 2.0f;
		return [cycles_](float input) -> float {
			return (std::sin(2.0f * cycles_ * M_PI * input));
		};
	}
	case InterpolatorType::Decelerate:{
		const float factor_ = 2.0f;
		return [factor_](float input) -> float {
		    if (factor_ == 1.0f) {
		        return (1.0f - (1.0f - input) * (1.0f - input));
		    } else {
		        return (1.0f - std::pow((1.0f - input), 2.0f * factor_));
		    }
		};
	}
	case InterpolatorType::Overshoot:{
		const float tension_ = 1.5f;
		return [tension_](float input)-> float {
		    input -= 1.0f;
		    return input * input * ((tension_ + 1.0f) * input + tension_) + 1.0f;
		};
	}
	case InterpolatorType::Linear:
	default:
		return [](float input) -> float {
			return input;
		};
	}
}

