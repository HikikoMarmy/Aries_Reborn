#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

template <typename T>
class threadsafe_queue
{
public:
    void push( T value )
    {
        std::lock_guard lock( mutex_ );
        queue_.push( std::move( value ) );
        condition_.notify_one();
    }

    bool try_pop( T &value )
    {
        std::lock_guard lock( mutex_ );
        if( queue_.empty() ) return false;
        value = std::move( queue_.front() );
        queue_.pop();
        return true;
    }

    bool empty() const
    {
        std::lock_guard lock( mutex_ );
        return queue_.empty();
    }

    size_t size() const
    {
        std::lock_guard lock( mutex_ );
        return queue_.size();
    }

    void clear()
    {
        std::lock_guard lock( mutex_ );
        std::queue<T> empty_queue;
        queue_.swap( empty_queue );
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<T> queue_;
};
