#include "thread_manager.h"
#include "error_code.h"

namespace Utils {
	
	ThreadManager::ThreadManager() :
		ThreadManager(std::thread::hardware_concurrency()) {
	}

	ThreadManager::ThreadManager(int numThreads) :
		barrier(numThreads)
	{
		syncDone.test_and_set(std::memory_order::memory_order_release);
		workes.reserve(numThreads);
		for (int i = 0; i < numThreads; ++i) {
			workes.emplace_back(&ThreadManager::threadLoop, this, i);
		}
	}

	ThreadManager::~ThreadManager() {
		TaskInfo abortBarrier = getBarrier(TaskInfo::Type::abort);
		std::unique_lock l(taskMutex);
		tasks.push(std::move(abortBarrier));
		l.unlock();
		hasTasksCv.notify_all();
		for (std::thread& w : workes) {
			if (w.joinable()) {
				w.join();
			}
		}
		ASSERT_BREAK(tasks.empty());
	}

	void ThreadManager::threadLoop(int threadIndex) {
		do {
			std::unique_lock l(taskMutex);
			hasTasksCv.wait(l, [&]() {return tasks.size(); });

			if (tasks.front().task) {
				// Actual task which is going to be executed
				TaskInfo t = std::move(tasks.front());
				tasks.pop();
				l.unlock();
				t.task->runTask(t.blockIndex, t.numBlocks);
			} else {
				// Barrier. All threads will reach this if. When each and every thread has entered here
				// the first thread that exits the barrier will remove the barrier from the queue. Then
				// depending on the type of the barrier, threads will either return from the loop or they
				// will continue executing/waiting to execute tasks.

				// It is IMPERATIVE to use volatile, since otherwise the compiler optimizes the calls away
				// and the if statements below are not executed as expected
				// (it replaces id == tasks.front().getId() with tasks.front().getId()  == tasks.front().getId()
				// which is always true)
				const volatile TaskInfo::TaskId id = tasks.front().getId();
				const volatile TaskInfo::Type type = tasks.front().getType();

				l.unlock();

				barrier.wait();
				
				l.lock();
				if (tasks.size() && tasks.front().task == nullptr && id == tasks.front().getId()) {
					// First thread released by the barrier deletes the barrier from the queue
					tasks.pop();
					l.unlock();
					notifySyncLaunchDone();
				}
				if (type == TaskInfo::Type::abort) {
					return;
				}
			}
		} while (true);
	}

	void ThreadManager::launchSync(std::shared_ptr<ITask> task) {
		launchSync(task, workes.size());
	}

	void ThreadManager::launchSync(std::shared_ptr<ITask> task, int numBlocks) {
		std::unique_lock l(taskMutex);
		for (int i = 0; i < numBlocks; ++i) {
			TaskInfo ti(task, numBlocks, i);
			tasks.push(std::move(ti));
		}
		
		tasks.push(std::move(getBarrier(TaskInfo::Type::barrier)));
		l.unlock();
		hasTasksCv.notify_all();
		
		waitSyncToHappen();
		ASSERT_BREAK(tasks.empty());
	}

	void ThreadManager::launchAsync(std::shared_ptr<ITask> task) {
		launchAsync(task, workes.size());
	}

	void ThreadManager::launchAsync(std::shared_ptr<ITask> task, int numBlocks) {
		std::unique_lock l(taskMutex);
		for (int i = 0; i < numBlocks; ++i) {
			TaskInfo ti(task, numBlocks, i);
			tasks.push(std::move(ti));
		}
		l.unlock();
		hasTasksCv.notify_all();
	}

	void ThreadManager::sync() {
		TaskInfo ti = getBarrier(TaskInfo::Type::barrier);
		{
			std::lock_guard l(taskMutex);
			tasks.push(ti);
		}
		hasTasksCv.notify_all();
		waitSyncToHappen();

	}

}