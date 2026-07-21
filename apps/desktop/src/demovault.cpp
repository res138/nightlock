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

// The vault doubles as the graph-view demo: logins, passwords and URL
// domains repeat across directories on purpose, so the graph has
// clusters (per directory), bridges (shared logins), red password-
// reuse links and a couple of deliberately isolated nodes.
std::unique_ptr<nightlock::Group> createDemoVault() {
    auto root = std::make_unique<nightlock::Group>("Root");

    auto& personal2018 = root->addGroup("Personal 2018");

    auto& work = root->addGroup("Work");
    auto& meta = work.addGroup("Meta");
    auto& githubWork = work.addGroup("GitHub");
    auto& microsoft = work.addGroup("Microsoft");
    auto& ssh = microsoft.addGroup("SSH");
    auto& emails = microsoft.addGroup("Emails");
    auto& banking = microsoft.addGroup("Banking");

    auto& startup = root->addGroup("My startup");
    auto& servers = startup.addGroup("Servers");

    auto& forum = root->addGroup("Forum Accounts");
    auto& forum1 = forum.addGroup("Category 1");
    auto& forum2 = forum.addGroup("Category 2");

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

    // Personal 2018: "Old Google" repeats the 2020 Google login,
    // password and domain — the graph's one triple-match edge.
    auto& oldGoogle = personal2018.addEntry(
        makeEntry("Old Google", login, "g00gle&Key7", date(2018, 2, 3), date(2018, 9, 14)));
    oldGoogle.url = "https://mail.google.com/";
    auto& yahooLegacy = personal2018.addEntry(
        makeEntry("Yahoo Legacy", login, "yh!Old_2018", date(2018, 3, 19), date(2018, 3, 19)));
    yahooLegacy.url = "https://yahoo.com/";
    personal2018.addEntry(
        makeEntry("Steam", "night_owl_92", "sT3am#Lib_18", date(2018, 6, 30), date(2018, 12, 24)));

    // Work / Meta: one workplace identity across three services.
    auto& workplace = meta.addEntry(makeEntry("Meta Workplace", "a.horat@meta-corp.com",
                                              "wPlace#2231", date(2023, 2, 6), date(2024, 1, 15)));
    workplace.url = "https://workplace.meta.com/";
    auto& metaAds = meta.addEntry(makeEntry("Meta Ads", "a.horat@meta-corp.com", "ads$Meta_77",
                                            date(2023, 2, 6), date(2023, 11, 2)));
    metaAds.url = "https://business.meta.com/";
    auto& igStudio = meta.addEntry(makeEntry("Instagram Studio", "a.horat@meta-corp.com",
                                             "iG!studio_04", date(2023, 5, 22), date(2023, 5, 22)));
    igStudio.url = "https://business.instagram.com/";
    auto& devPortal = meta.addEntry(makeEntry("Dev Portal", "dev.team@meta-corp.com",
                                              "d3v#Portal!9", date(2023, 8, 9), date(2024, 3, 27)));
    devPortal.url = "https://developers.meta.com/";

    // Work / GitHub: same domain as the personal 2020 GitHub entry.
    auto& githubWorkAcc = githubWork.addEntry(makeEntry(
        "GitHub Work", "a-horat-dev", "gH?wrk_5512", date(2023, 1, 10), date(2024, 6, 2)));
    githubWorkAcc.url = "https://github.com/";
    auto& actionsBot = githubWork.addEntry(makeEntry(
        "GitHub Actions Bot", "ci-bot-138", "c1!bot#Rnr", date(2023, 4, 18), date(2023, 4, 18)));
    actionsBot.url = "https://github.com/";
    auto& gist = githubWork.addEntry(makeEntry("GitHub Gist", "a-horat-dev", "g1st&Note_3",
                                               date(2023, 9, 1), date(2023, 9, 1)));
    gist.url = "https://gist.github.com/";

    // Work / Microsoft: t.horat@outlook.com stitches this subtree
    // together (and reaches into Emails and Banking below).
    auto& msAccount = microsoft.addEntry(makeEntry(
        "MS Account", "t.horat@outlook.com", "0utl%k_2024", date(2022, 11, 3), date(2024, 2, 19)));
    msAccount.url = "https://account.microsoft.com/";
    auto& azure = microsoft.addEntry(makeEntry(
        "Azure Portal", "t.horat@outlook.com", "aZ#ure!5540", date(2023, 1, 25), date(2023, 10, 8)));
    azure.url = "https://portal.azure.com/";

    // Work / Microsoft / SSH: the first three boxes share one deploy
    // password — a red reuse triangle in the graph.
    ssh.addEntry(
        makeEntry("Build Server", "deploy", "Dpl0y#Key_11", date(2023, 3, 7), date(2023, 3, 7)));
    ssh.addEntry(
        makeEntry("Staging Box", "deploy", "Dpl0y#Key_11", date(2023, 3, 7), date(2023, 8, 30)));
    ssh.addEntry(
        makeEntry("Legacy VM", "deploy", "Dpl0y#Key_11", date(2021, 6, 14), date(2021, 6, 14)));
    ssh.addEntry(
        makeEntry("Jump Host", "t.horat", "jmp!H0st_88", date(2023, 5, 2), date(2024, 4, 16)));
    ssh.addEntry(
        makeEntry("Backup Node", "backup-svc", "bKp>Svc_476", date(2022, 9, 28), date(2022, 9, 28)));

    // Work / Microsoft / Emails: Old Hotmail reuses the personal
    // testmail login, bridging Work into the 2018/2020 cluster.
    auto& exchange = emails.addEntry(makeEntry(
        "Exchange", "t.horat@outlook.com", "eXch#2020!m", date(2022, 11, 3), date(2023, 6, 21)));
    exchange.url = "https://outlook.office.com/";
    auto& hotmail = emails.addEntry(makeEntry("Old Hotmail", login, "h0t*Mail_93",
                                              date(2019, 1, 8), date(2019, 1, 8)));
    hotmail.url = "https://hotmail.com/";
    emails.addEntry(makeEntry("Newsletter Relay", "relay@nightsoft.dev", "rLy!Mail_205",
                              date(2024, 2, 12), date(2024, 2, 12)));
    emails.addEntry(makeEntry("Support Inbox", "support@nightsoft.dev", "sUp?Inbox_66",
                              date(2024, 2, 12), date(2024, 7, 3)));

    // Work / Microsoft / Banking: two portals on one bank domain.
    auto& payroll = banking.addEntry(makeEntry(
        "Payroll Portal", "t.horat@outlook.com", "pAy$Roll_310", date(2023, 2, 1), date(2024, 1, 31)));
    payroll.url = "https://payroll.contoso-bank.com/";
    auto& corpCard = banking.addEntry(makeEntry(
        "Corporate Card", "t.horat@outlook.com", "cOrp&Card_45", date(2023, 2, 1), date(2023, 12, 5)));
    corpCard.url = "https://cards.contoso-bank.com/";
    banking.addEntry(makeEntry("Expenses", "138-4471-92", "eXp#Claim_08", date(2023, 7, 19),
                               date(2023, 7, 19)));

    // My startup / Servers: root+password reuse across prod, admin
    // pair on the nightsoft.dev domain, and the CI bot login linking
    // back to Work/GitHub.
    servers.addEntry(
        makeEntry("Prod API", "root", "Tr0ub4dor&3", date(2024, 1, 5), date(2024, 1, 5)));
    servers.addEntry(
        makeEntry("Prod DB", "root", "Tr0ub4dor&3", date(2024, 1, 5), date(2024, 5, 20)));
    servers.addEntry(
        makeEntry("Edge Cache", "root", "Tr0ub4dor&3", date(2024, 3, 11), date(2024, 3, 11)));
    auto& grafana = servers.addEntry(
        makeEntry("Grafana", "admin", "gRaf!Dash_12", date(2024, 2, 8), date(2024, 2, 8)));
    grafana.url = "https://grafana.nightsoft.dev/";
    auto& statusPage = servers.addEntry(
        makeEntry("Status Page", "admin", "sT@tus_Up_1", date(2024, 2, 9), date(2024, 9, 1)));
    statusPage.url = "https://status.nightsoft.dev/";
    auto& registry = servers.addEntry(
        makeEntry("Registry", "ci-bot-138", "rEg^Push_77", date(2024, 4, 2), date(2024, 4, 2)));
    registry.url = "https://registry.nightsoft.dev/";

    // Forum Accounts: night_owl_92 spans both categories (and Steam
    // in Personal 2018); the photo login pairs across categories too.
    forum1.addEntry(makeEntry("RetroPC Forum", "night_owl_92", "rEtro!PC_486", date(2019, 10, 12),
                              date(2022, 3, 8)));
    forum1.addEntry(makeEntry("Mechanical Keys", "night_owl_92", "mEch#Kbd_60%", date(2020, 8, 25),
                              date(2020, 8, 25)));
    forum1.addEntry(makeEntry("Hi-Fi Corner", "night_owl_92", "h1Fi&Tube_300B", date(2021, 2, 17),
                              date(2021, 2, 17)));
    forum1.addEntry(makeEntry("Photo Club", "owl.photo@gmail.com", "pH0to!Raw_35",
                              date(2021, 11, 4), date(2023, 5, 29)));
    forum2.addEntry(makeEntry("Space Talk", "night_owl_92", "sP@ce_Talk9", date(2020, 5, 3),
                              date(2020, 5, 3)));
    forum2.addEntry(makeEntry("Model Trains", "night_owl_92", "tR@ins_H0_x", date(2022, 12, 26),
                              date(2022, 12, 26)));
    auto& chess = forum2.addEntry(makeEntry("Chess Arena", "owl.photo@gmail.com", "cH3ss!E4_e5",
                                            date(2023, 3, 15), date(2024, 8, 11)));
    chess.url = "https://chessarena.net/";
    forum2.addEntry(makeEntry("Anon Board", "anon4471", "aN0n#Brd_%%", date(2021, 7, 7),
                              date(2021, 7, 7)));

    // Personal 2026: nothing here matches anything — these two hang
    // off their directory hub alone.
    auto& personal2026 = root->addGroup("Personal 2026");
    personal2026.addEntry(makeEntry("Passkey Vault", "me@nightlock.app", "pK!v@ult_2026",
                                    date(2026, 1, 2), date(2026, 1, 2)));
    personal2026.addEntry(makeEntry("Crypto Wallet", "cold.storage", "cW#Seed*x91",
                                    date(2026, 2, 14), date(2026, 2, 14)));

    root->addGroup("Demo Folder");

    return root;
}
