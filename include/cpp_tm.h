#pragma once
#include <thread>
#include <memory>
#include <vector>
#include <condition_variable>
#include <queue>
#include <utility>
#include <atomic>
#include <cassert>

namespace CPPTM {
	
	#define CPP_TM_MAJOR_VERSION 0
	#define CPP_TM_MINOR_VERSION 1
	#define CPP_TM_PATCH_VERSION 0

	enum class CPPTMStatus {
		SUCCESS = 0,
		ERROR
	};

	/// @brief Abstract interface for a task which can be submitted to ThreadManager
	class ITask {
	public:
		/// @brief Function which will be called when the thread pool reaches this task
		/// @param blockIndex The index of the block for this specific task which is executed by the pool
		/// @param numBlocks Total number of blocks into which task was divided
		/// @return CPPTMStatus from the task
		virtual CPPTMStatus runTask(const int blockIndex, const int numBlocks) noexcept = 0;
		virtual ~ITask() {}
	};
	/// @brief Thread barrier class.
	/// It is initialized with the number of threads which could wait on the barrier
	/// When a thread calls Barrier::wait it will wait on the barrier until the count
	/// of the waiting threads becomes Barrier::count
	class Barrier {
	public:
		/// @brief Constructor for thread barrier class
		/// @param count Number of threads which could wait on this barrier
		Barrier(unsigned count) :
			count(count),
			spaces(count),
			generation(0)
		{ }
		/// @brief Wait on the barrier. When Barrier::count call this all of them will be released.
		void wait() {
			std::unique_lock<std::mutex> l(m);
			const unsigned myGeneration = generation;
			if (--spaces == 0) {
				spaces = count;
				++generation;
				l.unlock();
				cv.notify_all();
			} else {
				cv.wait(l, [&]() {return generation != myGeneration; });
			}
		}
	private:
		unsigned const count; ///< Total number of threads which can wait on the barrier
		unsigned spaces; ///< Number of free slots in the barrier. Initialized with count and goes down on each wait. When 0 all threads are released.
		/// Counts how many times the barrier was released. It is used to distinguish calls to wait, which release the barrier and the ones waiting on it
		/// Also used in the predicate of the cv to avoid spurious wakes
		unsigned generation; 
		std::condition_variable cv; ///< Conditional variable on which the waiting threads sleep
		std::mutex m; ///< Used by the conditional variable on which the waiting threads sleep 
	};

	/// @brief Thread manager class, keeping a pool of threads which can execute tasks in parallel
	class ThreadManager {
	public:
		ThreadManager();
		ThreadManager(int numThreads);
		// Copy Semantics
		ThreadManager(const ThreadManager&) = delete;
		ThreadManager& operator=(const ThreadManager&) = delete;

		// Move Semantics
		ThreadManager(ThreadManager&&) = delete;
		ThreadManager& operator=(ThreadManager&&) = delete;

		~ThreadManager();

		/// @brief Run a task in the pool and wait it to finish. This will first execute all pending tasks, before executing the currently submitted one.
		/// The number of blocks into which the task will be split is the same as the number of workers.
		/// IMPORTANT: It is NOT safe to call this from a worker thread.
		/// This is the same as calling launchSync(task, getNumWorkers())
		/// @param task The task which is going to be executed by the pool
		void launchSync(ITask* const task);

		/// @brief Run a task in the pool and wait it to finish. This will first execute all pending tasks, before executing the currently submitted one.
		/// IMPORTANT: It is NOT safe to call this from a worker thread.
		/// @param task The task which is going to be executed by the pool
		/// @param numBlocks The number of blocks into which the task is going to be split
		void launchSync(ITask* const, int numBlocks);

		/// @brief Run a task in the pool and do not wait for it to finish. The thread which calls this is free to continue its job.
		/// Use this signature when the caller handles the memory for the created task.
		/// The number of blocks into which the task will be split is the same as the number of workers.
		/// @param task The task which is going to be run asynchronously in the pool. The caller is obligated to manage the pointer.
		void launchAsync(ITask* const task);

		/// @brief Run a task in the pool and do not wait for it to finish. The thread which calls this is free to continue its job. 
		/// Use this signature when the caller handles the memory for the created task.
		/// @param task The task which is going to be run asynchronously in the pool. The caller is obligated to manage the pointer.
		/// @param numBlocks The number of blocks into which the task is going to be split
		void launchAsync(ITask* const task, int numBlocks);

		/// @brief Launch "fire and forget" kind of task. The thread manager will manage the memory of the task and will delete it when done
		/// The number of blocks into which the task will be split is the same as the number of workers.
		/// @param task The task which is going to be run asynchronously in the pool. The thread manager is obligated to manage the pointer.
		void launchAsync(std::unique_ptr<ITask> task);

		/// @brief Launch "fire and forget" kind of task. The thread manager will manage the memory of the task and will delete it when done
		/// The number of blocks into which the task will be split is the same as the number of workers.
		/// @param task The task which is going to be run asynchronously in the pool. The thread manager is obligated to manage the pointer.
		void launchAsync(std::unique_ptr<ITask> task, int numBlocks);

		/// @brief Wait for all pending tasks to finish.
		/// IMPORTANT: It is NOT safe to call this from a worker thread.
		void sync();
		
		/// @brief Get the number of worker threads in the pool
		/// @return Number of working threads in the pool
		const int getNumWorkers() const {
			return workes.size();
		}
	private:
		/// @brief Struct to wrap fire and forget tasks.
		/// It is safe to pass (only) pointers to ManagedTask to the thread manager (only when the manager created them)
		/// On run task it will execute the nested task and decrement the ref counter. The last one will delete the object
		/// Only heap allocated pointers to ManagedTask should be passed to the thread manager queue
		struct ManagedTask final : public ITask {
			ManagedTask(std::unique_ptr<ITask> task, const int initialRefs) :
				task(std::move(task)),
				refCounter(initialRefs)
			{ }
			CPPTMStatus runTask(int blockIndex, int numBlocks) noexcept override {
				task->runTask(blockIndex, numBlocks);
				if (refCounter.fetch_sub(1, std::memory_order::memory_order_acq_rel) == 1) {
					delete this;
				}
				return CPPTMStatus::SUCCESS;
			}
		private:
			std::unique_ptr<ITask> task;
			std::atomic_int refCounter;
		};
		/// @brief Main loop for each worker
		/// @param threadIndex The index of the worker inside ThreadManager::workers array
		void threadLoop(int threadIndex);

		/// @brief Notify all threads which called sync or lauchSync that the barrier has been reached by all threads
		void notifySyncLaunchDone() {
			syncDone.clear(std::memory_order_release);
			syncCv.notify_one();
		}

		/// @brief Called by sync and launcSync. Waits all threads to reach the first barrier in the command queue.
		void waitSyncToHappen() {
			std::unique_lock<std::mutex> lock(syncMut);
			syncCv.wait(lock, [&]() {return !syncDone.test_and_set(std::memory_order_acq_rel); });
		}

		/// @brief Wrapper around Task interface which carriers internal data needed by the ThreadManager class
		struct TaskInfo {
			enum class Type : unsigned {
				/// Blocks threads in the pool to reach this task before continuing their work. Also means that the main thread is waiting for the
				/// pool to sync. After all threads have reached the barrier the main thread will be notified too.
				barrier,
				/// Added when the pool destructor is called. After all threads in the pool reach an abort barrier they will exit their main loop.
				abort
			};
			using TaskId = unsigned;
			ITask* const task;
			union {
				unsigned numBlocks; ///< The number of blocks into which the task is split. Valid if task != nullptr
				unsigned id; ///< Id of the barrier. Valid if task == nullptr
			};
			union {
				unsigned blockIndex; ///< The index of the current block [0, numBlocks). Valid if task != nullptr
				Type type; ///< The type of the barrier. Valid if task == nullptr
			};

			TaskInfo(
				ITask* const task,
				unsigned numBlocks,
				unsigned blockIndex
			) :
				task(task),
				numBlocks(numBlocks),
				blockIndex(blockIndex)
			{ }

			/// @brief Construct a barrier task. It is used to synchronize the threads in the ThreadManger.
			/// When a thread reaches TaskInfo created with this constructor it will wait until all other threads reach it
			/// @param type The type of the barrier (abort only if added by the destructor, or regular barrier)
			/// @param id ID of the barrier used by the threads in order to know which thread must remove it from the pool
			TaskInfo(Type type, unsigned id) :
				task(nullptr),
				id(id),
				type(type)
			{ }

			/// @brief Get the id of the barrier. Valid only for barrier TaskInfo
			/// @return The ID of the barrier
			TaskId getId() const {
				assert(task == nullptr);
				return id;
			}

			/// @brief Get the type of the barrier. Valid only for barrier TaskInfo
			/// @return The type of the barrier
			Type getType() const {
				assert(task == nullptr);
				return type;
			}
		};

		/// @brief Create a task barrier
		/// @param type The type of the barrier (abort, barrier)
		/// @return Task barrier which can be added to the pool. It will force all threads to reach this task before executing the tasks after this.
		TaskInfo getBarrier(TaskInfo::Type type) {
			return TaskInfo(type, barrierId.fetch_add(1));
		}

		std::vector<std::thread> workes; ///< All threads which are in the pool
		std::condition_variable hasTasksCv; ///< Threads wait on this cv for task to be submitted into the pool
		std::mutex taskMutex; ///< Used to synchronize adding and popping tasks from the task queue, hasTasksCv uses this
		std::queue<TaskInfo> tasks; ///< Queue with tasks waiting to be computed by the workers in the pool
		Barrier barrier; ///< Used to synchronize the threads in the pool when a barrier task is added.

		std::condition_variable syncCv; ///< Calls to sync and launchSync wait on this
		std::mutex syncMut; ///< Used by syncCv
		/// Used in the predicate of syncCv to avoid spurious wake. When the flag is set this means that there could be working threads
		/// When the task barrier is reached this flag is cleared. The predicate of syncCv waits until the flag is cleared and sets it again after that.
		std::atomic_flag syncDone = ATOMIC_FLAG_INIT;

		/// When a barrier is put in the pool it takes the current barrierId as id and increments barrierId by one
		/// It is safe to warp to 0 again.
		std::atomic<unsigned> barrierId;
	};

	inline ThreadManager::ThreadManager() :
		ThreadManager(std::thread::hardware_concurrency()) {
	}

	inline ThreadManager::ThreadManager(int numThreads) :
		barrier(numThreads) {
		syncDone.test_and_set(std::memory_order::memory_order_release);
		workes.reserve(numThreads);
		for (int i = 0; i < numThreads; ++i) {
			workes.emplace_back(&ThreadManager::threadLoop, this, i);
		}
	}

	inline ThreadManager::~ThreadManager() {
		TaskInfo abortBarrier = getBarrier(TaskInfo::Type::abort);
		std::unique_lock<std::mutex> l(taskMutex);
		tasks.push(std::move(abortBarrier));
		l.unlock();
		hasTasksCv.notify_all();
		for (std::thread& w : workes) {
			if (w.joinable()) {
				w.join();
			}
		}
		assert(tasks.empty());
	}

	inline void ThreadManager::threadLoop(int threadIndex) {
		do {
			std::unique_lock<std::mutex> l(taskMutex);
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

	inline void ThreadManager::launchSync(ITask* const task) {
		launchSync(task, workes.size());
	}

	inline void ThreadManager::launchSync(ITask* const task, int numBlocks) {
		std::unique_lock<std::mutex> l(taskMutex);
		for (int i = 0; i < numBlocks; ++i) {
			tasks.emplace(task, numBlocks, i);
		}

		tasks.push(std::move(getBarrier(TaskInfo::Type::barrier)));
		l.unlock();
		hasTasksCv.notify_all();

		waitSyncToHappen();
		assert(tasks.empty());
	}

	inline void ThreadManager::launchAsync(ITask* const task) {
		launchAsync(task, workes.size());
	}

	inline void ThreadManager::launchAsync(ITask* const task, int numBlocks) {
		std::unique_lock<std::mutex> l(taskMutex);
		for (int i = 0; i < numBlocks; ++i) {
			tasks.emplace(task, numBlocks, i);
		}
		l.unlock();
		hasTasksCv.notify_all();
	}

	inline void ThreadManager::launchAsync(std::unique_ptr<ITask> task) {
		launchAsync(std::move(task), workes.size());
	}

	inline void ThreadManager::launchAsync(std::unique_ptr<ITask> task, int numBlocks) {
		std::unique_lock<std::mutex> l(taskMutex);
		// Note this is not a memory leak as the class internally will commit suicide calling (delete this)
		// when runTask was called numBlocks times. If the thread manager crashes, however the memory will leak.
		ManagedTask* managed = new ManagedTask(std::move(task), numBlocks);
		for (int i = 0; i < numBlocks; ++i) {
			tasks.emplace(managed, numBlocks, i);
		}
		l.unlock();
		hasTasksCv.notify_all();
	}

	inline void ThreadManager::sync() {
		TaskInfo ti = getBarrier(TaskInfo::Type::barrier);
		{
			std::lock_guard<std::mutex> l(taskMutex);
			tasks.push(ti);
		}
		hasTasksCv.notify_all();
		waitSyncToHappen();

	}

	/// @brief Return reference to a global thread manager 
	/// The global thread manager is statically initialized when this functions gets called for the first time
	/// @return Return reference to a global thread manager 
	inline ThreadManager& getGlobalTM() {
		static ThreadManager globalTm;
		return globalTm;
	}
}
