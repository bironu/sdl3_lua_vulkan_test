#ifndef TASK_TASKPOOL_H_
#define TASK_TASKPOOL_H_

#include "misc/Uncopyable.h"
#include <memory>
#include <utility>
#include <deque>
#include <iostream>

template<typename T>
class TaskPool final
{
public:
	UNCOPYABLE(TaskPool);
	TaskPool() : queTask_(){}
	~TaskPool() = default;

	std::shared_ptr<T> get()
	{
		std::shared_ptr<T> result;
		if (queTask_.empty()) {
			result = std::make_shared<T>();
			//std::cout << "get() : new task create" << std::endl;
		}
		else {
			result = std::move(queTask_.front());
			queTask_.pop_front();
			//std::cout << "get() : task que count = " << queTask_.size() << std::endl;
		}
		return result;
	}

	void collect(std::shared_ptr<T> task)
	{
		queTask_.push_back(std::move(task));
		//std::cout << "collect() : task que count = " << queTask_.size() << std::endl;
	}

private:
	std::deque<std::shared_ptr<T>> queTask_;
};

#endif // TASK_TASKPOOL_H_
