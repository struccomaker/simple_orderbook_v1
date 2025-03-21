#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <vector>
#include <atomic>

class TaskQueue {
public:
    using Task = std::function<void()>;

    TaskQueue(size_t numThreads) : shutdown_(false) {
        // Start worker threads
        for (size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back([this] { workerThread(); });
        }
    }

    ~TaskQueue() {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            shutdown_ = true;
        }

        // Notify all threads
        condition_.notify_all();

        // Join all threads
        for (auto& thread : workers_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    // Add a task to the queue
    void enqueue(Task task) {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);

            // Don't allow new tasks after shutdown
            if (shutdown_) {
                return;
            }

            taskQueue_.push(std::move(task));
        }

        // Notify one thread that there's a new task
        condition_.notify_one();
    }

private:
    // Thread worker function
    void workerThread() {
        while (true) {
            Task task;

            {
                std::unique_lock<std::mutex> lock(queueMutex_);

                // Wait until there's a task or shutdown
                condition_.wait(lock, [this] {
                    return shutdown_ || !taskQueue_.empty();
                    });

                // If queue is empty and shutdown, exit
                if (shutdown_ && taskQueue_.empty()) {
                    return;
                }

                // Get the next task
                task = std::move(taskQueue_.front());
                taskQueue_.pop();
            }

            // Execute the task
            task();
        }
    }

    std::vector<std::thread> workers_;           // Worker threads
    std::queue<Task> taskQueue_;                 // Task queue
    std::mutex queueMutex_;                      // Mutex for queue access
    std::condition_variable condition_;          // Condition variable for thread synchronization
    bool shutdown_;                              // Shutdown flag
};