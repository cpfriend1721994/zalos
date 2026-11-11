// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace zcall::audio {

template <typename T>
class SpscRing {
public:
    explicit SpscRing(size_t capacityPow2) : m_slots(capacityPow2), m_mask(capacityPow2 - 1) {}

    T* beginPush() {
        const size_t head = m_head.load(std::memory_order_relaxed);
        if (head - m_tail.load(std::memory_order_acquire) >= m_slots.size()) return nullptr;
        return &m_slots[head & m_mask];
    }
    void commitPush() { m_head.store(m_head.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

    T* front() {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) return nullptr;
        return &m_slots[tail & m_mask];
    }
    void pop() { m_tail.store(m_tail.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

    size_t capacity() const { return m_slots.size(); }
    std::vector<T>& storage() { return m_slots; }

private:
    std::vector<T> m_slots;
    size_t m_mask;
    alignas(64) std::atomic<size_t> m_head{0};
    alignas(64) std::atomic<size_t> m_tail{0};
};

}
