#include <gtest/gtest.h>
#include <cpp_tm.h>
#include <numeric>

class MultithreadedSumFunctor {
public:
	MultithreadedSumFunctor(int numBlocks, uint64_t sumTo) :
		threadSum(numBlocks),
		sumTo(sumTo) {
	}

	MultithreadedSumFunctor(const MultithreadedSumFunctor&) {
		assert(false && "Should not be called");
	}	

	MultithreadedSumFunctor(MultithreadedSumFunctor&&) {
		assert(false && "Should not be called");
	}

	~MultithreadedSumFunctor() = default;

	void operator()(int blockIndex, int numBlocks) {
		if (!numBlocks) {
			return;
		}
		const uint64_t blockSize = (sumTo + numBlocks) / numBlocks;
		const uint64_t start = blockSize * blockIndex;
		const uint64_t end = std::min(sumTo, start + blockSize);
		for (uint64_t i = start; i < end; ++i) {
			threadSum[blockIndex] += i;
		}
	}

	uint64_t reduce() const {
		return std::accumulate(threadSum.begin(), threadSum.end(), uint64_t(0));
	}
private:
	std::vector<uint64_t> threadSum;
	uint64_t sumTo;
};

[[nodiscard]] static inline uint64_t expectedSumResult(uint64_t sumTo) {
	return (sumTo - 1) * sumTo / 2;
}

template<typename Functor>
void repeatTest(const int numRepetitions, Functor f) {
	for (int i = 0; i < numRepetitions; ++i) {
		f();
	}
}

constexpr int numReps = 50;

TEST(ThreadManagerBasic, Sync) {
	repeatTest(numReps, []() {
		CPPTM::ThreadManager manager;
		manager.sync();
	});
}

TEST(ThreadManagerBasic, SumEmpty) {
	repeatTest(numReps, []() {
		CPPTM::ThreadManager manager;
		const int numWorkers = manager.getNumWorkers();
		const uint64_t sumTo = 0;
		const int numBlocks = 0;
		MultithreadedSumFunctor sumJob(numWorkers, sumTo);
		manager.launchSync(sumJob, numBlocks);
		EXPECT_EQ(sumJob.reduce(), 0);
	});

}

TEST(ThreadManagerBasic, SumBlockSizeLessThanThreads) {
	repeatTest(numReps, []() {
		const int numWorkers = 8;
		CPPTM::ThreadManager manager(numWorkers);
		const uint64_t sumTo = 5;
		const int numBlocks = 5;
		MultithreadedSumFunctor sumJob(numBlocks, sumTo);
		manager.launchSync(sumJob, numBlocks);
		const uint64_t res = expectedSumResult(sumTo);
		const uint64_t reduceRes = sumJob.reduce();
		EXPECT_EQ(reduceRes, res);
	});
}

TEST(ThreadManagerBasic, SumDefaultBlockSize) {
	repeatTest(numReps, []() {
		CPPTM::ThreadManager manager;
		const int numWorkers = manager.getNumWorkers();
		const uint64_t sumTo = 999994;
		MultithreadedSumFunctor sumJob(numWorkers, sumTo);
		manager.launchSync(sumJob);
		const uint64_t res = expectedSumResult(sumTo);
		EXPECT_EQ(sumJob.reduce(), res);
	});
}

TEST(ThreadManagerBasic, SumBlockSizeLargerThanThreads) {
	repeatTest(numReps, []() {
		const int numWorkers = 8;
		CPPTM::ThreadManager manager(numWorkers);
		const uint64_t sumTo = 1000003;
		const int numBlocks = 100;
		MultithreadedSumFunctor sumJob(numBlocks, sumTo);
		EXPECT_GT(numBlocks, numWorkers);
		manager.launchSync(sumJob, numBlocks);
		const uint64_t res = expectedSumResult(sumTo);
		EXPECT_EQ(sumJob.reduce(), res);
	});
}

TEST(ThreadManagerBasic, SumAsync) {
	repeatTest(numReps, []() {
		CPPTM::ThreadManager manager;
		const int numWorkers = manager.getNumWorkers();
		const uint64_t sumTo = 999994;
		MultithreadedSumFunctor sumJob(numWorkers, sumTo);
		manager.launchAsync(sumJob);
		manager.sync();
		const uint64_t res = expectedSumResult(sumTo);
		EXPECT_EQ(sumJob.reduce(), res);
	});
}