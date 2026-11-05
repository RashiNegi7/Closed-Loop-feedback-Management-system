// Member 4: student confirms whether staff resolved the feedback.
#include "FeedbackSystem.h"
#include <iostream>
#include <limits>
#include <string>

using namespace std;

void FeedbackSystem::studentConfirm() {
    if (records_.empty()) {
        cout << "No feedback to confirm.\n";
        return;
    }

    string studentId;
    cout << "Student ID: ";
    getline(cin, studentId);
    if (!isNumericStudentId(studentId)) {
        cout << "Student ID must contain digits only.\n";
        return;
    }
    bool found = false;
    for (const Feedback& report : records_) {
        if (report.studentId() != studentId) {
            continue;
        }
        found = true;
        cout << "\nReference: " << report.id()
             << " | Class: " << report.className()
             << " | Category: " << categoryName(report.category())
             << " | Status: " << statusName(report.status())
             << "\nFeedback: " << report.message()
             << (report.followUpComment().empty()
                     ? ""
                     : "\nYour follow-up: " + report.followUpComment())
             << "\n";
    }

    if (!found) {
        cout << "No feedback found for that student ID.\n";
        return;
    }

    cout << "Reference number to confirm or follow up (0 cancels): ";
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
    if (item == nullptr || item->studentId() != studentId) {
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        cout << "Reference not found for that student ID.\n";
        return;
    }

    if (item->status() != FeedbackStatus::WaitingForConfirmation) {
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        cout << "This feedback is not ready for confirmation.\n";
        return;
    }
    cout << "Was it solved? (1 yes, 2 no): ";
    int answer = 0;
    if (!(cin >> answer)) {
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        cout << "Enter a number.\n";
        return;
    }
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    if (answer == 1) {
        item->setStatus(FeedbackStatus::Resolved);
        cout << "Marked resolved.\n";
    } else if (answer == 2) {
        string comment;
        cout << "Add a follow-up comment for staff or Faculty: ";
        getline(cin, comment);
        if (comment.empty()) {
            cout << "Follow-up comment cannot be empty.\n";
            return;
        }
        item->setFollowUpComment(comment);
        item->setStatus(FeedbackStatus::InProgress);
        cout << "Follow-up saved and sent back for more work.\n";
    } else {
        cout << "No change made.\n";
    }
}

