// Team-written containers carried over unchanged from the Phase 2 prototype.
// ScratchVector  : minimal growable array (replaces std::vector for feedback storage)
// ScratchPriorityQueue : binary max-heap with a custom comparator
#ifndef FB_DS_H
#define FB_DS_H
#include <new>
#include <cstddef>
#include <utility>
#include <algorithm>

// Small vector implementation used to store feedback without std::vector.
template <typename T>
class ScratchVector {
public:
    ScratchVector() : data_(nullptr), size_(0), capacity_(0) {}
    ScratchVector(ScratchVector&& other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = other.capacity_ = 0;
    }
    ~ScratchVector() {
        clear();
        ::operator delete(data_);
    }

    ScratchVector(const ScratchVector&) = delete;
    ScratchVector& operator=(const ScratchVector&) = delete;
    ScratchVector& operator=(ScratchVector&& other) noexcept {
        if (this != &other) {
            clear();
            ::operator delete(data_);
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = other.capacity_ = 0;
        }
        return *this;
    }

    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }
    bool empty() const { return size_ == 0; }
    size_t size() const { return size_; }

    template <typename... Args>
    void emplace_back(Args&&... args) {
        if (size_ == capacity_) reserve(capacity_ == 0 ? 4 : capacity_ * 2);
        new (data_ + size_) T(std::forward<Args>(args)...);
        ++size_;
    }

    T& operator[](size_t index) { return data_[index]; }
    const T& operator[](size_t index) const { return data_[index]; }
    void pop_back() {
        if (size_ > 0) data_[--size_].~T();
    }

private:
    void reserve(size_t newCapacity) {
        T* newData = static_cast<T*>(::operator new(sizeof(T) * newCapacity));
        size_t moved = 0;
        try {
            for (; moved < size_; ++moved)
                new (newData + moved) T(std::move(data_[moved]));
        } catch (...) {
            while (moved > 0) newData[--moved].~T();
            ::operator delete(newData);
            throw;
        }
        for (size_t i = 0; i < size_; ++i) data_[i].~T();
        ::operator delete(data_);
        data_ = newData;
        capacity_ = newCapacity;
    }

    void clear() {
        for (size_t i = 0; i < size_; ++i) data_[i].~T();
        size_ = 0;
    }

    T* data_;
    size_t size_;
    size_t capacity_;
};

// Binary max-heap priority queue implemented without std::priority_queue.
template <typename T, typename Compare>
class ScratchPriorityQueue {
public:
    bool empty() const { return items_.empty(); }
    const T& top() const { return items_[0]; }

    void push(const T& value) {
        items_.emplace_back(value);
        size_t child = items_.size() - 1;
        while (child > 0) {
            size_t parent = (child - 1) / 2;
            if (!compare_(items_[parent], items_[child])) break;
            using std::swap;
            swap(items_[parent], items_[child]);
            child = parent;
        }
    }

    void pop() {
        if (items_.size() <= 1) {
            items_.pop_back();
            return;
        }
        items_[0] = std::move(items_[items_.size() - 1]);
        items_.pop_back();
        size_t parent = 0;
        while (true) {
            size_t left = parent * 2 + 1;
            size_t right = left + 1;
            size_t best = parent;
            if (left < items_.size() && compare_(items_[best], items_[left])) best = left;
            if (right < items_.size() && compare_(items_[best], items_[right])) best = right;
            if (best == parent) break;
            using std::swap;
            swap(items_[parent], items_[best]);
            parent = best;
        }
    }

private:
    ScratchVector<T> items_;
    Compare compare_;
};

#endif
