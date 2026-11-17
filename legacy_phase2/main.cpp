// Integrated Closed Loop Feedback Management System.
#include "FeedbackSystem.h"
#include <iostream>
#include <limits>

using namespace std;

int main() {
    FeedbackSystem system;
    while (true) {
        cout << "\nClosed Loop Feedback Management System\n"
             << "1. Student\n"
             << "2. Faculty\n"
             << "3. Staff\n"
             << "0. Exit\n"
             << "Choose role: ";
        int role = 0;
        if (!(cin >> role)) {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            cout << "Enter a number.\n";
            continue;
        }
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        if (role == 0) {
            break;
        }
        if (role < 1 || role > 3) {
            cout << "Choose one of the listed roles.\n";
            continue;
        }

        bool switchRole = false;
        while (!switchRole) {
            int action = 0;
            if (role == 1) {
                cout << "\nStudent\n"
                     << "1. Submit feedback\n"
                     << "2. View my feedback / confirm resolution\n"
                     << "0. Switch role\n"
                     << "9. Exit\n"
                     << "Choose: ";
            } else if (role == 2) {
                cout << "\nFaculty\n"
                     << "1. View and update teaching feedback\n"
                     << "0. Switch role\n"
                     << "9. Exit\n"
                     << "Choose: ";
            } else {
                cout << "\nStaff\n"
                     << "1. View feedback and summary\n"
                     << "2. Update feedback\n"
                     << "0. Switch role\n"
                     << "9. Exit\n"
                     << "Choose: ";
            }
            if (!(cin >> action)) {
                cin.clear();
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                cout << "Enter a number.\n";
                continue;
            }
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            if (action == 9) {
                switchRole = true;
                role = 0;
                break;
            }

            if (action == 0) {
                switchRole = true;
                continue;
            }

            if (role == 1 && action == 1) {
                system.submitFeedback();
            } else if (role == 1 && action == 2) {
                system.studentConfirm();
            } else if (role == 2 && action == 1) {
                system.facultyUpdate();
            } else if (role == 3 && action == 1) {
                system.viewFeedback();
            } else if (role == 3 && action == 2) {
                system.staffUpdate();
            } else {
                cout << "Choose one of the listed actions.\n";
            }
        }
        if (role == 0) break;
    }
    cout << "Goodbye. Data is cleared when the program exits.\n";
    return 0;
}
