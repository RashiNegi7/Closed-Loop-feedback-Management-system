#ifndef FEEDBACK_SYSTEM_H
#define FEEDBACK_SYSTEM_H

#include <string>
#include <new>
#include <cstddef>
#include <utility>
#include <cctype>
using namespace std;

inline bool isNumericStudentId(const string& value) {
    if (value.empty()) return false;
    for (unsigned char ch : value) {
        if (!isdigit(ch)) return false;
    }
    return true;
}

inline bool isNameWithoutDigits(const string& value) {
    if (value.empty()) return false;
    bool hasLetter = false;
    for (unsigned char ch : value) {
        if (isalpha(ch)) {
            hasLetter = true;
        } else if (ch != ' ') {
            return false;
        }
    }
    return hasLetter;
}

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

enum class Category {
    Teaching,
    Facilities,
    Portal,
    Other
};
enum class Priority {
    Low,
    Medium,
    High,
    Unassigned
};
enum class FeedbackStatus {
    New,
    InProgress,
    WaitingForConfirmation,
    Resolved
};
inline string categoryName(Category value) {
    switch (value) {
        case Category::Teaching:
            return "Teaching";
        case Category::Facilities:
            return "Facilities";
        case Category::Portal:
            return "Portal";
        case Category::Other:
            return "Other";
    }
    return "Other";
}
inline string priorityName(Priority value) {
    switch (value) {
        case Priority::Low:
            return "Low";
        case Priority::Medium:
            return "Medium";
        case Priority::High:
            return "High";
        case Priority::Unassigned:
            return "Unassigned";
    }
    return "Unassigned";
}
inline string statusName(FeedbackStatus value) {
    switch (value) {
        case FeedbackStatus::New:
            return "New";
        case FeedbackStatus::InProgress:
            return "In progress";
        case FeedbackStatus::WaitingForConfirmation:
            return "Waiting for student confirmation";
        case FeedbackStatus::Resolved:
            return "Resolved";
    }
    return "New";
}

// Encapsulation: report details are private and accessed through public methods.
class Feedback {
public:
    Feedback(int id, string studentId, string student, string className, Category category,
             Priority priority, string message)
        : id_(id), studentId_(move(studentId)), student_(move(student)), className_(move(className)),
          category_(category), priority_(priority), message_(move(message)),
          status_(FeedbackStatus::New) {}

    int id() const {
        return id_;
    }
    const string& studentId() const {
        return studentId_;
    }
    const string& student() const {
        return student_;
    }
    const string& className() const {
        return className_;
    }
    Category category() const {
        return category_;
    }
    Priority priority() const {
        return priority_;
    }
    void setPriority(Priority priority) {
        priority_ = priority;
    }
    const string& message() const {
        return message_;
    }
    const string& followUpComment() const {
        return followUpComment_;
    }
    void setFollowUpComment(string comment) {
        followUpComment_ = move(comment);
    }
    FeedbackStatus status() const {
        return status_;
    }
    void setStatus(FeedbackStatus status) {
        status_ = status;
    }

private:
    int id_;
    string studentId_;
    string student_;
    string className_;
    Category category_;
    Priority priority_;
    string message_;
    string followUpComment_;
    FeedbackStatus status_;
};

// The priority queue shows higher-priority open feedback first; ties use the older ID.
struct FeedbackPriorityOrder {
    bool operator()(const Feedback* left, const Feedback* right) const {
        auto rank = [](Priority priority) {
            switch (priority) {
                case Priority::High:
                    return 3;
                case Priority::Medium:
                    return 2;
                case Priority::Low:
                    return 1;
                case Priority::Unassigned:
                    return 0;
            }
            return 0;
        };
        if (left->priority() != right->priority()) {
            return rank(left->priority()) < rank(right->priority());
        }

        return left->id() > right->id();
    }
};

// Owns the feedback records and provides the system's operations.
class FeedbackSystem {
public:
    void submitFeedback();
    void viewFeedback() const;
    void staffUpdate();
    void facultyUpdate();
    void studentConfirm();

private:
    ScratchVector<Feedback> records_;
    int nextId_ = 1;

    Feedback* findById(int id) {
        for (Feedback& item : records_) {
            if (item.id() == id) {
                return &item;
            }
        }
        return nullptr;
    }
};

#endif
