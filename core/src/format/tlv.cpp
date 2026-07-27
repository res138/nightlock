#include "format/tlv.hpp"

#include <limits>

namespace nightlock::format {

void putU16(secure::Bytes& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void putU32(secure::Bytes& out, std::uint32_t v) {
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::uint8_t>(v >> shift));
}

void putU64(secure::Bytes& out, std::uint64_t v) {
    for (int shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<std::uint8_t>(v >> shift));
}

std::uint16_t getU16(std::span<const std::uint8_t> in) noexcept {
    return static_cast<std::uint16_t>(in[0] | in[1] << 8);
}

std::uint32_t getU32(std::span<const std::uint8_t> in) noexcept {
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i)
        v = v << 8 | in[static_cast<std::size_t>(i)];
    return v;
}

std::uint64_t getU64(std::span<const std::uint8_t> in) noexcept {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = v << 8 | in[static_cast<std::size_t>(i)];
    return v;
}

void TlvWriter::bytes(std::uint16_t tag, std::span<const std::uint8_t> value) {
    putU16(out_, tag);
    putU32(out_, static_cast<std::uint32_t>(value.size()));
    out_.insert(out_.end(), value.begin(), value.end());
}

void TlvWriter::string(std::uint16_t tag, std::string_view value) {
    bytes(tag, {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
}

void TlvWriter::u32(std::uint16_t tag, std::uint32_t value) {
    putU16(out_, tag);
    putU32(out_, 4);
    putU32(out_, value);
}

void TlvWriter::i64(std::uint16_t tag, std::int64_t value) {
    putU16(out_, tag);
    putU32(out_, 8);
    putU64(out_, static_cast<std::uint64_t>(value));
}

std::size_t TlvWriter::beginContainer(std::uint16_t tag) {
    putU16(out_, tag);
    const std::size_t token = out_.size();
    putU32(out_, 0);  // patched by endContainer
    return token;
}

void TlvWriter::endContainer(std::size_t token) {
    const std::size_t length = out_.size() - token - 4;
    for (int i = 0; i < 4; ++i)
        out_[token + static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(length >> 8 * i);
}

bool TlvReader::next() noexcept {
    if (malformed_ || cursor_ >= data_.size())
        return false;
    if (data_.size() - cursor_ < kRecordHeadBytes) {
        malformed_ = true;
        return false;
    }
    tag_ = getU16(data_.subspan(cursor_));
    const std::uint32_t length = getU32(data_.subspan(cursor_ + 2));
    cursor_ += kRecordHeadBytes;
    if (length > data_.size() - cursor_) {
        malformed_ = true;
        return false;
    }
    value_ = data_.subspan(cursor_, length);
    cursor_ += length;
    return true;
}

std::string_view TlvReader::valueString() const noexcept {
    return {reinterpret_cast<const char*>(value_.data()), value_.size()};
}

std::optional<std::uint32_t> TlvReader::valueU32() const noexcept {
    if (value_.size() != 4)
        return std::nullopt;
    return getU32(value_);
}

std::optional<std::int64_t> TlvReader::valueI64() const noexcept {
    if (value_.size() != 8)
        return std::nullopt;
    return static_cast<std::int64_t>(getU64(value_));
}

}  // namespace nightlock::format
