// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#ifndef INPUTER_UNICODE_H
#define INPUTER_UNICODE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Ari only needs a small, dependency-free subset of Unicode grapheme
// segmentation for pre-edit editing. Full ICU would add a large runtime
// dependency to a small Fcitx addon. These ranges cover combining marks,
// variation selectors, emoji modifiers, tag characters, regional-indicator
// flags, and ZWJ emoji sequences; ordinary CJK/Latin text remains one cluster
// per codepoint.
namespace inputer::unicode {

struct CodePoint {
    std::uint32_t value = 0;
    std::size_t length = 1;
    bool valid = false;
};

inline bool continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

inline CodePoint decode(const std::string &text, std::size_t offset) {
    if (offset >= text.size()) {
        return {};
    }
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    if (lead < 0x80) {
        return {lead, 1, true};
    }
    if ((lead & 0xE0) == 0xC0 && offset + 1 < text.size()) {
        const unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
        if (continuation(b1)) {
            const std::uint32_t value =
                ((lead & 0x1F) << 6) | (b1 & 0x3F);
            if (value >= 0x80) {
                return {value, 2, true};
            }
        }
    }
    if ((lead & 0xF0) == 0xE0 && offset + 2 < text.size()) {
        const unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
        const unsigned char b2 = static_cast<unsigned char>(text[offset + 2]);
        if (continuation(b1) && continuation(b2)) {
            const std::uint32_t value = ((lead & 0x0F) << 12) |
                                        ((b1 & 0x3F) << 6) | (b2 & 0x3F);
            if (value >= 0x800 && !(value >= 0xD800 && value <= 0xDFFF)) {
                return {value, 3, true};
            }
        }
    }
    if ((lead & 0xF8) == 0xF0 && offset + 3 < text.size()) {
        const unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
        const unsigned char b2 = static_cast<unsigned char>(text[offset + 2]);
        const unsigned char b3 = static_cast<unsigned char>(text[offset + 3]);
        if (continuation(b1) && continuation(b2) && continuation(b3)) {
            const std::uint32_t value = ((lead & 0x07) << 18) |
                                        ((b1 & 0x3F) << 12) |
                                        ((b2 & 0x3F) << 6) | (b3 & 0x3F);
            if (value >= 0x10000 && value <= 0x10FFFF) {
                return {value, 4, true};
            }
        }
    }
    // Preserve malformed input as a one-byte cluster. The existing UTF-8
    // validation path can still reject it; this function must not loop forever
    // when a clipboard supplies an invalid byte sequence.
    return {lead, 1, false};
}

inline bool inRange(std::uint32_t value, std::uint32_t first,
                    std::uint32_t last) {
    return value >= first && value <= last;
}

inline bool isRegionalIndicator(std::uint32_t value) {
    return inRange(value, 0x1F1E6, 0x1F1FF);
}

inline bool isExtend(std::uint32_t value) {
    return inRange(value, 0x0300, 0x036F) ||
           inRange(value, 0x0483, 0x0489) ||
           inRange(value, 0x0591, 0x05BD) || value == 0x05BF ||
           inRange(value, 0x05C1, 0x05C2) || inRange(value, 0x05C4, 0x05C5) ||
           value == 0x05C7 || inRange(value, 0x0610, 0x061A) ||
           inRange(value, 0x064B, 0x065F) || value == 0x0670 ||
           inRange(value, 0x06D6, 0x06DC) || inRange(value, 0x06DF, 0x06E4) ||
           inRange(value, 0x06E7, 0x06E8) || inRange(value, 0x06EA, 0x06ED) ||
           value == 0x0711 || inRange(value, 0x0730, 0x074A) ||
           inRange(value, 0x07A6, 0x07B0) || inRange(value, 0x07EB, 0x07F3) ||
           inRange(value, 0x0816, 0x0819) || inRange(value, 0x081B, 0x0823) ||
           inRange(value, 0x0825, 0x0827) || inRange(value, 0x0829, 0x082D) ||
           inRange(value, 0x0859, 0x085B) || inRange(value, 0x08D3, 0x0903) ||
           inRange(value, 0x093A, 0x093C) || inRange(value, 0x093E, 0x094F) ||
           inRange(value, 0x0951, 0x0957) || inRange(value, 0x0962, 0x0963) ||
           inRange(value, 0x0981, 0x0984) || inRange(value, 0x09BC, 0x09BC) ||
           inRange(value, 0x09BE, 0x09CD) || inRange(value, 0x09D7, 0x09D7) ||
           inRange(value, 0x09E2, 0x09E3) || inRange(value, 0x0A01, 0x0A03) ||
           inRange(value, 0x0A3C, 0x0A3C) || inRange(value, 0x0A3E, 0x0A51) ||
           inRange(value, 0x0A70, 0x0A71) || inRange(value, 0x0A75, 0x0A75) ||
           inRange(value, 0x0ABC, 0x0ABC) || inRange(value, 0x0ABE, 0x0ACD) ||
           inRange(value, 0x0AE2, 0x0AE3) || inRange(value, 0x0B01, 0x0B03) ||
           inRange(value, 0x0B3C, 0x0B3C) || inRange(value, 0x0B3E, 0x0B57) ||
           inRange(value, 0x0B62, 0x0B63) || inRange(value, 0x0B82, 0x0B82) ||
           inRange(value, 0x0BBE, 0x0BCD) || inRange(value, 0x0BD7, 0x0BD7) ||
           inRange(value, 0x0C00, 0x0C04) || inRange(value, 0x0C3E, 0x0C56) ||
           inRange(value, 0x0C62, 0x0C63) || inRange(value, 0x0C81, 0x0C83) ||
           inRange(value, 0x0CBC, 0x0CBC) || inRange(value, 0x0CBE, 0x0CDC) ||
           inRange(value, 0x0CE2, 0x0CE3) || inRange(value, 0x0D00, 0x0D03) ||
           inRange(value, 0x0D3B, 0x0D3C) || inRange(value, 0x0D3E, 0x0D57) ||
           inRange(value, 0x0D62, 0x0D63) || inRange(value, 0x0DCA, 0x0DCA) ||
           inRange(value, 0x0DCF, 0x0DDF) || inRange(value, 0x0E31, 0x0E31) ||
           inRange(value, 0x0E34, 0x0E3A) || inRange(value, 0x0E47, 0x0E4E) ||
           inRange(value, 0x0EB1, 0x0EB1) || inRange(value, 0x0EB4, 0x0EBC) ||
           inRange(value, 0x0EC8, 0x0ECD) || inRange(value, 0x0F18, 0x0F19) ||
           inRange(value, 0x0F35, 0x0F39) || inRange(value, 0x0F71, 0x0F84) ||
           inRange(value, 0x0F86, 0x0F87) || inRange(value, 0x0F8D, 0x0FBC) ||
           inRange(value, 0x0FC6, 0x0FC6) || inRange(value, 0x102D, 0x103E) ||
           inRange(value, 0x1056, 0x1059) || inRange(value, 0x105E, 0x1060) ||
           inRange(value, 0x1071, 0x1074) || inRange(value, 0x1082, 0x1082) ||
           inRange(value, 0x1085, 0x1086) || inRange(value, 0x108D, 0x108D) ||
           inRange(value, 0x109D, 0x109D) || inRange(value, 0x135D, 0x135F) ||
           inRange(value, 0x1712, 0x1714) || inRange(value, 0x1732, 0x1734) ||
           inRange(value, 0x1752, 0x1753) || inRange(value, 0x1772, 0x1773) ||
           inRange(value, 0x17B4, 0x17D3) || inRange(value, 0x17DD, 0x17DD) ||
           inRange(value, 0x180B, 0x180F) || inRange(value, 0x1885, 0x1886) ||
           inRange(value, 0x18A9, 0x18A9) || inRange(value, 0x1920, 0x193B) ||
           inRange(value, 0x1A17, 0x1A1B) || inRange(value, 0x1A55, 0x1A7F) ||
           inRange(value, 0x1AB0, 0x1AFF) || inRange(value, 0x1B00, 0x1B04) ||
           inRange(value, 0x1B34, 0x1B44) || inRange(value, 0x1B6B, 0x1B73) ||
           inRange(value, 0x1B80, 0x1B82) || inRange(value, 0x1BA1, 0x1BAD) ||
           inRange(value, 0x1BE6, 0x1BF3) || inRange(value, 0x1C24, 0x1C37) ||
           inRange(value, 0x1CD0, 0x1CF9) || inRange(value, 0x1DC0, 0x1DFF) ||
           inRange(value, 0x20D0, 0x20FF) || inRange(value, 0x2CEF, 0x2CF1) ||
           inRange(value, 0x2D7F, 0x2D7F) || inRange(value, 0x2DE0, 0x2DFF) ||
           inRange(value, 0x302A, 0x302F) || inRange(value, 0x3099, 0x309A) ||
           inRange(value, 0xA66F, 0xA672) || inRange(value, 0xA674, 0xA67D) ||
           inRange(value, 0xA69E, 0xA69F) || inRange(value, 0xA6F0, 0xA6F1) ||
           inRange(value, 0xA802, 0xA802) || inRange(value, 0xA806, 0xA806) ||
           inRange(value, 0xA80B, 0xA80B) || inRange(value, 0xA823, 0xA827) ||
           inRange(value, 0xA880, 0xA881) || inRange(value, 0xA8B4, 0xA8C5) ||
           inRange(value, 0xA8E0, 0xA8F1) || inRange(value, 0xA926, 0xA92F) ||
           inRange(value, 0xA947, 0xA953) || inRange(value, 0xA980, 0xA983) ||
           inRange(value, 0xA9B3, 0xA9C0) || inRange(value, 0xA9E5, 0xA9E5) ||
           inRange(value, 0xAA29, 0xAA37) || inRange(value, 0xAA43, 0xAA43) ||
           inRange(value, 0xAA4C, 0xAA4D) || inRange(value, 0xAA7B, 0xAA7D) ||
           inRange(value, 0xAAB0, 0xAAB0) || inRange(value, 0xAAB2, 0xAAB4) ||
           inRange(value, 0xAAB7, 0xAAB8) || inRange(value, 0xAABE, 0xAABF) ||
           inRange(value, 0xAAC1, 0xAAC1) || inRange(value, 0xAAEC, 0xAAEF) ||
           inRange(value, 0xAAF5, 0xAAF6) || inRange(value, 0xABE3, 0xABEA) ||
           inRange(value, 0xABEC, 0xABED) || inRange(value, 0xFB1E, 0xFB1E) ||
           inRange(value, 0xFE00, 0xFE0F) || inRange(value, 0xFE20, 0xFE2F) ||
           inRange(value, 0x101FD, 0x1020E) || inRange(value, 0x10376, 0x1037A) ||
           inRange(value, 0x10A01, 0x10A0F) || inRange(value, 0x10A38, 0x10A3F) ||
           inRange(value, 0x10AE5, 0x10AE6) || inRange(value, 0x10D24, 0x10D27) ||
           inRange(value, 0x10EAB, 0x10EAC) || inRange(value, 0x10F46, 0x10F50) ||
           inRange(value, 0x11001, 0x11003) || inRange(value, 0x11038, 0x11046) ||
           inRange(value, 0x11070, 0x11070) || inRange(value, 0x11073, 0x11074) ||
           inRange(value, 0x1107F, 0x11082) || inRange(value, 0x110B0, 0x110BA) ||
           inRange(value, 0x110BD, 0x110CD) || inRange(value, 0x11100, 0x11103) ||
           inRange(value, 0x11127, 0x11134) || inRange(value, 0x11145, 0x11146) ||
           inRange(value, 0x11173, 0x11173) || inRange(value, 0x11180, 0x11182) ||
           inRange(value, 0x111B3, 0x111C0) || inRange(value, 0x111C9, 0x111CC) ||
           inRange(value, 0x111CE, 0x111CF) || inRange(value, 0x1122C, 0x11237) ||
           inRange(value, 0x1123E, 0x1123E) || inRange(value, 0x112DF, 0x112EA) ||
           inRange(value, 0x11300, 0x11303) || inRange(value, 0x1133B, 0x1133C) ||
           inRange(value, 0x1133E, 0x1134F) || inRange(value, 0x11357, 0x11357) ||
           inRange(value, 0x11362, 0x11363) || inRange(value, 0x11435, 0x11446) ||
           inRange(value, 0x114B0, 0x114C3) || inRange(value, 0x114C6, 0x114C7) ||
           inRange(value, 0x115AF, 0x115C0) || inRange(value, 0x115DC, 0x115DD) ||
           inRange(value, 0x11630, 0x11640) || inRange(value, 0x116AB, 0x116B7) ||
           inRange(value, 0x1171D, 0x1172B) || inRange(value, 0x1182C, 0x1183A) ||
           inRange(value, 0x11930, 0x1193E) || inRange(value, 0x1193F, 0x11943) ||
           inRange(value, 0x119D1, 0x119DB) || inRange(value, 0x119E0, 0x119E0) ||
           inRange(value, 0x11A01, 0x11A0A) || inRange(value, 0x11A33, 0x11A39) ||
           inRange(value, 0x11A3B, 0x11A3E) || inRange(value, 0x11A47, 0x11A47) ||
           inRange(value, 0x11A51, 0x11A5B) || inRange(value, 0x11A8A, 0x11A99) ||
           inRange(value, 0x11C30, 0x11C36) || inRange(value, 0x11C38, 0x11C3D) ||
           inRange(value, 0x11C3F, 0x11C3F) || inRange(value, 0x11C92, 0x11CA7) ||
           inRange(value, 0x11CA9, 0x11CB6) || inRange(value, 0x11D31, 0x11D36) ||
           inRange(value, 0x11D3A, 0x11D3A) || inRange(value, 0x11D3C, 0x11D3D) ||
           inRange(value, 0x11D3F, 0x11D47) || inRange(value, 0x11D90, 0x11D91) ||
           inRange(value, 0x11EF3, 0x11EF6) || inRange(value, 0x16AF0, 0x16AF4) ||
           inRange(value, 0x16B30, 0x16B36) || inRange(value, 0x16F4F, 0x16F4F) ||
           inRange(value, 0x16F8F, 0x16F92) || inRange(value, 0x1BC9D, 0x1BC9E) ||
           inRange(value, 0x1D165, 0x1D169) || inRange(value, 0x1D16D, 0x1D172) ||
           inRange(value, 0x1D17B, 0x1D182) || inRange(value, 0x1D185, 0x1D18B) ||
           inRange(value, 0x1D1AA, 0x1D1AD) || inRange(value, 0x1D242, 0x1D244) ||
           inRange(value, 0x1DA00, 0x1DA36) || inRange(value, 0x1DA3B, 0x1DA6C) ||
           inRange(value, 0x1DA75, 0x1DA75) || inRange(value, 0x1DA84, 0x1DA84) ||
           inRange(value, 0x1DA9B, 0x1DA9F) || inRange(value, 0x1DAA1, 0x1DAAF) ||
           inRange(value, 0x1E000, 0x1E02A) || inRange(value, 0x1E130, 0x1E136) ||
           inRange(value, 0x1E2EC, 0x1E2EF) || inRange(value, 0x1E8D0, 0x1E8D6) ||
           inRange(value, 0x1E944, 0x1E94A) || inRange(value, 0x1F3FB, 0x1F3FF) ||
           inRange(value, 0xE0020, 0xE007F) || inRange(value, 0xE0100, 0xE01EF);
}

inline std::vector<std::string> splitGraphemes(const std::string &text) {
    std::vector<std::string> out;
    for (std::size_t start = 0; start < text.size();) {
        std::size_t end = start;
        CodePoint first = decode(text, end);
        end += first.length;
        bool previousJoiner = false;
        int regionalCount = isRegionalIndicator(first.value) ? 1 : 0;

        while (end < text.size()) {
            const CodePoint next = decode(text, end);
            const bool pairRegional = regionalCount > 0 &&
                                      isRegionalIndicator(next.value) &&
                                      (regionalCount % 2 == 1);
            if (next.value == 0x000A && first.value == 0x000D) {
                end += next.length;
                break;
            }
            if (isExtend(next.value) || next.value == 0x200D ||
                previousJoiner || pairRegional) {
                end += next.length;
                if (pairRegional) {
                    ++regionalCount;
                }
                previousJoiner = next.value == 0x200D;
                continue;
            }
            break;
        }
        out.push_back(text.substr(start, end - start));
        start = end;
    }
    return out;
}

inline int graphemeCount(const std::string &text) {
    return static_cast<int>(splitGraphemes(text).size());
}

inline std::size_t graphemeOffset(const std::string &text, int index) {
    if (index <= 0) {
        return 0;
    }
    const auto clusters = splitGraphemes(text);
    index = std::min(index, static_cast<int>(clusters.size()));
    std::size_t offset = 0;
    for (int i = 0; i < index; ++i) {
        offset += clusters[i].size();
    }
    return offset;
}

} // namespace inputer::unicode

#endif // INPUTER_UNICODE_H
