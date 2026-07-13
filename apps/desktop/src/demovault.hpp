#pragma once

#include <memory>

namespace nightlock {
class Group;
}

// In-memory vault mirroring the Figma mockup, until the real storage lands.
std::unique_ptr<nightlock::Group> createDemoVault();
