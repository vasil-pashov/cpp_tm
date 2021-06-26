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
	#define CPP_TM_MINOR_VERSION 2
	#define CPP_TM_PATCH_VERSION 0

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
		/// Total number of threads which can wait on the barrier
		unsigned const count;
		/// Number of free slots in the barrier. Initialized with count and goes down on each wait. When 0 all threads are released.
		unsigned spaces;
		/// Counts how many times the barrier was released. It is used to distinguish calls to wait, which release the barrier and
		/// the ones waiting on it. Also used in the predicate of the cv to avoid spurious wakes
		unsigned generation; 
		/// Conditional variable on which the waiting threads sleep
		std::condition_variable cv;
		/// Used by the conditional variable on which the waiting threads sleep
		std::mutex m; 
	};

	/// @brief Thread manager class, keeping a pool of threads which can execute tasks in parallel
	class ThreadManager {
	public:
		/// Create the thread manager with default number of threads based on computer hardware
		/// This will spawn one thread less than the maximul threads supported by the CPU. This is
		/// done in order to account that the main thread is also running.
		ThreadManager();
		/// Create thread manager by spawning exactly numThreads.
		ThreadManager(int numThreads);
		// Copy Semantics
		ThreadManager(const ThreadManager&) = delete;
		ThreadManager& operator=(const ThreadManager&) = delete;

		// Move Semantics
		ThreadManager(ThreadManager&&) = delete;
		ThreadManager& operator=(ThreadManager&&) = delete;

		~ThreadManager();

		/// @brief Run a task in the pool and wait it to finish.
		/// This will first execute all pending tasks, before executing the currently submitted one.
		/// The number of blocks into which the task will be split is the same as the number of workers.
		/// The task can be any arbitrary functor (e.g. function ptr, lambda, class) implementig operator(int, int)
		/// @tparam TFunctor Type of the functor it must be void and take two int parameters. First is the current block index
		/// and the second is the total number of blocks
		/// @param[in] task The task wich is going to be executed by the pool
		template<typename TFunctor>
		void launchSync(TFunctor&& task);

		/// @brief Run a task in the pool and wait it to finish.
		/// This will first execute all pending tasks, before executing the currently submitted one.
		/// The number of blocks into which the task will be split is the same as the number of workers.
		/// The task can be any arbitrary functor (e.g. function ptr, lambda, class) implementig operator(int, int)
		/// @tparam TFunctor Type of the functor it must be void and take two int parameters. First is the current block index
		/// and the second is the total number of blocks
		/// @param[in] task The task wich is going to be executed by the pool
		/// @param numBlocks The number of blocks into which the task is going to be split
		template<typename TFunctor>
		void launchSync(TFunctor&& task, int numBlocks);

		/// @brief Run a task in the pool and do not wait for it to finish. The thread which calls this is free to continue its job.
		/// The number of blocks into which the task will be split is the same as the number of workers.
		/// The task can be any arbitrary functor (e.g. function ptr, lambda, class) implementig operator(int, int)
		/// @tparam TFunctor Type of the functor it must be void and take two int parameters. First is the current block index
		/// and the second is the total number of blocks
		/// @param task[in] The task which is going to be run asynchronously in the pool. The caller is obligated to manage the pointer.
		template<typename TFunctor>
		void launchAsync(TFunctor&& task);

		/// @brief Run a task in the pool and do not wait for it to finish. The thread which calls this is free to continue its job. 
		/// The task can be any arbitrary functor (e.g. function ptr, lambda, class) implementig operator(int, int)
		/// @tparam TFunctor Type of the functor it must be void and take two int parameters. First is the current block index
		/// and the second is the total number of blocks
		/// @param task The task which is going to be run asynchronously in the pool. The caller is obligated to manage the pointer.
		/// @param numBlocks The number of blocks into which the task is going to be split
		template<typename TFunctor>
		void launchAsync(TFunctor&& task, int numBlocks);

		/// @brief Wait for all pending tasks to finish.
		/// IMPORTANT: It is NOT safe to call this from a worker thread.
		void sync();
		
		/// @brief Get the number of worker threads in the pool including the calling (main) thread
		/// @return Number of spawned threads plus one (to account for the calling thread)
		int getWorkersCount() const;

		/// Return the number of threads which the pool spawned and holds internally. Does not include the calling thread.
		/// @return Number of threads which the pool has spawned.
		int getSpawnedThreadsCount() const;
		
	private:
		enum ThreadIndex {
			callingThreadIndex = -1
		};

		/// @brief Abstract interface for a task which can be submitted to ThreadManager
		class ITask {
		public:
			/// @brief Function which will be called when the thread pool reaches this task
			/// @param blockIndex The index of the block for this specific task which is executed by the pool
			/// @param numBlocks Total number of blocks into which task was divided
			/// @return CPPTMStatus from the task
			virtual void runTask(const int blockIndex, const int numBlocks) noexcept = 0;
			virtual ~ITask() {}
		};	

		/// @brief Struct to wrap fire and forget tasks.
		/// It is safe to pass (only) pointers to ManagedTask to the thread manager (only when the manager created them)
		/// On run task it will execute the nested task and decrement the ref counter. The last one will delete the object
		/// Only heap allocated pointers to ManagedTask should be passed to the thread manager queue
		template<typename TFunctor>
		struct ManagedTask final : public ITask {
			ManagedTask(TFunctor&& task, const int initialRefs) :
				task(std::forward<TFunctor>(task)),
				refCounter(initialRefs)
			{ }
			void runTask(int blockIndex, int numBlocks) noexcept override {
				task(blockIndex, numBlocks);
				if (refCounter.fetch_sub(1, std::memory_order::memory_order_acq_rel) == 1) {
					delete this;
				}
			}
		private:
			TFunctor task;
			std::atomic_int refCounter;
		};

		/// @brief Used to wrap functors which do not inherit from ITask (e.g. lambdas)
		template<typename TFunctor>
		struct TaskWrapper final : public ITask {
			TaskWrapper(TFunctor&& task) :
				task(std::forward<TFunctor>(task))
			{

			}
			void runTask(int blockIndex, int numBlocks) noexcept override {
				task(blockIndex, numBlocks);
			}
		private:
			TFunctor task;
		};

		/// @brief Main loop for each worker
		/// @param threadIndex The index of the worker inside ThreadManager::workers array
		void threadLoop(int threadIndex);

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
		/// @return Task barrier which can be added to the pool.
		/// It will force all threads to reach this task before executing the tasks after this.
		TaskInfo getBarrier(TaskInfo::Type type) {
			return TaskInfo(type, barrierId.fetch_add(1));
		}

		/// All threads which are in the pool
		std::vector<std::thread> workers;
		/// Threads wait on this cv for task to be submitted into the pool
		std::condition_variable hasTasksCv;
		/// Used to synchronize adding and popping tasks from the task queue, hasTasksCv uses this
		std::mutex taskMutex;
		/// Queue with tasks waiting to be computed by the workers in the pool
		std::queue<TaskInfo> tasks;
		/// Used to synchronize the threads in the pool when a barrier task is added.
		Barrier barrier;
		/// When a barrier is put in the pool it takes the current barrierId as id and increments barrierId by one
		/// It is safe to warp to 0 again.
		std::atomic<unsigned> barrierId;
	};

	inline ThreadManager::ThreadManager() :
		ThreadManager(std::thread::hardware_concurrency() - 1) {
	}

	inline ThreadManager::ThreadManager(int numThreads) : 
		barrier(numThreads + 1),
		barrierId(0)
	{
		workers.reserve(numThreads);
		for (int i = 0; i < numThreads; ++i) {
			workers.emplace_back(&ThreadManager::threadLoop, this, i);
		}
	}

	inline ThreadManager::~ThreadManager() {
		TaskInfo abortBarrier = getBarrier(TaskInfo::Type::abort);
		std::unique_lock<std::mutex> l(taskMutex);
		tasks.push(std::move(abortBarrier));
		l.unlock();
		hasTasksCv.notify_all();
		for (std::thread& w : workers) {
			if (w.joinable()) {
				w.join();
			}
		}
		assert(
			tasks.size() == 1 &&
			tasks.front().task == nullptr &&
			tasks.front().getType() == TaskInfo::Type::abort
		);
	}

	inline int ThreadManager::getWorkersCount() const {
		return workers.size() + 1;
	}

	inline int ThreadManager::getSpawnedThreadsCount() const {
		return workers.size();
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

				// If we hit abbort barrier we exit from the loop. There is no need to sync using the barrier
				// since we put abort only in the destructor and the destructor uses join for all worker threads.
				// This joins plays the role of a barrier.
				if(type == TaskInfo::Type::abort) {
					return;
				}

				// Note the barrier will sync all worker threads AND the calling thread.
				barrier.wait();

				l.lock();
				if (tasks.size() && tasks.front().task == nullptr && id == tasks.front().getId()) {
					// First thread released by the barrier deletes the barrier from the queue
					tasks.pop();
					l.unlock();
				}

				// Up to this point all threads were synchronized by the barrier. The call which was made
				// was either ThreadManager::sync or ThreadManager::launchSync, no other tasks are on the queue
				// The calling thread must exit the loop here, so that it can continue executing main code.
				// If it does not exit here it will sleep on the conditional variable at the beggining of the loop
				// and the code will stall forever.
				if(threadIndex == callingThreadIndex) {
					return;
				}
			}
		} while (true);
	}



	template<typename TFunctor>
	inline void ThreadManager::launchSync(TFunctor&& task) {
		launchSync(std::forward<TFunctor>(task), getWorkersCount());
	}

	template<typename TFunctor>
	inline void ThreadManager::launchSync(TFunctor&& task, int numBlocks) {
		TaskWrapper<TFunctor> wrappedTask(std::forward<TFunctor>(task));
		std::unique_lock<std::mutex> l(taskMutex);
		for (int i = 0; i < numBlocks; ++i) {
			tasks.emplace(&wrappedTask, numBlocks, i);
		}

		tasks.push(std::move(getBarrier(TaskInfo::Type::barrier)));
		l.unlock();
		hasTasksCv.notify_all();
		threadLoop(callingThreadIndex);
	}


	template<typename TFunctor>
	inline void ThreadManager::launchAsync(TFunctor&& task) {
		launchAsync(std::forward<TFunctor>(task), getSpawnedThreadsCount());
	}

	template<typename TFunctor>
	inline void ThreadManager::launchAsync(TFunctor&& task, int numBlocks) {
		std::unique_lock<std::mutex> l(taskMutex);
		// Note this is not a memory leak as the class internally will commit suicide calling (delete this)
		// when runTask was called numBlocks times. If the thread manager crashes, however the memory will leak.
		ManagedTask<TFunctor>* managed = new ManagedTask<TFunctor>(std::forward<TFunctor>(task), numBlocks);
		for (int i = 0; i < numBlocks; ++i) {
			tasks.emplace(managed, numBlocks, i);
		}
		l.unlock();
		hasTasksCv.notify_all();
	}

	inline void ThreadManager::sync() {
		TaskInfo syncBarrier = getBarrier(TaskInfo::Type::barrier);
		{
			std::lock_guard<std::mutex> l(taskMutex);
			tasks.push(syncBarrier);
		}
		hasTasksCv.notify_all();
		threadLoop(callingThreadIndex);
	}

	struct LoopBlockData {
		LoopBlockData(const int blockIndex, const int numBlocks, const int totalLoopSize) {
			const int blockSize = (totalLoopSize + numBlocks) / numBlocks;
			start = blockIndex * blockSize;
			end = std::min(start + blockSize, totalLoopSize);
		}
		int start;
		int end;
	};

	/// @brief Return reference to a global thread manager 
	/// The global thread manager is statically initialized when this functions gets called for the first time
	/// @return Return reference to a global thread manager 
	inline ThreadManager& getGlobalTM() {
		static ThreadManager globalTm;
		return globalTm;
	}
}
