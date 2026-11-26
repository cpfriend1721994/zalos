// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace zcall::audio {

class SerialWorker {
public:
    static constexpr int kNoKey = 0;

    SerialWorker() : m_thread([this] { run(); }) {}
    ~SerialWorker() { stop(); }
    SerialWorker(const SerialWorker&) = delete;
    SerialWorker& operator=(const SerialWorker&) = delete;

    void post(std::function<void()> task, int coalesceKey = kNoKey) {
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_stop) return;
            if (coalesceKey != kNoKey && !m_queue.empty() && m_queue.back().key == coalesceKey) {
                m_queue.back().task = std::move(task);
            } else {
                m_queue.push_back(Item{std::move(task), coalesceKey});
            }
        }
        m_cv.notify_one();
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_stop) return;
            m_stop = true;
            m_queue.clear();
        }
        m_cv.notify_one();
        if (m_thread.joinable() && std::this_thread::get_id() != m_thread.get_id()) m_thread.join();
    }

private:
    struct Item {
        std::function<void()> task;
        int key;
    };

    void run() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_lock);
                m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
                if (m_stop) return;
                task = std::move(m_queue.front().task);
                m_queue.pop_front();
            }
            task();
        }
    }

    std::mutex m_lock;
    std::condition_variable m_cv;
    std::deque<Item> m_queue;
    bool m_stop{false};
    std::thread m_thread;
};

}
