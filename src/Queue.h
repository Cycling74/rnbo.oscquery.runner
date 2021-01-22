#pragma once

#include <mutex>
#include <queue>
#include <condition_variable>
#include <chrono>

//a thread safe (not realtime safe) queue
template <typename T>
class Queue {
	public:
		//push from any thread
		void push(T item) {
			std::lock_guard<std::mutex> guard(mMutex);
			mQueue.push(item);
			mCondition.notify_one();
		}

		//wait until there is data available.
		T pop() {
			std::unique_lock<std::mutex> guard(mMutex);
			while (mQueue.empty()) {
				mCondition.wait(guard);
			}
			T item = mQueue.front();
			mQueue.pop();
			return item;
		}

		//wait for an amount of time
		template <typename Rep, typename Period>
		std::optional<T> popTimeout(std::chrono::duration<Rep, Period> timeout) {
			std::unique_lock<std::mutex> guard(mMutex);
			if (mQueue.empty())
				mCondition.wait_for(guard, timeout);
			if (mQueue.empty())
				return std::nullopt;
			T item = mQueue.front();
			mQueue.pop();
			return std::optional(item);
		}

		//get data if it is available.
		std::optional<T> tryPop() {
			std::lock_guard<std::mutex> guard(mMutex);
			if (mQueue.empty())
				return std::nullopt;
			T item = mQueue.front();
			mQueue.pop();
			return std::optional(item);
		}
	private:
		std::queue<T> mQueue;
		std::mutex mMutex;
		std::condition_variable mCondition;
};
