#include "format/serialize.hpp"

#include <chrono>
#include <string>

#include "format/tlv.hpp"
#include "nightlock/entry.hpp"

namespace nightlock::format {

namespace {

using std::chrono::milliseconds;
using std::chrono::system_clock;
using std::chrono::time_point_cast;

std::int64_t toMs(system_clock::time_point tp) {
    return time_point_cast<milliseconds>(tp).time_since_epoch().count();
}

system_clock::time_point fromMs(std::int64_t ms) {
    return system_clock::time_point(milliseconds(ms));
}

void writeEntry(TlvWriter& w, const Entry& entry) {
    const std::size_t token = w.beginContainer(kTagEntry);
    if (!entry.name.empty())
        w.string(kTagEntryName, entry.name);
    if (!entry.login.empty())
        w.string(kTagEntryLogin, entry.login);
    if (!entry.password.empty())
        w.string(kTagEntryPassword, secure::view(entry.password));
    w.i64(kTagEntryCreatedMs, toMs(entry.created));
    w.i64(kTagEntryModifiedMs, toMs(entry.modified));
    if (!entry.url.empty())
        w.string(kTagEntryUrl, entry.url);
    if (!entry.icon.empty())
        w.string(kTagEntryIcon, entry.icon);
    if (!entry.note.empty())
        w.string(kTagEntryNote, entry.note);
    if (!entry.code.empty())
        w.string(kTagEntryCode, secure::view(entry.code));
    if (entry.pattern != Pattern::None)
        w.u32(kTagEntryPattern, static_cast<std::uint32_t>(entry.pattern));
    if (entry.preset != EntryPreset::Classic)
        w.u32(kTagEntryPreset, static_cast<std::uint32_t>(entry.preset));
    for (const EntryField& field : entry.fields) {
        if (field.label.empty())
            continue;
        const std::size_t fieldToken = w.beginContainer(kTagEntryField);
        w.string(kTagFieldLabel, field.label);
        if (!field.value.empty())
            w.string(kTagFieldValue, secure::view(field.value));
        if (field.secret)
            w.u32(kTagFieldSecret, 1);
        if (field.custom)
            w.u32(kTagFieldCustom, 1);
        w.endContainer(fieldToken);
    }
    w.endContainer(token);
}

void writeGroup(TlvWriter& w, const Group& group) {
    const std::size_t token = w.beginContainer(kTagGroup);
    if (!group.name().empty())
        w.string(kTagGroupName, group.name());
    if (!group.icon().empty())
        w.string(kTagGroupIcon, group.icon());
    for (const auto& child : group.groups())
        writeGroup(w, *child);
    for (const auto& entry : group.entries())
        writeEntry(w, *entry);
    w.endContainer(token);
}

ParseResult readField(TlvReader records, Entry& entry) {
    EntryField field;
    while (records.next()) {
        switch (records.tag()) {
            case kTagFieldLabel:
                field.label = std::string(records.valueString());
                break;
            case kTagFieldValue:
                field.value.assign(records.valueString());
                break;
            case kTagFieldSecret: {
                const auto value = records.valueU32();
                if (!value)
                    return ParseResult::Malformed;
                field.secret = *value != 0;
                break;
            }
            case kTagFieldCustom: {
                const auto value = records.valueU32();
                if (!value)
                    return ParseResult::Malformed;
                field.custom = *value != 0;
                break;
            }
            default:
                if (records.tag() & kCriticalTagBit)
                    return ParseResult::Unsupported;
                break;
        }
    }
    if (records.malformed())
        return ParseResult::Malformed;
    if (!field.label.empty())
        entry.fields.push_back(std::move(field));
    return ParseResult::Ok;
}

ParseResult readEntry(TlvReader records, Group& parent) {
    Entry entry;
    while (records.next()) {
        switch (records.tag()) {
            case kTagEntryName:
                entry.name = std::string(records.valueString());
                break;
            case kTagEntryLogin:
                entry.login = std::string(records.valueString());
                break;
            case kTagEntryPassword:
                entry.password.assign(records.valueString());
                break;
            case kTagEntryCreatedMs: {
                const auto ms = records.valueI64();
                if (!ms)
                    return ParseResult::Malformed;
                entry.created = fromMs(*ms);
                break;
            }
            case kTagEntryModifiedMs: {
                const auto ms = records.valueI64();
                if (!ms)
                    return ParseResult::Malformed;
                entry.modified = fromMs(*ms);
                break;
            }
            case kTagEntryUrl:
                entry.url = std::string(records.valueString());
                break;
            case kTagEntryIcon:
                entry.icon = std::string(records.valueString());
                break;
            case kTagEntryNote:
                entry.note = std::string(records.valueString());
                break;
            case kTagEntryCode:
                entry.code.assign(records.valueString());
                break;
            case kTagEntryPattern: {
                const auto value = records.valueU32();
                if (!value)
                    return ParseResult::Malformed;
                // Unknown future patterns degrade to None instead of
                // poisoning the enum.
                entry.pattern = *value <= static_cast<std::uint32_t>(Pattern::Halo)
                                    ? static_cast<Pattern>(*value)
                                    : Pattern::None;
                break;
            }
            case kTagEntryPreset: {
                const auto value = records.valueU32();
                if (!value)
                    return ParseResult::Malformed;
                entry.preset = *value <= static_cast<std::uint32_t>(EntryPreset::CryptoWallet)
                                   ? static_cast<EntryPreset>(*value)
                                   : EntryPreset::Classic;
                break;
            }
            case kTagEntryField: {
                const ParseResult field = readField(records.container(), entry);
                if (field != ParseResult::Ok)
                    return field;
                break;
            }
            default:
                if (records.tag() & kCriticalTagBit)
                    return ParseResult::Unsupported;
                break;  // unknown non-critical: skip
        }
    }
    if (records.malformed())
        return ParseResult::Malformed;
    parent.addEntry(std::move(entry));
    return ParseResult::Ok;
}

ParseResult readGroup(TlvReader records, Group& group) {
    while (records.next()) {
        switch (records.tag()) {
            case kTagGroupName:
                group.setName(std::string(records.valueString()));
                break;
            case kTagGroupIcon:
                group.setIcon(std::string(records.valueString()));
                break;
            case kTagGroup: {
                const ParseResult child =
                    readGroup(records.container(), group.addGroup(""));
                if (child != ParseResult::Ok)
                    return child;
                break;
            }
            case kTagEntry: {
                const ParseResult entry = readEntry(records.container(), group);
                if (entry != ParseResult::Ok)
                    return entry;
                break;
            }
            default:
                if (records.tag() & kCriticalTagBit)
                    return ParseResult::Unsupported;
                break;
        }
    }
    return records.malformed() ? ParseResult::Malformed : ParseResult::Ok;
}

ParseResult readMeta(TlvReader records) {
    bool sawVersion = false;
    while (records.next()) {
        if (records.tag() == kTagPayloadVersion) {
            const auto version = records.valueU32();
            if (!version)
                return ParseResult::Malformed;
            if (*version > kPayloadVersion)
                return ParseResult::Unsupported;
            sawVersion = true;
        } else if (records.tag() & kCriticalTagBit) {
            return ParseResult::Unsupported;
        }
        // VaultName/SavedAt are informational; nothing to rebuild.
    }
    if (records.malformed() || !sawVersion)
        return ParseResult::Malformed;
    return ParseResult::Ok;
}

}  // namespace

void serializeVault(const Group& root, secure::Bytes& out) {
    TlvWriter w(out);
    const std::size_t meta = w.beginContainer(kTagMeta);
    w.u32(kTagPayloadVersion, kPayloadVersion);
    w.string(kTagVaultName, root.name());
    w.i64(kTagSavedAt, toMs(system_clock::now()));
    w.endContainer(meta);
    writeGroup(w, root);
}

ParseResult deserializeVault(std::span<const std::uint8_t> payload,
                             std::unique_ptr<Group>& rootOut) {
    TlvReader records(payload);
    bool sawMeta = false;
    std::unique_ptr<Group> root;
    while (records.next()) {
        switch (records.tag()) {
            case kTagMeta: {
                const ParseResult meta = readMeta(records.container());
                if (meta != ParseResult::Ok)
                    return meta;
                sawMeta = true;
                break;
            }
            case kTagGroup: {
                if (root)
                    return ParseResult::Malformed;  // exactly one root
                root = std::make_unique<Group>("");
                const ParseResult result = readGroup(records.container(), *root);
                if (result != ParseResult::Ok)
                    return result;
                break;
            }
            default:
                if (records.tag() & kCriticalTagBit)
                    return ParseResult::Unsupported;
                break;
        }
    }
    if (records.malformed() || !sawMeta || !root)
        return ParseResult::Malformed;
    rootOut = std::move(root);
    return ParseResult::Ok;
}

}  // namespace nightlock::format
