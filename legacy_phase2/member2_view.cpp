// Member 2: view feedback, show status summaries, and order open feedback by priority.
#include "FeedbackSystem.h"
#include <iostream>

using namespace std;

namespace {
    void printFeedback(const Feedback& item) {
        cout << "\nReference: " << item.id()
             << " | Class: " << item.className()
             << " | Category: " << categoryName(item.category())
             << " | Priority: " << priorityName(item.priority())
             << "\nFeedback: " << item.message()
             << (item.followUpComment().empty()
                     ? ""
                     : "\nStudent follow-up: " + item.followUpComment())
             << "\nStatus: " << statusName(item.status()) << "\n";
    }
}

void FeedbackSystem::viewFeedback() const {
    if (records_.empty()) {
        cout << "No feedback has been submitted yet.\n";
        return;
    }

    int newCount = 0, progressCount = 0, waitingCount = 0, resolvedCount = 0;
    ScratchPriorityQueue<const Feedback*, FeedbackPriorityOrder> openQueue;
    for (const Feedback& item : records_) {
        switch (item.status()) {
            case FeedbackStatus::New:
                ++newCount;
                break;
            case FeedbackStatus::InProgress:
                ++progressCount;
                break;
            case FeedbackStatus::WaitingForConfirmation:
                ++waitingCount;
                break;
            case FeedbackStatus::Resolved:
                ++resolvedCount;
                break;
        }
        if (item.status() != FeedbackStatus::Resolved) {
            openQueue.push(&item);
        }
    }
    cout << "Open feedback (highest priority first):\n";
    while (!openQueue.empty()) {
        printFeedback(*openQueue.top());
        openQueue.pop();
    }
    if (resolvedCount > 0) {
        cout << "\nResolved feedback:\n";
        for (const Feedback& item : records_) {
            if (item.status() == FeedbackStatus::Resolved) {
                printFeedback(item);
            }
        }
    }
    cout << "\nSummary: New " << newCount
         << " | In progress " << progressCount
         << " | Waiting for confirmation " << waitingCount
         << " | Resolved " << resolvedCount << "\n";
}


