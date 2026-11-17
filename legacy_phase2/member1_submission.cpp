// Member 1: feedback submission, category, and priority.
#include "FeedbackSystem.h"
#include <iostream>
#include <limits>
#include <string>
using namespace std;

void FeedbackSystem::submitFeedback() {
    string studentId, student, className, message;
    cout << "Student ID: "; getline(cin, studentId);
    cout << "Student name: "; getline(cin, student);
    cout << "Class: "; getline(cin, className);
    cout << "Category (1 Teaching, 2 Facilities, 3 Portal, 4 Other): ";
    int category = 0;
    if (!(cin >> category) || category < 1 || category > 4) {
        cin.clear(); cin.ignore(numeric_limits<streamsize>::max(), '\n');
        cout << "Choose a category from 1 to 4.\n"; return;
    }
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    cout << "Feedback: "; getline(cin, message);
    if (!isNumericStudentId(studentId)) {
        cout << "Student ID must contain digits only.\n"; return;
    }
    if (!isNameWithoutDigits(student)) {
        cout << "Student name must contain letters and cannot contain numbers.\n"; return;
    }
    if (className.empty() || message.empty()) {
        cout << "Please fill in all fields.\n"; return;
    }
    records_.emplace_back(nextId_, studentId, student, className,
        static_cast<Category>(category - 1), Priority::Unassigned, message);
    cout << "Feedback saved. Reference number: " << nextId_++ << "\n";
}
