#include "demovault.hpp"

#include <chrono>

#include <nightlock/group.hpp>

using namespace std::chrono;

namespace {

system_clock::time_point date(int y, unsigned m, unsigned d) {
    return sys_days{year{y} / month{m} / day{d}};
}

nightlock::Entry makeEntry(std::string name, std::string login, std::string password,
                           system_clock::time_point created,
                           system_clock::time_point modified) {
    nightlock::Entry e;
    e.name = std::move(name);
    e.login = std::move(login);
    e.password = std::move(password);
    e.created = created;
    e.modified = modified;
    return e;
}

}  // namespace

std::unique_ptr<nightlock::Group> createDemoVault() {
    auto root = std::make_unique<nightlock::Group>("Root");

    root->addGroup("Personal 2018");

    auto& work = root->addGroup("Work");
    work.addGroup("Meta");
    work.addGroup("GitHub");
    auto& microsoft = work.addGroup("Microsoft");
    microsoft.addGroup("SSH");
    microsoft.addGroup("Emails");
    microsoft.addGroup("Banking");

    auto& startup = root->addGroup("My startup");
    startup.addGroup("Servers");

    auto& forum = root->addGroup("Forum Accounts");
    forum.addGroup("Category 1");
    forum.addGroup("Category 2");

    auto& personal2020 = root->addGroup("Personal 2020");

    const auto login = std::string("testmail@yahoo.com");

    personal2020.addEntry(
        makeEntry("Entry 1", login, "u8#kPz!m24Q", date(2020, 1, 12), date(2020, 2, 3)));
    personal2020.addEntry(
        makeEntry("MT Access", login, "mT-9wLx#51v", date(2020, 1, 30), date(2020, 4, 11)));
    personal2020.addEntry(
        makeEntry("Office", login, "0ff1ce!Pass", date(2020, 2, 14), date(2020, 2, 14)));

    auto& google = personal2020.addEntry(
        makeEntry("Google", login, "g00gle&Key7", date(2020, 3, 2), date(2020, 6, 20)));
    google.url = "https://accounts.google.com/";

    auto& github = personal2020.addEntry(
        makeEntry("GitHub", login, "gH-repo$2020x8", date(2020, 3, 27), date(2020, 5, 18)));
    github.url = "https://github.com/";
    github.code = "803 059";
    github.note = "primary account for scientific research";

    personal2020.addEntry(
        makeEntry("Meta", login, "mEta*4290Lq", date(2020, 4, 5), date(2020, 4, 5)));

    auto& yahoo = personal2020.addEntry(
        makeEntry("Yahoo", login, "yH!mail_733", date(2020, 4, 22), date(2020, 7, 1)));
    yahoo.url = "https://mail.yahoo.com/";

    personal2020.addEntry(
        makeEntry("Entry 2", login, "eNtry2#pass", date(2020, 5, 9), date(2020, 5, 9)));
    personal2020.addEntry(
        makeEntry("Entry 3", login, "eNtry3#pass", date(2020, 6, 17), date(2020, 6, 17)));

    root->addGroup("Personal 2026");
    root->addGroup("Demo Folder");

    return root;
}
