// Team-written data structures. The project does not use STL containers or algorithms.
//   ScratchVector        : growable array
//   ScratchSet           : ordered set (sorted array, binary search)
//   ScratchMap           : ordered map (sorted array, binary search)
//   ScratchPriorityQueue : binary max-heap with a custom comparator
//   scratchSort          : stable insertion sort,  scratchMax / scratchMin : plain helpers
#ifndef FB_DS_H
#define FB_DS_H
#include <new>
#include <cstddef>
#include <utility>
#include <initializer_list>
#include <stdexcept>

template <typename T>
inline void scratchSwap(T& a, T& b) {
    T tmp = std::move(a);
    a = std::move(b);
    b = std::move(tmp);
}
template <typename T>
inline const T& scratchMax(const T& a, const T& b) { return a < b ? b : a; }
template <typename T>
inline const T& scratchMin(const T& a, const T& b) { return b < a ? b : a; }

// Plain two-field record (the project's own pair type).
template <typename A, typename B>
struct ScratchPair {
    A first;
    B second;
};

// Growable array.
template <typename T>
class ScratchVector {
public:
    ScratchVector() : data_(nullptr), size_(0), capacity_(0) {}
    ScratchVector(std::initializer_list<T> items) : data_(nullptr), size_(0), capacity_(0) {
        reserve(items.size());
        for (const T& x : items) push_back(x);
    }
    ScratchVector(const ScratchVector& other) : data_(nullptr), size_(0), capacity_(0) {
        reserve(other.size_);
        for (size_t i = 0; i < other.size_; ++i) push_back(other.data_[i]);
    }
    ScratchVector(ScratchVector&& other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = other.capacity_ = 0;
    }
    ~ScratchVector() {
        clear();
        ::operator delete(data_);
    }

    ScratchVector& operator=(const ScratchVector& other) {
        if (this != &other) {
            ScratchVector copy(other);
            *this = std::move(copy);
        }
        return *this;
    }
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
    T* data() { return data_; }
    const T* data() const { return data_; }

    T& operator[](size_t index) { return data_[index]; }
    const T& operator[](size_t index) const { return data_[index]; }
    T& back() { return data_[size_ - 1]; }
    const T& back() const { return data_[size_ - 1]; }
    T& front() { return data_[0]; }
    const T& front() const { return data_[0]; }

    template <typename... Args>
    void emplace_back(Args&&... args) {
        if (size_ == capacity_) reserve(capacity_ == 0 ? 4 : capacity_ * 2);
        new (data_ + size_) T(std::forward<Args>(args)...);
        ++size_;
    }
    void push_back(const T& value) { emplace_back(value); }
    void push_back(T&& value) { emplace_back(std::move(value)); }
    void pop_back() {
        if (size_ > 0) data_[--size_].~T();
    }

    // Inserts before pos; returns the position of the new element.
    T* insert(const T* pos, const T& value) {
        size_t index = static_cast<size_t>(pos - data_);
        T copy(value);
        emplace_back(std::move(copy));
        for (size_t i = size_ - 1; i > index; --i) scratchSwap(data_[i], data_[i - 1]);
        return data_ + index;
    }
    // Removes [first, last); returns the position after the removed range.
    T* erase(const T* first, const T* last) {
        size_t from = static_cast<size_t>(first - data_);
        size_t to = static_cast<size_t>(last - data_);
        size_t count = to - from;
        for (size_t i = from; i + count < size_; ++i) data_[i] = std::move(data_[i + count]);
        for (size_t i = 0; i < count; ++i) pop_back();
        return data_ + from;
    }
    T* erase(const T* pos) { return erase(pos, pos + 1); }

    void reserve(size_t newCapacity) {
        if (newCapacity <= capacity_) return;
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

private:
    T* data_;
    size_t size_;
    size_t capacity_;
};

// Stable insertion sort on [first, last); comp(a, b) is true when a must come before b.
template <typename T, typename Compare>
void scratchSort(T* first, T* last, Compare comp) {
    for (T* i = first + 1; i < last; ++i) {
        T* j = i;
        while (j > first && comp(*j, *(j - 1))) {
            scratchSwap(*j, *(j - 1));
            --j;
        }
    }
}

// Ordered set kept as a sorted array.
template <typename T>
class ScratchSet {
public:
    ScratchSet() {}
    ScratchSet(std::initializer_list<T> items) {
        for (const T& x : items) insert(x);
    }
    const T* begin() const { return items_.begin(); }
    const T* end() const { return items_.end(); }
    bool empty() const { return items_.empty(); }
    size_t size() const { return items_.size(); }
    void clear() { items_.clear(); }

    const T* find(const T& value) const {
        size_t at = lower(value);
        return (at < items_.size() && !(value < items_[at])) ? items_.begin() + at : items_.end();
    }
    size_t count(const T& value) const { return find(value) == items_.end() ? 0 : 1; }
    bool insert(const T& value) {  // true if the value was new
        size_t at = lower(value);
        if (at < items_.size() && !(value < items_[at])) return false;
        items_.insert(items_.begin() + at, value);
        return true;
    }
    size_t erase(const T& value) {
        const T* it = find(value);
        if (it == items_.end()) return 0;
        items_.erase(it);
        return 1;
    }

private:
    size_t lower(const T& value) const {  // first position whose element is not less than value
        size_t lo = 0, hi = items_.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (items_[mid] < value) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }
    ScratchVector<T> items_;
};

// Ordered map kept as a sorted array of key/value records.
template <typename K, typename V>
class ScratchMap {
public:
    typedef ScratchPair<K, V> Entry;
    ScratchMap() {}
    ScratchMap(std::initializer_list<Entry> entries) {
        for (const Entry& e : entries) (*this)[e.first] = e.second;
    }
    Entry* begin() { return items_.begin(); }
    Entry* end() { return items_.end(); }
    const Entry* begin() const { return items_.begin(); }
    const Entry* end() const { return items_.end(); }
    bool empty() const { return items_.empty(); }
    size_t size() const { return items_.size(); }
    void clear() { items_.clear(); }

    Entry* find(const K& key) {
        size_t at = lower(key);
        return (at < items_.size() && !(key < items_[at].first)) ? items_.begin() + at : items_.end();
    }
    const Entry* find(const K& key) const {
        size_t at = lower(key);
        return (at < items_.size() && !(key < items_[at].first)) ? items_.begin() + at : items_.end();
    }
    size_t count(const K& key) const { return find(key) == items_.end() ? 0 : 1; }
    V& operator[](const K& key) {
        size_t at = lower(key);
        if (at < items_.size() && !(key < items_[at].first)) return items_[at].second;
        Entry fresh{key, V()};
        return items_.insert(items_.begin() + at, fresh)->second;
    }
    const V& at(const K& key) const {
        const Entry* e = find(key);
        if (e == items_.end()) throw std::runtime_error("ScratchMap: key not found");
        return e->second;
    }
    bool emplace(const K& key, const V& value) {  // true if the key was new
        if (find(key) != items_.end()) return false;
        (*this)[key] = value;
        return true;
    }
    size_t erase(const K& key) {
        Entry* e = find(key);
        if (e == items_.end()) return 0;
        items_.erase(e);
        return 1;
    }
    void erase(const Entry* pos) { items_.erase(pos); }

private:
    size_t lower(const K& key) const {
        size_t lo = 0, hi = items_.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (items_[mid].first < key) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }
    ScratchVector<Entry> items_;
};

// Binary max-heap priority queue.
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
            scratchSwap(items_[parent], items_[child]);
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
            scratchSwap(items_[parent], items_[best]);
            parent = best;
        }
    }

private:
    ScratchVector<T> items_;
    Compare compare_;
};

#endif
