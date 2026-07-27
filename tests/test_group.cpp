#include <doctest/doctest.h>

#include <nightlock/group.hpp>

// Pins the Group tree behavior the desktop app already depends on, so
// the serializer and vault work can build on it without surprises.

using nightlock::Entry;
using nightlock::Group;

namespace {

Entry makeEntry(std::string name) {
    Entry e;
    e.name = std::move(name);
    return e;
}

}  // namespace

TEST_CASE("addGroup wires parent and order") {
    Group root("Root");
    Group& a = root.addGroup("A");
    Group& b = root.addGroup("B");

    CHECK(a.parent() == &root);
    CHECK(b.parent() == &root);
    REQUIRE(root.groups().size() == 2);
    CHECK(root.groups()[0].get() == &a);
    CHECK(root.groups()[1].get() == &b);
    CHECK(a.indexInParent() == 0);
    CHECK(b.indexInParent() == 1);
}

TEST_CASE("removeGroup only removes direct children") {
    Group root("Root");
    Group& a = root.addGroup("A");
    Group& nested = a.addGroup("Nested");

    CHECK_FALSE(root.removeGroup(&nested));  // grandchild, not direct
    CHECK(root.removeGroup(&a));             // destroys the subtree
    CHECK(root.groups().empty());
}

TEST_CASE("removeEntry only removes own entries") {
    Group root("Root");
    Group& other = root.addGroup("Other");
    Entry& e = root.addEntry(makeEntry("GitHub"));

    CHECK_FALSE(other.removeEntry(&e));
    CHECK(root.removeEntry(&e));
    CHECK(root.entries().empty());
}

TEST_CASE("moveEntry uses pre-removal indices") {
    Group root("Root");
    root.addEntry(makeEntry("0"));
    root.addEntry(makeEntry("1"));
    root.addEntry(makeEntry("2"));

    SUBCASE("no-op moves fail") {
        CHECK_FALSE(root.moveEntry(1, 1));      // onto itself
        CHECK_FALSE(root.moveEntry(1, 2));      // right before its next slot
        CHECK_FALSE(root.moveEntry(-1, 0));     // out of range
        CHECK_FALSE(root.moveEntry(3, 0));
    }

    SUBCASE("move forward") {
        CHECK(root.moveEntry(0, 3));
        CHECK(root.entries()[0]->name == "1");
        CHECK(root.entries()[1]->name == "2");
        CHECK(root.entries()[2]->name == "0");
    }

    SUBCASE("move backward") {
        CHECK(root.moveEntry(2, 0));
        CHECK(root.entries()[0]->name == "2");
        CHECK(root.entries()[1]->name == "0");
        CHECK(root.entries()[2]->name == "1");
    }

    SUBCASE("negative target clamps to the end") {
        // to < 0 clamps to count; from == count-1 then hits the
        // to == from + 1 no-op, so use the first entry.
        CHECK(root.moveEntry(0, -5));
        CHECK(root.entries()[2]->name == "0");
    }
}

TEST_CASE("transferEntry keeps the object alive and appends") {
    Group root("Root");
    Group& target = root.addGroup("Target");
    Entry& e = root.addEntry(makeEntry("Moved"));
    Entry* address = &e;

    SUBCASE("same group is a no-op success") {
        CHECK(root.transferEntry(&e, root));
        CHECK(root.entries().size() == 1);
    }

    SUBCASE("moves to the end of target") {
        target.addEntry(makeEntry("Existing"));
        CHECK(root.transferEntry(&e, target));
        CHECK(root.entries().empty());
        REQUIRE(target.entries().size() == 2);
        CHECK(target.entries()[1].get() == address);  // pointer stability
    }

    SUBCASE("fails for foreign entries") {
        CHECK_FALSE(target.transferEntry(&e, root));
    }
}

TEST_CASE("path joins names from the root") {
    Group root("Root");
    Group& a = root.addGroup("Work");
    Group& b = a.addGroup("Banking");

    CHECK(root.path() == "Root");
    CHECK(b.path() == "Root/Work/Banking");
    CHECK(b.path('>') == "Root>Work>Banking");
}

TEST_CASE("isAncestorOf walks the parent chain") {
    Group root("Root");
    Group& a = root.addGroup("A");
    Group& b = a.addGroup("B");

    CHECK(root.isAncestorOf(&b));
    CHECK(a.isAncestorOf(&b));
    CHECK_FALSE(b.isAncestorOf(&a));
    CHECK_FALSE(a.isAncestorOf(&a));
    CHECK_FALSE(root.isAncestorOf(nullptr));
}

TEST_CASE("reparent moves subtrees and rejects cycles") {
    Group root("Root");
    Group& a = root.addGroup("A");
    Group& b = root.addGroup("B");
    Group& nested = a.addGroup("Nested");

    SUBCASE("rejects the root, self and descendants") {
        CHECK_FALSE(Group::reparent(root, a));     // root has no parent
        CHECK_FALSE(Group::reparent(a, a));        // onto itself
        CHECK_FALSE(Group::reparent(a, nested));   // into own subtree
    }

    SUBCASE("moves across parents, clamped to the end") {
        CHECK(Group::reparent(nested, b, 99));
        CHECK(a.groups().empty());
        REQUIRE(b.groups().size() == 1);
        CHECK(b.groups()[0].get() == &nested);
        CHECK(nested.parent() == &b);
    }

    SUBCASE("same-parent move adjusts the index past the hole") {
        Group& c = root.addGroup("C");
        // Order: A B C. Move A to pre-removal position 3 (after C).
        CHECK(Group::reparent(a, root, 3));
        CHECK(root.groups()[0].get() == &b);
        CHECK(root.groups()[1].get() == &c);
        CHECK(root.groups()[2].get() == &a);
    }
}
