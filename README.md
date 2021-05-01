# cpp_tm
This is a small, header only, cross platform thread manager implemented in C++11. 

When `CPPTM::ThreadManager` is created, it spawns a number of threads and keeps them asleep via conditional variable until some task is pushed to the thread manager queue.
The library provides functionality to split a certain task into arbitrary number of chunks which could be potentially executed by different threads in parallel.
Tasks can be run synchronously or asynchronously, for examples checkout [How to Use](#How-to-Use) section.

# How to Use
## The Obligatory Vector Addition Example
```cpp
// Define the input data
const int sz = 1000;
int a[sz], b[sz], c[sz];
// Fill the arrays somehow
for(int i = 0; i < sz; ++i) {
	a[i] = i;
	b[i] = sz + i;
}
// Inherit from CPPTM::ITask and override runTask
struct AddVectorsTask : public CPPTM::ITask {
	AddVectorsTask(int* a, int* b, int* res, int size) : 
		a(a),
		b(b),
		res(res),
		size(size)
	{}
	CPPTM::CPPTMStatus runTask(const int blockIndex, const int numBlocks) noexcept override {
		const int blockSize = (size + numBlocks) / numBlocks;
		const int start = (size / numBlocks) * blockIndex;
		const int end = std::min(start + blockSize, size);
		for(int i = start; i < end; ++i) {
			res[i] = a[i] + b[i];
		}
		return CPPTM::CPPTMStatus::SUCCESS;
	}
	int* a;
	int* b;
	int* res;
	int size;
};
// Create a task instance
AddVectorsTask task(a, b, c, sz);
// Create the thread manager
CPPTM::ThreadManager tm;
// Push the task to the manager
tm.launchSync(&task);
```
## Initialize
* Use default constructor `CPPTM::ThreadManager::ThreadManager(void)` and let the library choose how many threads to spawn.
  It spawns `std::thread::hardware_concurrency()` number of threads by default.
* Use constructor `CPPTM::ThreadManager::ThreadManager(int numThreads)` to spawn exactly `numThreads` threads
* Use `CPPTM::getGlobalTM(void)` to get a "global manager". It is created the first time the function is called, subsequent calls return reference to it.
  The global manager is created via `CPPTM::ThreadManager::ThreadManager(void)`.

## Create Task Class
* Create a class and inherit `CPPTM::ITask`. 
* Override `runTask(const int blockIndex, const int numBlocks) noexcept`. When the task is pushed to the thread manager it could be split into several blocks. Each of those
will execute `runTask` and will get as an input `numBlocks`(the total number of blocks into which the task was split) and `blockIndex` (the index for the current
block), where `blockIndex` varies between 0 and `numBlocks - 1`.

## Push Task to the Manager
### Sync
* `CPPTM::ThreadManager::launchSync(ITask* const task)` will split the task into number of blocks equal to the number of spawned threads. The caller
will sleep until the task is finished. If there are pending tasks in the manager they will be executed first.
* `CPPTM::ThreadManager::launchSync(ITask* const task, int numBlocks)` will split the task into `numBlocks` blocks.
The caller will sleep until the task is finished. If there are pending tasks in the manager they will be executed first.
`numBlocks` can be any positive number (it could be larger than the number of spawned threads).
### Async
* `CPPTM::ThreadManager::launchAsync(ITask* const task)` will split the task into number of blocks equal to the number of spawned threads.
The caller will continue executing without waiting for the task to finish. If there are pending tasks in the manager they will be executed first.
* `CPPTM::ThreadManager::launchAsync(ITask* const task, int numBlocks)` will split the task into `numBlocks` blocks.
The caller will continue executing without waiting for the task to finish. If there are pending tasks in the manager they will be executed first.
`numBlocks` can be any positive number (it could be larger than the number of spawned threads).
* `CPPTM::ThreadManager::launchAsync(std::unique_ptr<ITask> task);` and `CPPTM::ThreadManager::launchAsync(std::unique_ptr<ITask> task, int numBlocks);`
are the same as the above except that the thread manager will deallocate the task when the task is completed.

# Dependencies
* Compiler supporting C++11
* Google test version 1.10.0, it is needed only if unit tests are going to be executed

## Installing Dependencies via Conan
Google Test can be installed via [conan](https://conan.io/) using the provided `conanfile.txt`.
```
conan install conanfile.txt -if <path_to_build_folder> --build=missing -s build_type=<desired_build_type>
```
Where:
* `<path_to_build_folder>` is the folder where the project is going to be built. It could be a top level project, where cpp_tm is added as subdirectory.
* `<desired_build_type>` could be `Debug` or `Release`

Note the default generator is `cmake_find_package_multi` and it can be changed if needed. If `cmake_find_package_multi` is used it is **important** not to forget to
set these two CMake `CMAKE_PREFIX_PATH`, `CMAKE_MODULE_PATH` so that `find_package` utility.find_package` function can find the files generated by conan. Both folders should be set
to whatever `<path_to_build_folder>` was chosen in the step above, using absolute path is imperative. Note also that for building missing packages for `gtest` C++11 standard library is needed (libstdc++11).
You might need to edit your default conan profile or create new profile.

Generating and building project with unit tests is enabled by default. To disable it set `CPPTM_UNIT_TESTS` to `False`

## Using CMake find_package for Dependencies
If you have `FindGTest.cmake` already existing, append the absolute path to the directory containing it to`CMAKE_PREFIX_PATH` and `CMAKE_MODULE_PATH`.

# How to Integrate into Excisting Projects
## CMake Workflow
Add the project directory as a subfolder to your CMake project, then link with the library like this: `target_link_libraries(${PROJECT_NAME} PRIVATE cpp_tm)`.
Note `PRIVATE` could be changed to `PUBLIC` or `INTERFACE` if needed.
## Other
Copy `include/cpp_tm.h` in one of the include directories for your project.

# Build and Run Tests
```sh
git clone https://github.com/vasil-pashov/cpp_tm.git
cd cpp_tm
mkdir build
conan install . --build=missing -s build_type=Release -if ./build
cmake -B"./build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=${PWD}/build -DCMAKE_MODULE_PATH=${PWD}/build
cd build
cmake --build . --config Release
cd test/unit
ctest
```

# Install
The library can be installed to the system, so that other cmake project can use it via the `fing_package` utility.
```
git clone https://github.com/vasil-pashov/cpp_tm.git
cd cpp_tm
mkdir build
cmake -B"./build" -DCMAKE_BUILD_TYPE=Release -DCPPTM_UNIT_TESTS=OFF
cd build
sudo cmake --install ./
```
To change the install directories use `CMAKE_INSTALL_PREFIX` and `CONFIG_INSTALL_DIR` variables (if some nonstandard structure is needed). Where `CONFIG_INSTALL_DIR` gets appended to `CMAKE_INSTALL_PREFIX`.
