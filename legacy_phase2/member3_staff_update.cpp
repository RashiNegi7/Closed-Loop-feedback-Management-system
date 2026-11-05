// Member 3: Staff and Faculty assign priorities and move feedback through the work stages.
#include "FeedbackSystem.h"
#include <iostream>
#include <limits>

using namespace std;

void FeedbackSystem::staffUpdate() {
    if (records_.empty()) {
        cout << "No feedback to update.\n";
        return;
    }

    viewFeedback();
    cout << "Reference number to update (0 cancels): ";
    int id = 0;
    if (!(cin >> id)) {
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        cout << "Enter a number.\n";
        return;
    }

    if (id == 0) {
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        return;
    }

    Feedback* item = findById(id);
    if (item == nullptr) {
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        cout << "Reference not found.\n";
        return;
    }

    if (item->category() == Category::Teaching) {
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        cout << "Teaching feedback is handled by Faculty.\n";
        return;
    }
    cout << "Assign priority (1 Low, 2 Medium, 3 High): ";
    int priority = 0;
    if (!(cin >> priority) || priority < 1 || priority > 3) {
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        cout << "Choose a priority from 1 to 3.\n";
        return;
    }
    item->setPriority(static_cast<Priority>(priority - 1));
    cout << "1. In progress\n2. Ready for student confirmation\nChoose: ";
    int choice = 0;
    if (!(cin >> choice)) {
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        cout << "Enter a number.\n";
        return;
    }

    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    if (choice == 1) {
        item->setStatus(FeedbackStatus::InProgress);
    } else if (choice == 2) {
        item->setStatus(FeedbackStatus::WaitingForConfirmation);
    } else {
        cout << "No change made.\n";
        return;
    }

    cout << "Status updated.\n";
}

void FeedbackSystem::facultyUpdate() {
    bool foundTeaching = false;
    for (const Feedback& item : records_) {
        if (item.category() != Category::Teaching) {
            continue;
        }
        foundTeaching = true;
        cout << "Reference: " << item.id()
             << " | Class: " << item.className()
             << " | Priority: " << priorityName(item.priority())
             << " | Status: " << statusName(item.status())
             << "\nFeedback: " << item.message()
             << (item.followUpComment().empty()
                     ? ""
                     : "\nStudent follow-up: " + item.followUpComment())
             << "\n";
    }
    if (!foundTeaching) {
        cout << "No teaching feedback for Faculty.\n";
        return;
    }
    cout << "Teaching feedback reference to update (0 cancels): ";
    int id = 0;
    if (!(cin >> id)) {
        cin.clear();
        cin.ignore(10000, '\n');
        cout << "Enter a number.\n";
        return;
    }
    if (id == 0) {
        cin.ignore(10000, '\n');
        return;
    }
    Feedback* item = findById(id);
    if (item == nullptr || item->category() != Category::Teaching) {
        cin.ignore(10000, '\n');
        cout << "Teaching feedback reference not found.\n";
        return;
    }
    cout << "Assign priority (1 Low, 2 Medium, 3 High): ";
    int priority = 0;
    if (!(cin >> priority) || priority < 1 || priority > 3) {
        cin.clear();
        cin.ignore(10000, '\n');
        cout << "Choose a priority from 1 to 3.\n";
        return;
    }
    item->setPriority(static_cast<Priority>(priority - 1));
    cout << "1. Mark in progress\n2. Confirm work is ready for student feedback\nChoose: ";
    int choice = 0;
    if (!(cin >> choice)) {
        cin.clear();
        cin.ignore(10000, '\n');
        cout << "Enter a number.\n";
        return;
    }
    cin.ignore(10000, '\n');
    if (choice == 1) {
        item->setStatus(FeedbackStatus::InProgress);
    } else if (choice == 2) {
        item->setStatus(FeedbackStatus::WaitingForConfirmation);
    } else {
        cout << "No change made.\n";
        return;
    }

    cout << "Faculty update saved.\n";
}

