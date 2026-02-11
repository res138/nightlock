#include <cstring>
#include <iostream>
#include "Manager.h"

int main() {
    Manager manager;
    manager.root_set_password("Google Account", "admin123123", "admin@gmail.com", "https://accounts.google.com/", "Primary Account");
    manager.root_set_wifi("home2c", "987328455728");
    manager.root_get_specific_entry<Password>("Google Account");

}
