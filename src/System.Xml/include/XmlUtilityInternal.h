#pragma once
// Shared internal utility helpers.

#include "Xml.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <string_view>
#include <tuple>
#include <utility>

#if defined(_M_X64) || defined(_M_IX86)
#include <emmintrin.h>
#include <intrin.h>
#elif defined(_M_ARM64) || defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace System::Xml {

std::string BuildExceptionMessage(const std::string& message, std::size_t line, std::size_t column);
std::string ComposeQualifiedName(std::string_view prefix, std::string_view localName);
std::string ResolveNodeNamespaceUri(const XmlNode& node);
const std::string& EmptyString();
void ValidateXmlDeclarationVersion(std::string_view version);
void ValidateXmlDeclarationEncoding(std::string_view encoding);
void ValidateXmlDeclarationStandalone(std::string_view standalone);
std::string BuildDeclarationValue(
    const std::string& version,
    const std::string& encoding,
    const std::string& standalone);
std::string BuildDeclarationValue(const XmlDeclaration& declaration);

namespace {

constexpr unsigned char kAttributeValueDecoded = 0x01;
constexpr unsigned char kAttributeValueNeedsDecoding = 0x02;

bool IsWhitespace(char ch) {
    static constexpr bool kIsWhitespaceTable[256] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    return kIsWhitespaceTable[static_cast<unsigned char>(ch)];
}

bool IsNameStartChar(char ch) {
    if (static_cast<unsigned char>(ch) >= 0x80) {
        return true;
    }

    static constexpr bool kIsNameStartCharTable[256] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
        0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
        0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    return kIsNameStartCharTable[static_cast<unsigned char>(ch)];
}

bool IsNameChar(char ch) {
    if (static_cast<unsigned char>(ch) >= 0x80) {
        return true;
    }

    static constexpr bool kIsNameCharTable[256] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
        0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
        0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    return kIsNameCharTable[static_cast<unsigned char>(ch)];
}

bool IsXmlNameStartCodePoint(std::uint32_t codePoint) noexcept {
    return codePoint == ':'
        || (codePoint >= 'A' && codePoint <= 'Z')
        || codePoint == '_'
        || (codePoint >= 'a' && codePoint <= 'z')
        || (codePoint >= 0xC0 && codePoint <= 0xD6)
        || (codePoint >= 0xD8 && codePoint <= 0xF6)
        || (codePoint >= 0xF8 && codePoint <= 0x2FF)
        || (codePoint >= 0x370 && codePoint <= 0x37D)
        || (codePoint >= 0x37F && codePoint <= 0x1FFF)
        || (codePoint >= 0x200C && codePoint <= 0x200D)
        || (codePoint >= 0x2070 && codePoint <= 0x218F)
        || (codePoint >= 0x2C00 && codePoint <= 0x2FEF)
        || (codePoint >= 0x3001 && codePoint <= 0xD7FF)
        || (codePoint >= 0xF900 && codePoint <= 0xFDCF)
        || (codePoint >= 0xFDF0 && codePoint <= 0xFFFD)
        || (codePoint >= 0x10000 && codePoint <= 0xEFFFF);
}

bool IsXmlNameCodePoint(std::uint32_t codePoint) noexcept {
    return IsXmlNameStartCodePoint(codePoint)
        || codePoint == '-'
        || codePoint == '.'
        || (codePoint >= '0' && codePoint <= '9')
        || codePoint == 0xB7
        || (codePoint >= 0x0300 && codePoint <= 0x036F)
        || (codePoint >= 0x203F && codePoint <= 0x2040);
}

template <typename CharAtFn>
bool DecodeUtf8CodePointAt(std::size_t position, CharAtFn&& charAt, std::uint32_t& codePoint, std::size_t& width) noexcept {
    const auto first = static_cast<unsigned char>(charAt(position));
    if (first == 0) {
        return false;
    }

    if (first < 0x80) {
        codePoint = first;
        width = 1;
        return true;
    }

    std::uint32_t value = 0;
    std::uint32_t minValue = 0;
    if ((first & 0xE0) == 0xC0) {
        value = first & 0x1Fu;
        width = 2;
        minValue = 0x80;
    } else if ((first & 0xF0) == 0xE0) {
        value = first & 0x0Fu;
        width = 3;
        minValue = 0x800;
    } else if ((first & 0xF8) == 0xF0) {
        value = first & 0x07u;
        width = 4;
        minValue = 0x10000;
    } else {
        return false;
    }

    for (std::size_t index = 1; index < width; ++index) {
        const auto continuation = static_cast<unsigned char>(charAt(position + index));
        if (continuation == 0 || (continuation & 0xC0) != 0x80) {
            return false;
        }
        value = (value << 6) | static_cast<std::uint32_t>(continuation & 0x3Fu);
    }

    if (value < minValue || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        return false;
    }

    codePoint = value;
    return true;
}

template <typename CharAtFn>
std::size_t ConsumeXmlNameAt(std::size_t position, CharAtFn&& charAt, bool ncName = false) noexcept {
    std::uint32_t codePoint = 0;
    std::size_t width = 0;
    if (!DecodeUtf8CodePointAt(position, charAt, codePoint, width)) {
        return position;
    }

    if (!IsXmlNameStartCodePoint(codePoint) || (ncName && codePoint == ':')) {
        return position;
    }

    position += width;
    while (DecodeUtf8CodePointAt(position, charAt, codePoint, width)) {
        if (!IsXmlNameCodePoint(codePoint) || (ncName && codePoint == ':')) {
            break;
        }
        position += width;
    }

    return position;
}

template <typename CharAtFn>
std::size_t ConsumeXmlNmTokenAt(std::size_t position, CharAtFn&& charAt, bool ncName = false) noexcept {
    std::uint32_t codePoint = 0;
    std::size_t width = 0;
    std::size_t current = position;
    while (DecodeUtf8CodePointAt(current, charAt, codePoint, width)) {
        if (!IsXmlNameCodePoint(codePoint) || (ncName && codePoint == ':')) {
            break;
        }
        current += width;
    }
    return current;
}

std::size_t ConsumeNameCharsInBuffer(const char* data, std::size_t length) noexcept {
    if (data == nullptr || length == 0) {
        return 0;
    }

#if defined(_M_X64) || defined(_M_IX86)
    const __m128i colonVector = _mm_set1_epi8(':');
    const __m128i underscoreVector = _mm_set1_epi8('_');
    const __m128i minusVector = _mm_set1_epi8('-');
    const __m128i dotVector = _mm_set1_epi8('.');
    const __m128i biasVector = _mm_set1_epi8(static_cast<char>(0x80));
    const __m128i digitLow = _mm_set1_epi8(static_cast<char>(('0' - 1) ^ 0x80));
    const __m128i digitHigh = _mm_set1_epi8(static_cast<char>(('9' + 1) ^ 0x80));
    const __m128i upperLow = _mm_set1_epi8(static_cast<char>(('A' - 1) ^ 0x80));
    const __m128i upperHigh = _mm_set1_epi8(static_cast<char>(('Z' + 1) ^ 0x80));
    const __m128i lowerLow = _mm_set1_epi8(static_cast<char>(('a' - 1) ^ 0x80));
    const __m128i lowerHigh = _mm_set1_epi8(static_cast<char>(('z' + 1) ^ 0x80));

    std::size_t offset = 0;
    while (offset + 16 <= length) {
        const __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + offset));
        const __m128i biasedBlock = _mm_xor_si128(block, biasVector);

        const __m128i isColon = _mm_cmpeq_epi8(block, colonVector);
        const __m128i isUnderscore = _mm_cmpeq_epi8(block, underscoreVector);
        const __m128i isMinus = _mm_cmpeq_epi8(block, minusVector);
        const __m128i isDot = _mm_cmpeq_epi8(block, dotVector);
        const __m128i isDigit = _mm_and_si128(_mm_cmpgt_epi8(biasedBlock, digitLow), _mm_cmpgt_epi8(digitHigh, biasedBlock));
        const __m128i isUpper = _mm_and_si128(_mm_cmpgt_epi8(biasedBlock, upperLow), _mm_cmpgt_epi8(upperHigh, biasedBlock));
        const __m128i isLower = _mm_and_si128(_mm_cmpgt_epi8(biasedBlock, lowerLow), _mm_cmpgt_epi8(lowerHigh, biasedBlock));

        __m128i valid = _mm_or_si128(isColon, isUnderscore);
        valid = _mm_or_si128(valid, isMinus);
        valid = _mm_or_si128(valid, isDot);
        valid = _mm_or_si128(valid, isDigit);
        valid = _mm_or_si128(valid, isUpper);
        valid = _mm_or_si128(valid, isLower);

        const unsigned int mask = static_cast<unsigned int>(_mm_movemask_epi8(valid));
        if (mask != 0xFFFFu) {
            const unsigned int invalidMask = (~mask) & 0xFFFFu;
            unsigned long firstInvalid = 0;
            _BitScanForward(&firstInvalid, invalidMask);
            return offset + static_cast<std::size_t>(firstInvalid);
        }

        offset += 16;
    }

    for (; offset < length; ++offset) {
        if (!IsNameChar(data[offset])) {
            break;
        }
    }
    return offset;
#elif defined(_M_ARM64) || defined(__aarch64__)
    const uint8x16_t colonVector = vdupq_n_u8(static_cast<std::uint8_t>(':'));
    const uint8x16_t underscoreVector = vdupq_n_u8(static_cast<std::uint8_t>('_'));
    const uint8x16_t minusVector = vdupq_n_u8(static_cast<std::uint8_t>('-'));
    const uint8x16_t dotVector = vdupq_n_u8(static_cast<std::uint8_t>('.'));
    const uint8x16_t digitLow = vdupq_n_u8(static_cast<std::uint8_t>('0'));
    const uint8x16_t digitHigh = vdupq_n_u8(static_cast<std::uint8_t>('9'));
    const uint8x16_t upperLow = vdupq_n_u8(static_cast<std::uint8_t>('A'));
    const uint8x16_t upperHigh = vdupq_n_u8(static_cast<std::uint8_t>('Z'));
    const uint8x16_t lowerLow = vdupq_n_u8(static_cast<std::uint8_t>('a'));
    const uint8x16_t lowerHigh = vdupq_n_u8(static_cast<std::uint8_t>('z'));

    std::size_t offset = 0;
    while (offset + 16 <= length) {
        const uint8x16_t block = vld1q_u8(reinterpret_cast<const std::uint8_t*>(data + offset));

        uint8x16_t valid = vorrq_u8(vceqq_u8(block, colonVector), vceqq_u8(block, underscoreVector));
        valid = vorrq_u8(valid, vceqq_u8(block, minusVector));
        valid = vorrq_u8(valid, vceqq_u8(block, dotVector));
        valid = vorrq_u8(valid, vandq_u8(vcgeq_u8(block, digitLow), vcleq_u8(block, digitHigh)));
        valid = vorrq_u8(valid, vandq_u8(vcgeq_u8(block, upperLow), vcleq_u8(block, upperHigh)));
        valid = vorrq_u8(valid, vandq_u8(vcgeq_u8(block, lowerLow), vcleq_u8(block, lowerHigh)));

        alignas(16) std::uint8_t maskBytes[16];
        vst1q_u8(maskBytes, valid);
        for (std::size_t index = 0; index < 16; ++index) {
            if (maskBytes[index] == 0) {
                return offset + index;
            }
        }

        offset += 16;
    }

    for (; offset < length; ++offset) {
        if (!IsNameChar(data[offset])) {
            break;
        }
    }
    return offset;
#else
    std::size_t offset = 0;
    while (offset < length && IsNameChar(data[offset])) {
        ++offset;
    }
    return offset;
#endif
}

bool IsTextLike(XmlNodeType nodeType) {
    return nodeType == XmlNodeType::Text
    || nodeType == XmlNodeType::EntityReference
        || nodeType == XmlNodeType::CDATA
        || nodeType == XmlNodeType::Whitespace
        || nodeType == XmlNodeType::SignificantWhitespace;
}

const char* NodeTypeDisplayName(XmlNodeType nodeType) {
    switch (nodeType) {
    case XmlNodeType::None: return "None";
    case XmlNodeType::Element: return "Element";
    case XmlNodeType::Attribute: return "Attribute";
    case XmlNodeType::Text: return "Text";
    case XmlNodeType::CDATA: return "CDATA";
    case XmlNodeType::EntityReference: return "EntityReference";
    case XmlNodeType::Entity: return "Entity";
    case XmlNodeType::ProcessingInstruction: return "ProcessingInstruction";
    case XmlNodeType::Comment: return "Comment";
    case XmlNodeType::Document: return "Document";
    case XmlNodeType::DocumentType: return "DocumentType";
    case XmlNodeType::DocumentFragment: return "DocumentFragment";
    case XmlNodeType::Notation: return "Notation";
    case XmlNodeType::Whitespace: return "Whitespace";
    case XmlNodeType::SignificantWhitespace: return "SignificantWhitespace";
    case XmlNodeType::EndElement: return "EndElement";
    case XmlNodeType::EndEntity: return "EndEntity";
    case XmlNodeType::XmlDeclaration: return "XmlDeclaration";
    }

    return "Unknown";
}

std::string NormalizeNewLines(std::string_view value, const std::string& replacement) {
    std::string normalized;
    normalized.reserve(value.size());

    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '\r') {
            if (index + 1 < value.size() && value[index + 1] == '\n') {
                ++index;
            }
            normalized += replacement;
        } else if (value[index] == '\n') {
            normalized += replacement;
        } else {
            normalized.push_back(value[index]);
        }
    }

    return normalized;
}

std::string Trim(const std::string& value) {
    const auto start = std::find_if_not(value.begin(), value.end(), IsWhitespace);
    if (start == value.end()) {
        return {};
    }

    const auto end = std::find_if_not(value.rbegin(), value.rend(), IsWhitespace).base();
    return std::string(start, end);
}

std::pair<std::string, std::string> SplitQualifiedName(const std::string& name) {
    const auto separator = name.find(':');
    if (separator == std::string::npos) {
        return {{}, name};
    }

    return {name.substr(0, separator), name.substr(separator + 1)};
}

std::pair<std::string_view, std::string_view> SplitQualifiedNameView(std::string_view name) noexcept {
    const auto separator = name.find(':');
    if (separator == std::string_view::npos) {
        return {{}, name};
    }

    return {name.substr(0, separator), name.substr(separator + 1)};
}

bool IsValidXmlNameToken(std::string_view name) {
    if (name.empty()) {
        return false;
    }

    const auto end = ConsumeXmlNameAt(0, [name](std::size_t index) noexcept {
        return index < name.size() ? name[index] : '\0';
    });
    return end == name.size();
}

bool IsValidXmlQualifiedName(std::string_view name) {
    if (name.empty()) {
        return false;
    }

    const auto separator = name.find(':');
    if (separator == std::string_view::npos) {
        return IsValidXmlNameToken(name);
    }

    if (name.find(':', separator + 1) != std::string_view::npos) {
        return false;
    }

    return IsValidXmlNameToken(name.substr(0, separator))
        && IsValidXmlNameToken(name.substr(separator + 1));
}

bool IsNamespaceDeclarationName(const std::string& name) {
    return name == "xmlns" || name.rfind("xmlns:", 0) == 0;
}

bool IsNamespaceDeclarationName(std::string_view name) noexcept {
    return name == "xmlns" || name.rfind("xmlns:", 0) == 0;
}

std::string NamespaceDeclarationPrefix(const std::string& name) {
    if (name == "xmlns") {
        return {};
    }
    if (name.rfind("xmlns:", 0) == 0) {
        return name.substr(6);
    }
    return {};
}

std::string_view NamespaceDeclarationPrefixView(std::string_view name) noexcept {
    if (name == "xmlns") {
        return {};
    }
    if (name.rfind("xmlns:", 0) == 0) {
        return name.substr(6);
    }
    return {};
}

void ValidateNamespaceDeclarationBinding(std::string_view prefix, std::string_view namespaceUri) {
    constexpr std::string_view kXmlNamespaceUri = "http://www.w3.org/XML/1998/namespace";
    constexpr std::string_view kXmlnsNamespaceUri = "http://www.w3.org/2000/xmlns/";

    if (prefix == "xmlns") {
        throw XmlException("Invalid namespace declaration for reserved prefix 'xmlns'");
    }
    if (prefix == "xml") {
        if (namespaceUri != kXmlNamespaceUri) {
            throw XmlException("Invalid namespace declaration for reserved prefix 'xml'");
        }
        return;
    }
    if (namespaceUri == kXmlnsNamespaceUri) {
        throw XmlException("The namespace URI 'http://www.w3.org/2000/xmlns/' cannot be declared");
    }
    if (namespaceUri == kXmlNamespaceUri) {
        throw XmlException("The namespace URI 'http://www.w3.org/XML/1998/namespace' must be bound to the prefix 'xml'");
    }
}

std::string LookupPrefixOnElement(const XmlElement* element, std::string_view namespaceUri) {
    for (auto current = element; current != nullptr; ) {
        const std::string prefix = current->FindNamespaceDeclarationPrefix(namespaceUri);
        if (!prefix.empty()) {
            return prefix;
        }
        const XmlNode* parent = current->ParentNode();
        current = parent != nullptr && parent->NodeType() == XmlNodeType::Element
            ? static_cast<const XmlElement*>(parent)
            : nullptr;
    }
    if (namespaceUri == "http://www.w3.org/XML/1998/namespace") return "xml";
    if (namespaceUri == "http://www.w3.org/2000/xmlns/") return "xmlns";
    return {};
}

const char kBase64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string EncodeBase64(const unsigned char* data, std::size_t length) {
    std::string result;
    result.reserve(((length + 2) / 3) * 4);
    for (std::size_t i = 0; i < length; i += 3) {
        unsigned int triplet = static_cast<unsigned int>(data[i]) << 16;
        if (i + 1 < length) triplet |= static_cast<unsigned int>(data[i + 1]) << 8;
        if (i + 2 < length) triplet |= static_cast<unsigned int>(data[i + 2]);
        result.push_back(kBase64Chars[(triplet >> 18) & 0x3F]);
        result.push_back(kBase64Chars[(triplet >> 12) & 0x3F]);
        result.push_back(i + 1 < length ? kBase64Chars[(triplet >> 6) & 0x3F] : '=');
        result.push_back(i + 2 < length ? kBase64Chars[triplet & 0x3F] : '=');
    }
    return result;
}

int DecodeBase64Char(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

std::vector<unsigned char> DecodeBase64(const std::string& input) {
    std::vector<unsigned char> result;
    result.reserve((input.size() / 4) * 3);
    unsigned int accumulator = 0;
    int bits = 0;
    for (char ch : input) {
        if (IsWhitespace(ch) || ch == '=') continue;
        int value = DecodeBase64Char(ch);
        if (value < 0) continue;
        accumulator = (accumulator << 6) | static_cast<unsigned int>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<unsigned char>((accumulator >> bits) & 0xFF));
        }
    }
    return result;
}

void AppendDecodedBase64Chunk(std::string_view input, std::vector<unsigned char>& buffer, unsigned int& accumulator, int& bits) {
    for (char ch : input) {
        if (IsWhitespace(ch) || ch == '=') {
            continue;
        }

        const int value = DecodeBase64Char(ch);
        if (value < 0) {
            continue;
        }

        accumulator = (accumulator << 6) | static_cast<unsigned int>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            buffer.push_back(static_cast<unsigned char>((accumulator >> bits) & 0xFF));
        }
    }
}

}  // namespace

}  // namespace System::Xml
