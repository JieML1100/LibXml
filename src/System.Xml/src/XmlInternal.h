#pragma once
// Internal shared implementation helpers.
// All helpers live in an anonymous namespace for internal linkage.

#include "Xml.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
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
#endif

namespace System::Xml {

void ValidateXmlReaderInputAgainstSchemas(const std::string& xml, const XmlReaderSettings& settings);
void ValidateXmlReaderInputAgainstSchemas(XmlReader& reader, const XmlReaderSettings& settings);

namespace {


constexpr unsigned char kAttributeValueDecoded = 0x01;
constexpr unsigned char kAttributeValueNeedsDecoding = 0x02;

std::string BuildExceptionMessage(const std::string& message, std::size_t line, std::size_t column) {
    if (line == 0 || column == 0) {
        return message;
    }

    std::ostringstream stream;
    stream << message << " (line " << line << ", column " << column << ')';
    return stream.str();
}

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

std::string NormalizeNewLines(const std::string& value, const std::string& replacement) {
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

enum class XmlMarkupKind {
    None = 0,
    Element,
    EndTag,
    Comment,
    CData,
    DocumentType,
    XmlDeclaration,
    ProcessingInstruction,
    UnsupportedDeclaration
};

XmlMarkupKind ClassifyXmlMarkupPrefix(const char* prefix, std::size_t available) noexcept {
    if (prefix == nullptr || available == 0 || prefix[0] != '<') {
        return XmlMarkupKind::None;
    }

    if (available < 2) {
        return XmlMarkupKind::None;
    }

    const char ch1 = prefix[1];
    if (ch1 == '?') {
        if (available >= 3 && prefix[2] != 'x') {
            return XmlMarkupKind::ProcessingInstruction;
        }

        if (available >= 4 && prefix[2] == 'x' && prefix[3] != 'm') {
            return XmlMarkupKind::ProcessingInstruction;
        }

        if (available < 5) {
            return XmlMarkupKind::None;
        }

        if (prefix[2] == 'x' && prefix[3] == 'm' && prefix[4] == 'l') {
            return XmlMarkupKind::XmlDeclaration;
        }

        return XmlMarkupKind::ProcessingInstruction;
    }

    if (ch1 == '!') {
        if (available < 3) {
            return XmlMarkupKind::None;
        }

        const char ch2 = prefix[2];
        if (ch2 == '-') {
            if (available < 4) {
                return XmlMarkupKind::None;
            }
            return prefix[3] == '-' ? XmlMarkupKind::Comment : XmlMarkupKind::UnsupportedDeclaration;
        }

        if (ch2 == '[') {
            if (available < 9) {
                return XmlMarkupKind::None;
            }
            return prefix[3] == 'C' && prefix[4] == 'D' && prefix[5] == 'A'
                && prefix[6] == 'T' && prefix[7] == 'A' && prefix[8] == '['
                ? XmlMarkupKind::CData
                : XmlMarkupKind::UnsupportedDeclaration;
        }

        if (ch2 == 'D') {
            if (available < 9) {
                return XmlMarkupKind::None;
            }
            return prefix[3] == 'O' && prefix[4] == 'C' && prefix[5] == 'T'
                && prefix[6] == 'Y' && prefix[7] == 'P' && prefix[8] == 'E'
                ? XmlMarkupKind::DocumentType
                : XmlMarkupKind::UnsupportedDeclaration;
        }

        return XmlMarkupKind::UnsupportedDeclaration;
    }

    if (ch1 == '/') {
        return XmlMarkupKind::EndTag;
    }

    if (ch1 == '\0') {
        return XmlMarkupKind::None;
    }

    return XmlMarkupKind::Element;
}

template <typename CharAtFn>
XmlMarkupKind ClassifyXmlMarkupWithCharAt(std::size_t position, CharAtFn&& charAt) noexcept {
    char prefix[9] = {};
    for (std::size_t offset = 0; offset < sizeof(prefix); ++offset) {
        prefix[offset] = charAt(position + offset);
    }
    return ClassifyXmlMarkupPrefix(prefix, sizeof(prefix));
}

XmlMarkupKind ClassifyXmlMarkupAt(const char* p, std::size_t available) noexcept
{
    return ClassifyXmlMarkupPrefix(p, available);
}

template <typename CharAtFn>
std::size_t ScanQuotedValueEndAt(std::size_t quoteStart, CharAtFn&& charAt) noexcept {
    const char quote = charAt(quoteStart);
    if (quote != '"' && quote != '\'') {
        return std::string::npos;
    }

    std::size_t position = quoteStart + 1;
    while (charAt(position) != '\0' && charAt(position) != quote) {
        ++position;
    }

    return charAt(position) == quote ? position : std::string::npos;
}

std::size_t FindQuoteOrNulInBuffer(const char* data, std::size_t length, char quote) noexcept {
    if (data == nullptr || length == 0) {
        return std::string::npos;
    }

#if defined(_M_X64) || defined(_M_IX86)
    const __m128i quoteVector = _mm_set1_epi8(quote);
    const __m128i nulVector = _mm_setzero_si128();

    std::size_t offset = 0;
    while (offset + 16 <= length) {
        const __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + offset));
        const __m128i quoteMatches = _mm_cmpeq_epi8(block, quoteVector);
        const __m128i nulMatches = _mm_cmpeq_epi8(block, nulVector);
        const unsigned int mask = static_cast<unsigned int>(_mm_movemask_epi8(_mm_or_si128(quoteMatches, nulMatches)));
        if (mask != 0) {
            unsigned long firstMatch = 0;
            _BitScanForward(&firstMatch, mask);
            return offset + static_cast<std::size_t>(firstMatch);
        }
        offset += 16;
    }

    for (; offset < length; ++offset) {
        if (data[offset] == quote || data[offset] == '\0') {
            return offset;
        }
    }
    return std::string::npos;
#else
    for (std::size_t offset = 0; offset < length; ++offset) {
        if (data[offset] == quote || data[offset] == '\0') {
            return offset;
        }
    }
    return std::string::npos;
#endif
}

template <typename FindFn>
std::size_t ScanDelimitedSectionEndAt(std::size_t contentStart, std::string_view terminator, FindFn&& find) noexcept {
    return find(terminator, contentStart);
}

template <typename CharAtFn, typename StartsWithFn>
bool ConsumeXmlStartTagCloseAt(
    std::size_t& position,
    bool& isEmptyElement,
    CharAtFn&& charAt,
    StartsWithFn&& startsWith) noexcept {
    if (startsWith(position, "/>")) {
        position += 2;
        isEmptyElement = true;
        return true;
    }
    if (charAt(position) == '>') {
        ++position;
        isEmptyElement = false;
        return true;
    }
    return false;
}

template <typename SkipWhitespaceFn, typename CharAtFn>
bool ConsumeXmlEndTagCloseAt(std::size_t& position, SkipWhitespaceFn&& skipWhitespace, CharAtFn&& charAt) noexcept {
    skipWhitespace(position);
    if (charAt(position) != '>') {
        return false;
    }
    ++position;
    return true;
}

struct XmlAttributeAssignmentToken {
    std::string name;
    std::size_t rawValueStart = std::string::npos;
    std::size_t rawValueEnd = std::string::npos;
    bool sawEquals = false;
    bool valid = false;
};

template <typename ParseNameFn, typename SkipWhitespaceFn, typename CharAtFn, typename ScanQuotedValueFn>
XmlAttributeAssignmentToken ParseXmlAttributeAssignmentAt(
    std::size_t& position,
    ParseNameFn&& parseName,
    SkipWhitespaceFn&& skipWhitespace,
    CharAtFn&& charAt,
    ScanQuotedValueFn&& scanQuotedValue) {
    XmlAttributeAssignmentToken token;
    token.name = parseName(position);
    if (token.name.empty()) {
        return token;
    }

    skipWhitespace(position);
    if (charAt(position) != '=') {
        return token;
    }
    token.sawEquals = true;
    ++position;

    skipWhitespace(position);
    const char quote = charAt(position);
    const auto quoteEnd = scanQuotedValue(position);
    if ((quote != '"' && quote != '\'') || quoteEnd == std::string::npos) {
        return token;
    }

    token.rawValueStart = position + 1;
    token.rawValueEnd = quoteEnd;
    position = quoteEnd + 1;
    token.valid = true;
    return token;
}

std::filesystem::path CreateTemporaryXmlReplayPath() {
    const auto tempDirectory = std::filesystem::temp_directory_path();
    static std::atomic<unsigned long long> counter{0};

    while (true) {
        const auto uniqueValue = counter.fetch_add(1, std::memory_order_relaxed) + 1;
        const auto timestamp = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto candidate = tempDirectory / (
            "libxml-xmlreader-replay-"
            + std::to_string(timestamp)
            + "-"
            + std::to_string(uniqueValue)
            + ".xml");
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
}

std::filesystem::path SpoolStreamToTemporaryFile(std::istream& stream) {
    const auto path = CreateTemporaryXmlReplayPath();
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw XmlException("Failed to create temporary XML replay file");
    }

    char chunk[64 * 1024];
    while (stream) {
        stream.read(chunk, static_cast<std::streamsize>(sizeof(chunk)));
        const auto bytesRead = stream.gcount();
        if (bytesRead <= 0) {
            break;
        }
        output.write(chunk, bytesRead);
        if (!output) {
            break;
        }
    }

    if (!output) {
        std::error_code error;
        std::filesystem::remove(path, error);
        throw XmlException("Failed to write temporary XML replay file");
    }

    return path;
}

std::shared_ptr<std::istream> OpenTemporaryXmlReplayStream(const std::filesystem::path& path) {
    auto stream = std::shared_ptr<std::ifstream>(
        new std::ifstream(path, std::ios::binary),
        [path](std::ifstream* file) {
            if (file != nullptr) {
                file->close();
                delete file;
            }
            std::error_code error;
            std::filesystem::remove(path, error);
        });

    if (!*stream) {
        std::error_code error;
        stream.reset();
        std::filesystem::remove(path, error);
        throw XmlException("Failed to open temporary XML replay file");
    }

    return std::static_pointer_cast<std::istream>(stream);
}

std::string ReadStreamPrefix(std::istream& stream, std::size_t maxBytes) {
    std::string buffer;
    buffer.reserve(maxBytes);

    while (buffer.size() < maxBytes && stream) {
        const auto remaining = maxBytes - buffer.size();
        const auto chunkSize = static_cast<std::streamsize>((std::min<std::size_t>)(remaining, 64 * 1024));
        std::string chunk(static_cast<std::size_t>(chunkSize), '\0');
        stream.read(chunk.data(), chunkSize);
        const auto bytesRead = stream.gcount();
        if (bytesRead <= 0) {
            break;
        }
        chunk.resize(static_cast<std::size_t>(bytesRead));
        buffer += chunk;
    }

    return buffer;
}

std::string ReadFileFully(const std::string& path) {
    std::ifstream stream(std::filesystem::path(path), std::ios::binary);
    if (!stream) {
        throw XmlException("Failed to open XML file: " + path);
    }

    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    stream.seekg(0, std::ios::beg);

    std::string content;
    if (end > std::streampos(0)) {
        content.resize(static_cast<std::size_t>(end));
        stream.read(content.data(), static_cast<std::streamsize>(content.size()));
        content.resize(static_cast<std::size_t>(stream.gcount()));
        if (!stream.eof() && !stream.good()) {
            throw XmlException("Failed to read XML file: " + path);
        }
        return content;
    }

    content.assign(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
    if (!stream.eof() && !stream.good()) {
        throw XmlException("Failed to read XML file: " + path);
    }
    return content;
}

std::size_t GetUtf8BomLength(std::string_view text) noexcept {
    return text.size() >= 3
            && static_cast<unsigned char>(text[0]) == 0xEF
            && static_cast<unsigned char>(text[1]) == 0xBB
            && static_cast<unsigned char>(text[2]) == 0xBF
        ? 3u
        : 0u;
}

void ThrowIfXmlExceedsMaxCharacters(const std::string& xml, const XmlReaderSettings& settings) {
    if (settings.MaxCharactersInDocument == 0) {
        return;
    }

    const std::size_t bomLength = GetUtf8BomLength(xml);
    if (xml.size() > bomLength && xml.size() - bomLength > settings.MaxCharactersInDocument) {
        throw XmlException("The XML document exceeds the configured MaxCharactersInDocument limit");
    }
}

std::size_t ScanDocumentTypeInternalSubsetEnd(std::string_view text, std::size_t contentStart) noexcept {
    std::size_t position = contentStart;
    int bracketDepth = 1;
    bool inQuote = false;
    char quote = '\0';

    while (position < text.size() && bracketDepth > 0) {
        const char ch = text[position++];
        if (inQuote) {
            if (ch == quote) {
                inQuote = false;
            }
            continue;
        }

        if (ch == '"' || ch == '\'') {
            inQuote = true;
            quote = ch;
        } else if (ch == '[') {
            ++bracketDepth;
        } else if (ch == ']') {
            --bracketDepth;
        }
    }

    return bracketDepth == 0 ? position - 1 : std::string::npos;
}

struct ParsedDocumentTypeDeclaration {
    std::string name;
    std::string publicId;
    std::string systemId;
    std::string internalSubset;
};

struct ParsedXmlDeclarationData {
    std::string version = "1.0";
    std::string encoding;
    std::string standalone;
};

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

std::string ComposeQualifiedName(std::string_view prefix, std::string_view localName) {
    if (prefix.empty()) {
        return std::string(localName);
    }
    std::string result;
    result.reserve(prefix.size() + 1 + localName.size());
    result.append(prefix);
    result.push_back(':');
    result.append(localName);
    return result;
}

bool IsValidXmlNameToken(std::string_view name) {
    if (name.empty() || !IsNameStartChar(name.front())) {
        return false;
    }

    return std::all_of(name.begin() + 1, name.end(), [](char ch) {
        return IsNameChar(ch);
    });
}

bool IsValidXmlQualifiedName(const std::string& name) {
    if (name.empty()) {
        return false;
    }

    const auto separator = name.find(':');
    if (separator == std::string::npos) {
        return IsValidXmlNameToken(name);
    }

    if (name.find(':', separator + 1) != std::string::npos) {
        return false;
    }

    return IsValidXmlNameToken(std::string_view(name).substr(0, separator))
        && IsValidXmlNameToken(std::string_view(name).substr(separator + 1));
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

std::string ResolveNodeNamespaceUri(const XmlNode& node) {
    if (node.NodeType() == XmlNodeType::Attribute) {
        const auto prefix = SplitQualifiedNameView(node.Name()).first;
        if (node.Name() == "xmlns" || prefix == "xmlns") {
            return "http://www.w3.org/2000/xmlns/";
        }
        if (prefix.empty()) {
            return {};
        }
        const auto* parent = node.ParentNode();
        if (parent != nullptr && parent->NodeType() == XmlNodeType::Element) {
            return LookupNamespaceUriOnElement(static_cast<const XmlElement*>(parent), prefix);
        }
        return prefix == "xml" ? "http://www.w3.org/XML/1998/namespace" : std::string{};
    }

    if (node.NodeType() == XmlNodeType::Element) {
        const auto* element = static_cast<const XmlElement*>(&node);
        return LookupNamespaceUriOnElement(element, SplitQualifiedNameView(node.Name()).first);
    }

    return {};
}

std::string LookupPrefixOnElement(const XmlElement* element, const std::string& namespaceUri) {
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

struct PracticalIntegerValue {
    bool negative = false;
    unsigned long long magnitude = 0;
};

bool TryParsePracticalInteger(const std::string& input, PracticalIntegerValue& result) {
    if (input.empty()) {
        return false;
    }

    std::size_t index = 0;
    bool negative = false;
    if (input[index] == '+' || input[index] == '-') {
        negative = input[index] == '-';
        ++index;
    }
    if (index == input.size()) {
        return false;
    }

    unsigned long long magnitude = 0;
    for (; index < input.size(); ++index) {
        const unsigned char digitChar = static_cast<unsigned char>(input[index]);
        if (std::isdigit(digitChar) == 0) {
            return false;
        }

        const unsigned int digit = static_cast<unsigned int>(digitChar - '0');
        if (magnitude > (std::numeric_limits<unsigned long long>::max() - digit) / 10ULL) {
            return false;
        }
        magnitude = magnitude * 10ULL + digit;
    }

    result.negative = negative && magnitude != 0;
    result.magnitude = magnitude;
    return true;
}

PracticalIntegerValue ParsePracticalIntegerOrThrow(const std::string& input) {
    PracticalIntegerValue result;
    if (!TryParsePracticalInteger(input, result)) {
        throw XmlException("invalid integer lexical form");
    }
    return result;
}

int ComparePracticalIntegerValues(const PracticalIntegerValue& left, const PracticalIntegerValue& right) {
    if (left.negative != right.negative) {
        return left.negative ? -1 : 1;
    }
    if (left.magnitude == right.magnitude) {
        return 0;
    }
    if (left.negative) {
        return left.magnitude > right.magnitude ? -1 : 1;
    }
    return left.magnitude < right.magnitude ? -1 : 1;
}

bool IsPracticalIntegerBuiltinType(const std::string& qualifiedName) {
    return qualifiedName == "xs:integer"
        || qualifiedName == "xs:long"
        || qualifiedName == "xs:int"
        || qualifiedName == "xs:short"
        || qualifiedName == "xs:byte"
        || qualifiedName == "xs:nonNegativeInteger"
        || qualifiedName == "xs:positiveInteger"
        || qualifiedName == "xs:unsignedLong"
        || qualifiedName == "xs:unsignedInt"
        || qualifiedName == "xs:unsignedShort"
        || qualifiedName == "xs:unsignedByte"
        || qualifiedName == "xs:nonPositiveInteger"
        || qualifiedName == "xs:negativeInteger";
}

std::pair<std::optional<PracticalIntegerValue>, std::optional<PracticalIntegerValue>> GetPracticalIntegerBounds(const std::string& qualifiedName) {
    const auto positive = [](unsigned long long magnitude) {
        return PracticalIntegerValue{false, magnitude};
    };
    const auto negative = [](unsigned long long magnitude) {
        return PracticalIntegerValue{true, magnitude};
    };

    if (qualifiedName == "xs:byte") {
        return {negative(128ULL), positive(127ULL)};
    }
    if (qualifiedName == "xs:short") {
        return {negative(32768ULL), positive(32767ULL)};
    }
    if (qualifiedName == "xs:int") {
        return {negative(2147483648ULL), positive(2147483647ULL)};
    }
    if (qualifiedName == "xs:long" || qualifiedName == "xs:integer") {
        return {negative(9223372036854775808ULL), positive(9223372036854775807ULL)};
    }
    if (qualifiedName == "xs:unsignedByte") {
        return {positive(0ULL), positive(255ULL)};
    }
    if (qualifiedName == "xs:unsignedShort") {
        return {positive(0ULL), positive(65535ULL)};
    }
    if (qualifiedName == "xs:unsignedInt") {
        return {positive(0ULL), positive(4294967295ULL)};
    }
    if (qualifiedName == "xs:unsignedLong" || qualifiedName == "xs:nonNegativeInteger") {
        return {positive(0ULL), positive(std::numeric_limits<unsigned long long>::max())};
    }
    if (qualifiedName == "xs:positiveInteger") {
        return {positive(1ULL), positive(std::numeric_limits<unsigned long long>::max())};
    }
    if (qualifiedName == "xs:nonPositiveInteger") {
        return {negative(9223372036854775808ULL), positive(0ULL)};
    }
    if (qualifiedName == "xs:negativeInteger") {
        return {negative(9223372036854775808ULL), negative(1ULL)};
    }
    return {std::nullopt, std::nullopt};
}

bool PracticalIntegerFitsBuiltinType(const std::string& qualifiedName, const PracticalIntegerValue& value) {
    const auto [minValue, maxValue] = GetPracticalIntegerBounds(qualifiedName);
    if (minValue.has_value() && ComparePracticalIntegerValues(value, *minValue) < 0) {
        return false;
    }
    if (maxValue.has_value() && ComparePracticalIntegerValues(value, *maxValue) > 0) {
        return false;
    }
    return true;
}

struct PracticalDecimalValue {
    bool negative = false;
    std::string digits = "0";
    std::size_t scale = 0;
};

PracticalDecimalValue ParsePracticalDecimalOrThrow(const std::string& lexicalValue) {
    if (lexicalValue.empty()) {
        throw XmlException("invalid decimal lexical form");
    }

    std::size_t index = 0;
    bool negative = false;
    if (lexicalValue[index] == '+' || lexicalValue[index] == '-') {
        negative = lexicalValue[index] == '-';
        ++index;
    }

    bool seenDecimalPoint = false;
    bool sawDigit = false;
    std::string integerDigits;
    std::string fractionalDigits;
    for (; index < lexicalValue.size(); ++index) {
        const char ch = lexicalValue[index];
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            sawDigit = true;
            if (seenDecimalPoint) {
                fractionalDigits.push_back(ch);
            } else {
                integerDigits.push_back(ch);
            }
            continue;
        }
        if (ch == '.' && !seenDecimalPoint) {
            seenDecimalPoint = true;
            continue;
        }
        throw XmlException("invalid decimal lexical form");
    }

    if (!sawDigit) {
        throw XmlException("invalid decimal lexical form");
    }

    while (!fractionalDigits.empty() && fractionalDigits.back() == '0') {
        fractionalDigits.pop_back();
    }

    std::string digits = integerDigits + fractionalDigits;
    const std::size_t firstNonZero = digits.find_first_not_of('0');
    if (firstNonZero == std::string::npos) {
        return PracticalDecimalValue{};
    }

    digits.erase(0, firstNonZero);
    PracticalDecimalValue value;
    value.negative = negative;
    value.digits = std::move(digits);
    value.scale = fractionalDigits.size();
    return value;
}

int ComparePracticalDecimalValues(const PracticalDecimalValue& left, const PracticalDecimalValue& right) {
    if (left.negative != right.negative) {
        return left.negative ? -1 : 1;
    }

    const std::size_t commonScale = std::max(left.scale, right.scale);
    const std::string leftScaled = left.digits + std::string(commonScale - left.scale, '0');
    const std::string rightScaled = right.digits + std::string(commonScale - right.scale, '0');

    int comparison = 0;
    if (leftScaled.size() < rightScaled.size()) {
        comparison = -1;
    } else if (leftScaled.size() > rightScaled.size()) {
        comparison = 1;
    } else if (leftScaled < rightScaled) {
        comparison = -1;
    } else if (leftScaled > rightScaled) {
        comparison = 1;
    }

    if (comparison == 0) {
        return 0;
    }
    return left.negative ? -comparison : comparison;
}

std::optional<std::string> GetBuiltinSimpleTypeBase(const std::string& qualifiedName) {
    if (qualifiedName == "xs:string" || qualifiedName == "xs:boolean" || qualifiedName == "xs:float"
        || qualifiedName == "xs:double" || qualifiedName == "xs:decimal" || qualifiedName == "xs:duration"
        || qualifiedName == "xs:date" || qualifiedName == "xs:time" || qualifiedName == "xs:dateTime"
        || qualifiedName == "xs:gYear" || qualifiedName == "xs:gYearMonth"
        || qualifiedName == "xs:gMonth" || qualifiedName == "xs:gDay" || qualifiedName == "xs:gMonthDay"
        || qualifiedName == "xs:hexBinary" || qualifiedName == "xs:base64Binary"
        || qualifiedName == "xs:QName" || qualifiedName == "xs:NOTATION"
        || qualifiedName == "xs:NMTOKENS" || qualifiedName == "xs:IDREFS" || qualifiedName == "xs:ENTITIES") {
        return "xs:anySimpleType";
    }
    if (qualifiedName == "xs:normalizedString") {
        return "xs:string";
    }
    if (qualifiedName == "xs:token") {
        return "xs:normalizedString";
    }
    if (qualifiedName == "xs:language" || qualifiedName == "xs:Name" || qualifiedName == "xs:NMTOKEN" || qualifiedName == "xs:anyURI") {
        return "xs:token";
    }
    if (qualifiedName == "xs:NCName") {
        return "xs:Name";
    }
    if (qualifiedName == "xs:ID" || qualifiedName == "xs:IDREF" || qualifiedName == "xs:ENTITY") {
        return "xs:NCName";
    }
    if (qualifiedName == "xs:integer") {
        return "xs:decimal";
    }
    if (qualifiedName == "xs:long" || qualifiedName == "xs:nonPositiveInteger" || qualifiedName == "xs:nonNegativeInteger") {
        return "xs:integer";
    }
    if (qualifiedName == "xs:int") {
        return "xs:long";
    }
    if (qualifiedName == "xs:short") {
        return "xs:int";
    }
    if (qualifiedName == "xs:byte") {
        return "xs:short";
    }
    if (qualifiedName == "xs:negativeInteger") {
        return "xs:nonPositiveInteger";
    }
    if (qualifiedName == "xs:positiveInteger" || qualifiedName == "xs:unsignedLong") {
        return "xs:nonNegativeInteger";
    }
    if (qualifiedName == "xs:unsignedInt") {
        return "xs:unsignedLong";
    }
    if (qualifiedName == "xs:unsignedShort") {
        return "xs:unsignedInt";
    }
    if (qualifiedName == "xs:unsignedByte") {
        return "xs:unsignedShort";
    }
    return std::nullopt;
}

bool BuiltinSimpleTypeDerivesFrom(const std::string& derivedType, const std::string& baseType, bool& usesRestriction) {
    if (derivedType.empty() || baseType.empty()) {
        return false;
    }
    if (derivedType == baseType) {
        return true;
    }

    std::string current = derivedType;
    std::unordered_set<std::string> visited;
    while (visited.insert(current).second) {
        const std::optional<std::string> parent = GetBuiltinSimpleTypeBase(current);
        if (!parent.has_value()) {
            return false;
        }
        usesRestriction = true;
        if (*parent == baseType) {
            return true;
        }
        current = *parent;
    }
    return false;
}

struct BuiltinSimpleTypeDescriptor {
    enum class Variety {
        Atomic,
        List,
    };

    Variety variety = Variety::Atomic;
    std::string baseType;
    std::optional<std::string> whiteSpace;
    std::string itemType;
    std::optional<std::string> itemWhiteSpace;
};

std::optional<BuiltinSimpleTypeDescriptor> ResolveBuiltinSimpleTypeDescriptor(const std::string& qualifiedName) {
    if (qualifiedName == "xs:anySimpleType"
        || qualifiedName == "xs:int" || qualifiedName == "xs:integer"
        || qualifiedName == "xs:long" || qualifiedName == "xs:short" || qualifiedName == "xs:byte"
        || qualifiedName == "xs:nonNegativeInteger" || qualifiedName == "xs:positiveInteger"
        || qualifiedName == "xs:unsignedLong" || qualifiedName == "xs:unsignedInt"
        || qualifiedName == "xs:unsignedShort" || qualifiedName == "xs:unsignedByte"
        || qualifiedName == "xs:nonPositiveInteger" || qualifiedName == "xs:negativeInteger"
        || qualifiedName == "xs:boolean" || qualifiedName == "xs:float" || qualifiedName == "xs:double"
        || qualifiedName == "xs:decimal" || qualifiedName == "xs:string" || qualifiedName == "xs:duration"
        || qualifiedName == "xs:token" || qualifiedName == "xs:normalizedString"
        || qualifiedName == "xs:language" || qualifiedName == "xs:anyURI"
        || qualifiedName == "xs:date" || qualifiedName == "xs:time" || qualifiedName == "xs:dateTime"
        || qualifiedName == "xs:gYear" || qualifiedName == "xs:gYearMonth"
        || qualifiedName == "xs:gMonth" || qualifiedName == "xs:gDay" || qualifiedName == "xs:gMonthDay"
        || qualifiedName == "xs:hexBinary" || qualifiedName == "xs:base64Binary"
        || qualifiedName == "xs:Name" || qualifiedName == "xs:NCName"
        || qualifiedName == "xs:NMTOKEN" || qualifiedName == "xs:QName" || qualifiedName == "xs:NOTATION"
        || qualifiedName == "xs:ID" || qualifiedName == "xs:IDREF" || qualifiedName == "xs:ENTITY") {
        BuiltinSimpleTypeDescriptor rule;
        rule.baseType = qualifiedName;
        if (qualifiedName == "xs:normalizedString") {
            rule.whiteSpace = "replace";
        }
        if (qualifiedName == "xs:boolean" || qualifiedName == "xs:float" || qualifiedName == "xs:double"
            || qualifiedName == "xs:decimal"
            || qualifiedName == "xs:token" || qualifiedName == "xs:language" || qualifiedName == "xs:anyURI"
            || qualifiedName == "xs:duration"
            || IsPracticalIntegerBuiltinType(qualifiedName)
            || qualifiedName == "xs:date" || qualifiedName == "xs:time" || qualifiedName == "xs:dateTime"
            || qualifiedName == "xs:gYear" || qualifiedName == "xs:gYearMonth"
            || qualifiedName == "xs:gMonth" || qualifiedName == "xs:gDay" || qualifiedName == "xs:gMonthDay"
            || qualifiedName == "xs:hexBinary" || qualifiedName == "xs:base64Binary"
            || qualifiedName == "xs:Name" || qualifiedName == "xs:NCName"
            || qualifiedName == "xs:NMTOKEN" || qualifiedName == "xs:QName" || qualifiedName == "xs:NOTATION"
            || qualifiedName == "xs:ID" || qualifiedName == "xs:IDREF" || qualifiedName == "xs:ENTITY") {
            rule.whiteSpace = "collapse";
        }
        return rule;
    }
    if (qualifiedName == "xs:NMTOKENS") {
        BuiltinSimpleTypeDescriptor rule;
        rule.variety = BuiltinSimpleTypeDescriptor::Variety::List;
        rule.baseType = "xs:string";
        rule.whiteSpace = "collapse";
        rule.itemType = "xs:NMTOKEN";
        rule.itemWhiteSpace = "collapse";
        return rule;
    }
    if (qualifiedName == "xs:IDREFS") {
        BuiltinSimpleTypeDescriptor rule;
        rule.variety = BuiltinSimpleTypeDescriptor::Variety::List;
        rule.baseType = "xs:string";
        rule.whiteSpace = "collapse";
        rule.itemType = "xs:IDREF";
        rule.itemWhiteSpace = "collapse";
        return rule;
    }
    if (qualifiedName == "xs:ENTITIES") {
        BuiltinSimpleTypeDescriptor rule;
        rule.variety = BuiltinSimpleTypeDescriptor::Variety::List;
        rule.baseType = "xs:string";
        rule.whiteSpace = "collapse";
        rule.itemType = "xs:ENTITY";
        rule.itemWhiteSpace = "collapse";
        return rule;
    }
    return std::nullopt;
}

struct PracticalDurationValue {
    bool negative = false;
    unsigned long long wholeSeconds = 0;
    int fractionalNanoseconds = 0;
};

int ComparePracticalDurationValues(const PracticalDurationValue& left, const PracticalDurationValue& right) {
    if (left.negative != right.negative) {
        return left.negative ? -1 : 1;
    }

    if (left.wholeSeconds != right.wholeSeconds) {
        if (left.negative) {
            return left.wholeSeconds > right.wholeSeconds ? -1 : 1;
        }
        return left.wholeSeconds < right.wholeSeconds ? -1 : 1;
    }

    if (left.fractionalNanoseconds != right.fractionalNanoseconds) {
        if (left.negative) {
            return left.fractionalNanoseconds > right.fractionalNanoseconds ? -1 : 1;
        }
        return left.fractionalNanoseconds < right.fractionalNanoseconds ? -1 : 1;
    }

    return 0;
}

PracticalDurationValue ParsePracticalDurationOrThrow(const std::string& lexicalValue) {
    std::size_t index = 0;
    bool negative = false;
    if (!lexicalValue.empty() && lexicalValue[0] == '-') {
        negative = true;
        index = 1;
    }

    if (index >= lexicalValue.size() || lexicalValue[index] != 'P') {
        throw XmlException("invalid duration lexical form");
    }
    ++index;

    bool inTimeSection = false;
    bool sawComponent = false;
    bool sawTimeComponent = false;
    unsigned long long wholeSeconds = 0;
    int fractionalNanoseconds = 0;

    const auto parseUnsignedComponent = [&](const std::string& text, const std::size_t start, const std::size_t end) {
        if (start >= end) {
            throw XmlException("invalid duration lexical form");
        }
        unsigned long long value = 0;
        for (std::size_t pos = start; pos < end; ++pos) {
            const char ch = text[pos];
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                throw XmlException("invalid duration lexical form");
            }
            if (value > (std::numeric_limits<unsigned long long>::max() - static_cast<unsigned long long>(ch - '0')) / 10ULL) {
                throw XmlException("invalid duration lexical form");
            }
            value = value * 10ULL + static_cast<unsigned long long>(ch - '0');
        }
        return value;
    };

    const auto addSecondsChecked = [&](const unsigned long long value) {
        if (wholeSeconds > std::numeric_limits<unsigned long long>::max() - value) {
            throw XmlException("invalid duration lexical form");
        }
        wholeSeconds += value;
    };

    while (index < lexicalValue.size()) {
        if (lexicalValue[index] == 'T') {
            if (inTimeSection) {
                throw XmlException("invalid duration lexical form");
            }
            inTimeSection = true;
            ++index;
            if (index >= lexicalValue.size()) {
                throw XmlException("invalid duration lexical form");
            }
            continue;
        }

        const std::size_t numberStart = index;
        while (index < lexicalValue.size() && std::isdigit(static_cast<unsigned char>(lexicalValue[index]))) {
            ++index;
        }
        if (numberStart == index) {
            throw XmlException("invalid duration lexical form");
        }

        unsigned long long wholePart = parseUnsignedComponent(lexicalValue, numberStart, index);
        int fractionPart = 0;
        if (index < lexicalValue.size() && lexicalValue[index] == '.') {
            if (!inTimeSection) {
                throw XmlException("invalid duration lexical form");
            }
            ++index;
            const std::size_t fractionStart = index;
            while (index < lexicalValue.size() && std::isdigit(static_cast<unsigned char>(lexicalValue[index]))) {
                ++index;
            }
            if (fractionStart == index) {
                throw XmlException("invalid duration lexical form");
            }
            for (std::size_t pos = 0; pos < 9; ++pos) {
                fractionPart *= 10;
                const std::size_t digitIndex = fractionStart + pos;
                if (digitIndex < index) {
                    fractionPart += lexicalValue[digitIndex] - '0';
                }
            }
        }

        if (index >= lexicalValue.size()) {
            throw XmlException("invalid duration lexical form");
        }

        const char designator = lexicalValue[index++];
        sawComponent = true;
        switch (designator) {
        case 'D':
            if (inTimeSection) {
                throw XmlException("invalid duration lexical form");
            }
            if (wholePart > std::numeric_limits<unsigned long long>::max() / 86400ULL) {
                throw XmlException("invalid duration lexical form");
            }
            addSecondsChecked(wholePart * 86400ULL);
            break;
        case 'H':
            if (!inTimeSection || fractionPart != 0) {
                throw XmlException("invalid duration lexical form");
            }
            sawTimeComponent = true;
            if (wholePart > std::numeric_limits<unsigned long long>::max() / 3600ULL) {
                throw XmlException("invalid duration lexical form");
            }
            addSecondsChecked(wholePart * 3600ULL);
            break;
        case 'M':
            if (!inTimeSection || fractionPart != 0) {
                throw XmlException("invalid duration lexical form");
            }
            sawTimeComponent = true;
            if (wholePart > std::numeric_limits<unsigned long long>::max() / 60ULL) {
                throw XmlException("invalid duration lexical form");
            }
            addSecondsChecked(wholePart * 60ULL);
            break;
        case 'S':
            if (!inTimeSection) {
                throw XmlException("invalid duration lexical form");
            }
            sawTimeComponent = true;
            addSecondsChecked(wholePart);
            fractionalNanoseconds = fractionPart;
            break;
        case 'Y':
        case 'W':
            throw XmlException("unsupported duration lexical form");
        default:
            throw XmlException("invalid duration lexical form");
        }
    }

    if (!sawComponent || (inTimeSection && !sawTimeComponent)) {
        throw XmlException("invalid duration lexical form");
    }

    return PracticalDurationValue{ negative, wholeSeconds, fractionalNanoseconds };
}

bool TryDecodeHexBinary(const std::string& input, std::vector<unsigned char>& result) {
    if (input.size() % 2 != 0) {
        return false;
    }

    auto decodeHexNibble = [](const char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        return -1;
    };

    result.clear();
    result.reserve(input.size() / 2);
    for (std::size_t index = 0; index < input.size(); index += 2) {
        const int high = decodeHexNibble(input[index]);
        const int low = decodeHexNibble(input[index + 1]);
        if (high < 0 || low < 0) {
            result.clear();
            return false;
        }
        result.push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return true;
}

bool TryDecodeBase64Binary(const std::string& input, std::vector<unsigned char>& result) {
    if (input.empty()) {
        result.clear();
        return true;
    }
    if (std::any_of(input.begin(), input.end(), IsWhitespace) || input.size() % 4 != 0) {
        return false;
    }

    std::size_t padding = 0;
    if (!input.empty() && input.back() == '=') {
        padding = 1;
        if (input.size() >= 2 && input[input.size() - 2] == '=') {
            padding = 2;
        }
    }
    for (std::size_t index = 0; index + padding < input.size(); ++index) {
        if (input[index] == '=') {
            return false;
        }
    }
    if (padding > 2) {
        return false;
    }

    result.clear();
    result.reserve((input.size() / 4) * 3);
    for (std::size_t index = 0; index < input.size(); index += 4) {
        int values[4] = { 0, 0, 0, 0 };
        int localPadding = 0;
        for (int offset = 0; offset < 4; ++offset) {
            const char ch = input[index + offset];
            if (ch == '=') {
                values[offset] = 0;
                ++localPadding;
                continue;
            }
            const int value = DecodeBase64Char(ch);
            if (value < 0) {
                result.clear();
                return false;
            }
            values[offset] = value;
        }
        if (localPadding > 0 && index + 4 != input.size()) {
            result.clear();
            return false;
        }
        if (localPadding == 1 && input[index + 3] != '=') {
            result.clear();
            return false;
        }
        if (localPadding == 2 && !(input[index + 2] == '=' && input[index + 3] == '=')) {
            result.clear();
            return false;
        }
        if (localPadding > 2) {
            result.clear();
            return false;
        }

        const unsigned int chunk = (static_cast<unsigned int>(values[0]) << 18)
            | (static_cast<unsigned int>(values[1]) << 12)
            | (static_cast<unsigned int>(values[2]) << 6)
            | static_cast<unsigned int>(values[3]);
        result.push_back(static_cast<unsigned char>((chunk >> 16) & 0xFF));
        if (localPadding < 2) {
            result.push_back(static_cast<unsigned char>((chunk >> 8) & 0xFF));
        }
        if (localPadding < 1) {
            result.push_back(static_cast<unsigned char>(chunk & 0xFF));
        }
    }
    return true;
}

bool IsWhitespaceOnly(const std::string& value) {
    return std::all_of(value.begin(), value.end(), IsWhitespace);
}

void AppendParsedCharacterDataNode(XmlDocument& document, XmlElement& element, const std::string& value, bool preserveWhitespaceOnly) {
    if (document.PreserveWhitespace() && IsWhitespaceOnly(value)) {
        element.AppendChild(document.CreateWhitespace(value));
    } else if (document.PreserveWhitespace() || !IsWhitespaceOnly(value) || preserveWhitespaceOnly) {
        element.AppendChild(document.CreateTextNode(value));
    }
}

bool IsXmlSpacePreserve(const std::string& value) {
    return value == "preserve";
}

bool IsXmlSpaceDefault(const std::string& value) {
    return value == "default";
}

std::string_view ResolvePredefinedEntityReferenceValueView(std::string_view name) noexcept {
    if (name == "lt") {
        return "<";
    }
    if (name == "gt") {
        return ">";
    }
    if (name == "amp") {
        return "&";
    }
    if (name == "apos") {
        return "'";
    }
    if (name == "quot") {
        return "\"";
    }
    return {};
}

std::string ResolvePredefinedEntityReferenceValue(const std::string& name) {
    return std::string(ResolvePredefinedEntityReferenceValueView(name));
}

bool TryParseUnsignedInteger(std::string_view digits, const unsigned int base, unsigned int& value) noexcept {
    if (digits.empty() || (base != 10 && base != 16)) {
        return false;
    }

    unsigned int parsed = 0;
    for (const char ch : digits) {
        unsigned int digit = 0;
        if (ch >= '0' && ch <= '9') {
            digit = static_cast<unsigned int>(ch - '0');
        } else if (base == 16 && ch >= 'a' && ch <= 'f') {
            digit = static_cast<unsigned int>(10 + (ch - 'a'));
        } else if (base == 16 && ch >= 'A' && ch <= 'F') {
            digit = static_cast<unsigned int>(10 + (ch - 'A'));
        } else {
            return false;
        }

        if (digit >= base) {
            return false;
        }
        if (parsed > (std::numeric_limits<unsigned int>::max() - digit) / base) {
            return false;
        }

        parsed = parsed * base + digit;
    }

    value = parsed;
    return true;
}

bool TryParseNumericEntityReferenceCodePoint(std::string_view entity, unsigned int& codePoint) noexcept {
    if (entity.size() < 2 || entity.front() != '#') {
        return false;
    }

    if (entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X')) {
        return TryParseUnsignedInteger(entity.substr(2), 16, codePoint);
    }

    return TryParseUnsignedInteger(entity.substr(1), 10, codePoint);
}

void AppendCodePointUtf8(std::string& output, unsigned int codePoint) {
    if (codePoint <= 0x7F) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

std::optional<std::string> LookupInternalEntityDeclaration(const XmlDocumentType* documentType, const std::string& name) {
    if (documentType == nullptr) {
        return std::nullopt;
    }

    const auto entityNode = documentType->Entities().GetNamedItem(name);
    if (entityNode == nullptr || entityNode->NodeType() != XmlNodeType::Entity) {
        return std::nullopt;
    }

    const auto* entity = static_cast<const XmlEntity*>(entityNode.get());
    if (!entity->PublicId().empty() || !entity->SystemId().empty() || !entity->NotationName().empty()) {
        return std::nullopt;
    }

    return entity->Value();
}

std::optional<std::string> LookupDocumentInternalEntityDeclaration(const XmlDocument* document, const std::string& name) {
    return document == nullptr ? std::nullopt : LookupInternalEntityDeclaration(document->DocumentType().get(), name);
}

bool HasDocumentEntityDeclaration(const XmlDocument* document, const std::string& name) {
    return document != nullptr
        && document->DocumentType() != nullptr
        && document->DocumentType()->Entities().GetNamedItem(name) != nullptr;
}

bool HasDocumentUnparsedEntityDeclaration(const XmlDocument* document, const std::string& name) {
    if (document == nullptr || document->DocumentType() == nullptr) {
        return false;
    }

    const auto entityNode = document->DocumentType()->Entities().GetNamedItem(name);
    if (entityNode == nullptr || entityNode->NodeType() != XmlNodeType::Entity) {
        return false;
    }

    const auto* entity = static_cast<const XmlEntity*>(entityNode.get());
    return !entity->NotationName().empty();
}

bool HasDocumentNotationDeclaration(const XmlDocument* document, const std::string& name) {
    return document != nullptr
        && document->DocumentType() != nullptr
        && document->DocumentType()->Notations().GetNamedItem(name) != nullptr;
}

void PopulateInternalEntityDeclarations(
    const std::vector<std::shared_ptr<XmlNode>>& entities,
    std::unordered_map<std::string, std::string>& declarations) {
    declarations.clear();
    for (const auto& node : entities) {
        if (!node || node->NodeType() != XmlNodeType::Entity) {
            continue;
        }

        const auto* entity = static_cast<const XmlEntity*>(node.get());
        if (!entity->PublicId().empty() || !entity->SystemId().empty() || !entity->NotationName().empty()) {
            continue;
        }

        declarations[entity->Name()] = entity->Value();
    }
}

template <typename Resolver, typename ErrorHandler>
void DecodeEntityTextTo(
    std::string& decoded,
    std::string_view value,
    const Resolver& resolver,
    const ErrorHandler& onError,
    int depth = 0,
    const std::vector<std::string>* resolutionStack = nullptr) {
    if (depth > 16) {
        onError("Entity reference recursion limit exceeded");
        return;
    }

    decoded.reserve(decoded.size() + value.size());

    std::size_t cursor = 0;
    while (cursor < value.size()) {
        const auto ampersand = value.find('&', cursor);
        if (ampersand == std::string_view::npos) {
            decoded.append(value.data() + cursor, value.size() - cursor);
            break;
        }

        if (ampersand > cursor) {
            decoded.append(value.data() + cursor, ampersand - cursor);
        }

        const auto semicolon = value.find(';', ampersand + 1);
        if (semicolon == std::string_view::npos) {
            onError("Unterminated entity reference");
            return;
        }

        const std::string_view entity = value.substr(ampersand + 1, semicolon - ampersand - 1);
        const auto predefined = ResolvePredefinedEntityReferenceValueView(entity);
        if (!predefined.empty()) {
            decoded.append(predefined.data(), predefined.size());
        } else if (!entity.empty() && entity.front() == '#') {
            unsigned int codePoint = 0;
            if (!TryParseNumericEntityReferenceCodePoint(entity, codePoint)) {
                onError("Invalid numeric entity reference: &" + std::string(entity) + ';');
                return;
            }
            AppendCodePointUtf8(decoded, codePoint);
        } else {
            const std::string entityName(entity);
            if (const auto resolved = resolver(entityName); resolved.has_value()) {
                if (resolutionStack != nullptr
                    && std::find(resolutionStack->begin(), resolutionStack->end(), entityName) != resolutionStack->end()) {
                    onError("Entity reference cycle detected: &" + entityName + ';');
                    return;
                }

                auto nestedResolutionStack = resolutionStack == nullptr ? std::vector<std::string>{} : *resolutionStack;
                nestedResolutionStack.push_back(entityName);
                DecodeEntityTextTo(decoded, *resolved, resolver, onError, depth + 1, &nestedResolutionStack);
            } else {
                onError("Unknown entity reference: &" + entityName + ';');
                return;
            }
        }

        cursor = semicolon + 1;
    }
}

template <typename Resolver, typename ErrorHandler>
std::string DecodeEntityText(
    std::string_view value,
    const Resolver& resolver,
    const ErrorHandler& onError,
    int depth = 0,
    const std::vector<std::string>* resolutionStack = nullptr) {
    std::string decoded;
    DecodeEntityTextTo(decoded, value, resolver, onError, depth, resolutionStack);
    return decoded;
}

void ValidateWhitespaceValue(const std::string& value, const char* nodeTypeName) {
    if (!IsWhitespaceOnly(value)) {
        throw XmlException(std::string(nodeTypeName) + " nodes can only contain whitespace characters");
    }
}

bool TryParseXmlSchemaBoolean(const std::string& value, bool& result) {
    if (value == "true" || value == "1") {
        result = true;
        return true;
    }
    if (value == "false" || value == "0") {
        result = false;
        return true;
    }
    return false;
}

std::filesystem::path NormalizeSchemaPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return canonical;
    }

    error.clear();
    const auto absolute = std::filesystem::absolute(path, error);
    if (!error) {
        return absolute.lexically_normal();
    }

    return path.lexically_normal();
}

std::vector<std::pair<std::string, std::string>> CollectInScopeNamespaceBindings(const XmlElement& element) {
    std::vector<const XmlElement*> ancestry;
    for (auto current = &element; current != nullptr; ) {
        ancestry.push_back(current);
        const XmlNode* parent = current->ParentNode();
        current = parent != nullptr && parent->NodeType() == XmlNodeType::Element
            ? static_cast<const XmlElement*>(parent)
            : nullptr;
    }
    std::reverse(ancestry.begin(), ancestry.end());

    std::unordered_map<std::string, std::string> bindings;
    for (const auto* current : ancestry) {
        for (const auto& attribute : current->Attributes()) {
            if (!IsNamespaceDeclarationName(attribute->Name())) {
                continue;
            }
            bindings[NamespaceDeclarationPrefix(attribute->Name())] = attribute->Value();
        }
    }

    bindings["xml"] = "http://www.w3.org/XML/1998/namespace";
    bindings["xmlns"] = "http://www.w3.org/2000/xmlns/";

    std::vector<std::pair<std::string, std::string>> collected;
    collected.reserve(bindings.size());
    for (const auto& [prefix, uri] : bindings) {
        collected.emplace_back(prefix, uri);
    }
    return collected;
}

std::string SerializeIdentityConstraintTuple(const std::vector<std::string>& values) {
    if (values.size() == 1) {
        return values.front();
    }

    std::string serialized;
    for (const auto& value : values) {
        serialized += std::to_string(value.size());
        serialized.push_back(':');
        serialized += value;
        serialized.push_back('|');
    }
    return serialized;
}

std::string BuildIdentityConstraintLookupKey(const std::string& localName, const std::string& namespaceUri) {
    std::string key;
    key.reserve(namespaceUri.size() + localName.size() + 1);
    key += namespaceUri;
    key.push_back('\x1f');
    key += localName;
    return key;
}

std::string IdentityConstraintFieldStringValue(const XmlNode& node) {
    switch (node.NodeType()) {
    case XmlNodeType::Attribute:
    case XmlNodeType::Text:
    case XmlNodeType::CDATA:
    case XmlNodeType::Whitespace:
    case XmlNodeType::SignificantWhitespace:
        return node.Value();
    default:
        return node.InnerText();
    }
}

struct IdentityConstraintFieldEvaluationResult {
    bool found = false;
    bool multiple = false;
    std::string value;
};

bool MatchesCompiledIdentityStep(
    const auto& step,
    std::string_view localName,
    std::string_view namespaceUri) {
    if (step.localName.empty()) {
        return true;
    }
    const bool localNameMatches = step.localName == "*" || localName == step.localName;
    const bool namespaceMatches = step.localName == "*" && step.namespaceUri.empty()
        ? true
        : namespaceUri == step.namespaceUri;
    return localNameMatches && namespaceMatches;
}

bool MatchesCompiledIdentityPredicate(const auto& step, const XmlNode& node) {
    if (!step.predicateAttributeValue.has_value()) {
        return true;
    }
    if (node.NodeType() != XmlNodeType::Element) {
        return false;
    }

    const auto* element = static_cast<const XmlElement*>(&node);
    const auto attribute = element->GetAttributeNode(step.predicateAttributeLocalName, step.predicateAttributeNamespaceUri);
    return attribute != nullptr && attribute->Value() == *step.predicateAttributeValue;
}

void AppendDescendantIdentityMatches(
    const XmlNode& node,
    const auto& step,
    std::vector<const XmlNode*>& matches) {
    for (const auto& child : node.ChildNodes()) {
        if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
            continue;
        }
        if (MatchesCompiledIdentityStep(step, child->LocalName(), child->NamespaceURI())
            && MatchesCompiledIdentityPredicate(step, *child)) {
            matches.push_back(child.get());
        }
        AppendDescendantIdentityMatches(*child, step, matches);
    }
}

void AppendAncestorIdentityMatches(
    const XmlNode& node,
    const auto& step,
    bool includeSelf,
    std::vector<const XmlNode*>& matches) {
    const XmlNode* current = includeSelf ? std::addressof(node) : node.ParentNode();
    while (current != nullptr) {
        if (current->NodeType() == XmlNodeType::Element
            && MatchesCompiledIdentityStep(step, current->LocalName(), current->NamespaceURI())
            && MatchesCompiledIdentityPredicate(step, *current)) {
            matches.push_back(current);
        }
        current = current->ParentNode();
    }
}

void AppendFollowingSiblingIdentityMatches(
    const XmlNode& node,
    const auto& step,
    std::vector<const XmlNode*>& matches) {
    const XmlNode* parent = node.ParentNode();
    if (parent == nullptr) {
        return;
    }

    bool foundSelf = false;
    for (const auto& sibling : parent->ChildNodes()) {
        if (!foundSelf) {
            foundSelf = sibling.get() == &node;
            continue;
        }
        if (sibling != nullptr
            && sibling->NodeType() == XmlNodeType::Element
            && MatchesCompiledIdentityStep(step, sibling->LocalName(), sibling->NamespaceURI())
            && MatchesCompiledIdentityPredicate(step, *sibling)) {
            matches.push_back(sibling.get());
        }
    }
}

void AppendPrecedingSiblingIdentityMatches(
    const XmlNode& node,
    const auto& step,
    std::vector<const XmlNode*>& matches) {
    const XmlNode* parent = node.ParentNode();
    if (parent == nullptr) {
        return;
    }

    std::vector<const XmlNode*> preceding;
    for (const auto& sibling : parent->ChildNodes()) {
        if (sibling.get() == &node) {
            break;
        }
        if (sibling != nullptr
            && sibling->NodeType() == XmlNodeType::Element
            && MatchesCompiledIdentityStep(step, sibling->LocalName(), sibling->NamespaceURI())
            && MatchesCompiledIdentityPredicate(step, *sibling)) {
            preceding.push_back(sibling.get());
        }
    }

    for (auto index = preceding.size(); index > 0; --index) {
        matches.push_back(preceding[index - 1]);
    }
}

void CollectElementIdentityNodesInDocumentOrder(const XmlNode& node, std::vector<const XmlNode*>& nodes) {
    if (node.NodeType() == XmlNodeType::Element) {
        nodes.push_back(&node);
    }
    for (const auto& child : node.ChildNodes()) {
        if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
            continue;
        }
        CollectElementIdentityNodesInDocumentOrder(*child, nodes);
    }
}

void AppendFollowingIdentityMatches(
    const XmlNode& node,
    const auto& step,
    std::vector<const XmlNode*>& matches) {
    const XmlNode* current = &node;
    while (current != nullptr) {
        const XmlNode* parent = current->ParentNode();
        if (parent == nullptr) {
            break;
        }

        bool foundSelf = false;
        for (const auto& sibling : parent->ChildNodes()) {
            if (!foundSelf) {
                foundSelf = sibling.get() == current;
                continue;
            }
            if (sibling != nullptr
                && sibling->NodeType() == XmlNodeType::Element
                && MatchesCompiledIdentityStep(step, sibling->LocalName(), sibling->NamespaceURI())
                && MatchesCompiledIdentityPredicate(step, *sibling)) {
                matches.push_back(sibling.get());
            }
            if (sibling != nullptr) {
                AppendDescendantIdentityMatches(*sibling, step, matches);
            }
        }
        current = parent;
    }
}

void AppendPrecedingIdentityMatches(
    const XmlNode& node,
    const auto& step,
    std::vector<const XmlNode*>& matches) {
    std::unordered_set<const XmlNode*> ancestors;
    for (const XmlNode* ancestor = node.ParentNode(); ancestor != nullptr; ancestor = ancestor->ParentNode()) {
        ancestors.insert(ancestor);
    }

    const XmlNode* root = &node;
    while (root->ParentNode() != nullptr) {
        root = root->ParentNode();
    }

    std::vector<const XmlNode*> allNodes;
    if (root->NodeType() == XmlNodeType::Element) {
        CollectElementIdentityNodesInDocumentOrder(*root, allNodes);
    } else {
        for (const auto& child : root->ChildNodes()) {
            if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                continue;
            }
            CollectElementIdentityNodesInDocumentOrder(*child, allNodes);
        }
    }

    std::vector<const XmlNode*> preceding;
    for (const XmlNode* candidate : allNodes) {
        if (candidate == &node) {
            break;
        }
        if (ancestors.find(candidate) == ancestors.end()
            && MatchesCompiledIdentityStep(step, candidate->LocalName(), candidate->NamespaceURI())
            && MatchesCompiledIdentityPredicate(step, *candidate)) {
            preceding.push_back(candidate);
        }
    }

    for (auto index = preceding.size(); index > 0; --index) {
        matches.push_back(preceding[index - 1]);
    }
}

const XmlElement* FindTopLevelSchemaDeclaration(
    const XmlElement& schemaRoot,
    const std::string& schemaNamespace,
    const std::string& declarationLocalName,
    const std::string& declarationName) {
    for (const auto& child : schemaRoot.ChildNodes()) {
        if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
            continue;
        }

        const auto* childElement = static_cast<const XmlElement*>(child.get());
        if (childElement->NamespaceURI() != schemaNamespace || childElement->LocalName() != declarationLocalName) {
            continue;
        }
        if (AttributeValueEqualsInternal(*childElement, "name", declarationName)) {
            return childElement;
        }
    }

    return nullptr;
}

std::string BuildRedefineAliasName(const std::string& declarationName, std::size_t index) {
    return declarationName + "__libxml_redefine_base_" + std::to_string(index);
}

bool ShouldRewriteRedefineReference(
    const XmlElement& context,
    const std::string& qualifiedName,
    const std::string& expectedLocalName,
    const std::string& targetNamespace) {
    if (qualifiedName.empty()) {
        return false;
    }

    const auto separator = qualifiedName.find(':');
    if (separator == std::string::npos) {
        return qualifiedName == expectedLocalName;
    }

    const std::string prefix = qualifiedName.substr(0, separator);
    const std::string localName = qualifiedName.substr(separator + 1);
    return localName == expectedLocalName && context.GetNamespaceOfPrefix(prefix) == targetNamespace;
}

std::string RewriteRedefineQualifiedName(
    const XmlElement& context,
    const std::string& qualifiedName,
    const std::string& expectedLocalName,
    const std::string& aliasName,
    const std::string& targetNamespace) {
    if (!ShouldRewriteRedefineReference(context, qualifiedName, expectedLocalName, targetNamespace)) {
        return qualifiedName;
    }

    const auto separator = qualifiedName.find(':');
    if (separator == std::string::npos) {
        return aliasName;
    }

    return qualifiedName.substr(0, separator + 1) + aliasName;
}

void RewriteRedefineReferences(
    const std::shared_ptr<XmlNode>& node,
    const std::string& schemaNamespace,
    const std::string& targetNamespace,
    const std::string& originalName,
    const std::string& aliasName,
    const std::string& redefineLocalName) {
    if (node == nullptr || node->NodeType() != XmlNodeType::Element) {
        return;
    }

    auto element = std::static_pointer_cast<XmlElement>(node);
    if (element->NamespaceURI() == schemaNamespace) {
        const std::string& localName = element->LocalName();
        std::string_view attributeValue;
        if ((localName == "restriction" || localName == "extension")
            && TryGetAttributeValueViewInternal(*element, "base", attributeValue)
            && !attributeValue.empty()) {
            element->SetAttribute(
                "base",
                RewriteRedefineQualifiedName(*element, std::string(attributeValue), originalName, aliasName, targetNamespace));
        }
        if (localName == redefineLocalName
            && TryGetAttributeValueViewInternal(*element, "ref", attributeValue)
            && !attributeValue.empty()) {
            element->SetAttribute(
                "ref",
                RewriteRedefineQualifiedName(*element, std::string(attributeValue), originalName, aliasName, targetNamespace));
        }
        if (localName == "list"
            && TryGetAttributeValueViewInternal(*element, "itemType", attributeValue)
            && !attributeValue.empty()) {
            element->SetAttribute(
                "itemType",
                RewriteRedefineQualifiedName(*element, std::string(attributeValue), originalName, aliasName, targetNamespace));
        }
        if (localName == "union"
            && TryGetAttributeValueViewInternal(*element, "memberTypes", attributeValue)
            && !attributeValue.empty()) {
            std::istringstream members{std::string(attributeValue)};
            std::string rewrittenMemberTypes;
            std::string memberType;
            bool first = true;
            while (members >> memberType) {
                if (!first) {
                    rewrittenMemberTypes.push_back(' ');
                }
                rewrittenMemberTypes += RewriteRedefineQualifiedName(*element, memberType, originalName, aliasName, targetNamespace);
                first = false;
            }
            element->SetAttribute("memberTypes", rewrittenMemberTypes);
        }
    }

    for (const auto& child : element->ChildNodes()) {
        RewriteRedefineReferences(child, schemaNamespace, targetNamespace, originalName, aliasName, redefineLocalName);
    }
}

void CopySchemaNamespaceBindings(const XmlElement& schemaRoot, const std::shared_ptr<XmlElement>& declarationElement) {
    if (declarationElement == nullptr) {
        return;
    }

    for (const auto& attribute : schemaRoot.Attributes()) {
        if (attribute == nullptr) {
            continue;
        }

        const std::string& attributeName = attribute->Name();
        if (attributeName == "xmlns" || attributeName.rfind("xmlns:", 0) == 0) {
            declarationElement->SetAttribute(attributeName, attribute->Value());
        }
    }
}

std::string BuildSchemaAddXmlPayload(
    const XmlElement& schemaRoot,
    const std::filesystem::path& schemaPath,
    const std::string& schemaNamespace) {
    const std::string targetNamespace = schemaRoot.GetAttribute("targetNamespace");
    std::string payload = "<" + schemaRoot.Name();
    for (const auto& attribute : schemaRoot.Attributes()) {
        if (attribute == nullptr) {
            continue;
        }
        payload.push_back(' ');
        payload += attribute->OuterXml();
    }
    payload.push_back('>');

    for (const auto& child : schemaRoot.ChildNodes()) {
        if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
            continue;
        }

        const auto* childElement = static_cast<const XmlElement*>(child.get());
        if (childElement->NamespaceURI() != schemaNamespace) {
            continue;
        }

        if (childElement->LocalName() == "override") {
            for (const auto& overrideChild : childElement->ChildNodes()) {
                if (overrideChild == nullptr || overrideChild->NodeType() != XmlNodeType::Element) {
                    continue;
                }
                const auto* overrideChildElement = static_cast<const XmlElement*>(overrideChild.get());
                if (overrideChildElement->NamespaceURI() != schemaNamespace) {
                    continue;
                }
                const std::string& overrideLocalName = overrideChildElement->LocalName();
                if (overrideLocalName != "annotation"
                    && overrideLocalName != "element"
                    && overrideLocalName != "attribute"
                    && overrideLocalName != "simpleType"
                    && overrideLocalName != "complexType"
                    && overrideLocalName != "group"
                    && overrideLocalName != "attributeGroup") {
                    throw XmlException("XML Schema xs:override can only contain annotation, element, attribute, simpleType, complexType, group, and attributeGroup declarations");
                }
                payload += overrideChildElement->OuterXml();
            }
            continue;
        }

        if (childElement->LocalName() == "include" || childElement->LocalName() == "import") {
            for (const auto& referenceChild : childElement->ChildNodes()) {
                if (referenceChild == nullptr || referenceChild->NodeType() != XmlNodeType::Element) {
                    continue;
                }
                const auto* referenceChildElement = static_cast<const XmlElement*>(referenceChild.get());
                if (referenceChildElement->NamespaceURI() == schemaNamespace
                    && referenceChildElement->LocalName() != "annotation") {
                    throw XmlException("XML Schema xs:" + childElement->LocalName() + " can only declare annotation children");
                }
            }
        }

        if (childElement->LocalName() == "redefine") {
            const std::string schemaLocation = childElement->GetAttribute("schemaLocation");
            if (schemaLocation.empty()) {
                throw XmlException("XML Schema redefine requires a schemaLocation when loaded via AddFile");
            }

            const std::filesystem::path referencedPath = NormalizeSchemaPath(schemaPath.parent_path() / std::filesystem::path(schemaLocation));
            std::ifstream referencedStream(referencedPath, std::ios::binary);
            if (!referencedStream) {
                throw XmlException("Failed to open referenced XML schema file: " + referencedPath.generic_string());
            }

            std::ostringstream referencedBuffer;
            referencedBuffer << referencedStream.rdbuf();
            const auto referencedDocument = XmlDocument::Parse(referencedBuffer.str());
            const auto referencedRoot = referencedDocument->DocumentElement();
            if (referencedRoot == nullptr || referencedRoot->LocalName() != "schema" || referencedRoot->NamespaceURI() != schemaNamespace) {
                throw XmlException("Referenced XML schema file is not a valid XML Schema document: " + referencedPath.generic_string());
            }

            std::size_t redefineIndex = 0;
            for (const auto& redefineChild : childElement->ChildNodes()) {
                if (redefineChild == nullptr || redefineChild->NodeType() != XmlNodeType::Element) {
                    continue;
                }

                const auto* redefineChildElement = static_cast<const XmlElement*>(redefineChild.get());
                if (redefineChildElement->NamespaceURI() != schemaNamespace) {
                    continue;
                }

                const std::string& redefineLocalName = redefineChildElement->LocalName();
                if (redefineLocalName != "simpleType"
                    && redefineLocalName != "complexType"
                    && redefineLocalName != "group"
                    && redefineLocalName != "attributeGroup") {
                    throw XmlException("XML Schema xs:redefine currently supports only top-level simpleType, complexType, group, and attributeGroup declarations");
                }

                const std::string declarationName = redefineChildElement->GetAttribute("name");
                if (declarationName.empty()) {
                    throw XmlException("XML Schema xs:redefine declarations require a name");
                }

                const auto* originalDeclaration = FindTopLevelSchemaDeclaration(
                    *referencedRoot,
                    schemaNamespace,
                    redefineLocalName,
                    declarationName);
                if (originalDeclaration == nullptr) {
                    throw XmlException(
                        "XML Schema xs:redefine could not find the referenced " + redefineLocalName + " '" + declarationName + "'");
                }

                const std::string aliasName = BuildRedefineAliasName(declarationName, redefineIndex++);
                auto originalClone = std::static_pointer_cast<XmlElement>(originalDeclaration->CloneNode(true));
                CopySchemaNamespaceBindings(schemaRoot, originalClone);
                originalClone->SetAttribute("name", aliasName);
                payload += originalClone->OuterXml();

                auto redefineClone = std::static_pointer_cast<XmlElement>(redefineChildElement->CloneNode(true));
                CopySchemaNamespaceBindings(schemaRoot, redefineClone);
                RewriteRedefineReferences(redefineClone, schemaNamespace, targetNamespace, declarationName, aliasName, redefineLocalName);
                payload += redefineClone->OuterXml();
            }
            continue;
        }

        payload += childElement->OuterXml();
    }

    payload += "</" + schemaRoot.Name() + ">";
    return payload;
}

const std::string& EmptyString() {
    static const std::string empty;
    return empty;
}

std::string BuildDeclarationValue(
    const std::string& version,
    const std::string& encoding,
    const std::string& standalone) {
    std::string value = "version=\"" + version + "\"";
    if (!encoding.empty()) {
        value += " encoding=\"" + encoding + "\"";
    }
    if (!standalone.empty()) {
        value += " standalone=\"" + standalone + "\"";
    }
    return value;
}

std::string BuildDeclarationValue(const XmlDeclaration& declaration) {
    return BuildDeclarationValue(declaration.Version(), declaration.Encoding(), declaration.Standalone());
}

void CollectElementsByName(
    const XmlNode& node,
    const std::string& name,
    std::vector<std::shared_ptr<XmlElement>>& results) {
    for (const auto& child : node.ChildNodes()) {
        if (child->NodeType() == XmlNodeType::Element) {
            auto element = std::static_pointer_cast<XmlElement>(child);
            if (name == "*" || element->Name() == name || element->LocalName() == name) {
                results.push_back(element);
            }

            CollectElementsByName(*element, name, results);
        }
    }
}

void CollectElementsByNameNS(
    const XmlNode& node,
    const std::string& localName,
    const std::string& namespaceUri,
    std::vector<std::shared_ptr<XmlElement>>& results) {
    for (const auto& child : node.ChildNodes()) {
        if (child->NodeType() == XmlNodeType::Element) {
            auto element = std::static_pointer_cast<XmlElement>(child);
            const bool nameMatch = (localName == "*" || element->LocalName() == localName);
            const bool nsMatch = (namespaceUri == "*" || element->NamespaceURI() == namespaceUri);
            if (nameMatch && nsMatch) {
                results.push_back(element);
            }
            CollectElementsByNameNS(*element, localName, namespaceUri, results);
        }
    }
}

struct XPathName {
    bool wildcard = false;
    std::string name;
    std::string prefix;
    std::string localName;
    bool attribute = false;
};

struct XPathStep {
    enum class Axis {
        Child,
        Descendant,
        DescendantOrSelf,
        Parent,
        Ancestor,
        AncestorOrSelf,
        FollowingSibling,
        PrecedingSibling,
        Following,
        Preceding,
        Self,
        Attribute,
    };

    Axis axis = Axis::Child;
    bool descendant = false;
    bool self = false;
    bool attribute = false;
    bool textNode = false;
    bool nodeTest = false;
    XPathName name;
    struct PredicatePathSegment {
        enum class Kind {
            Self,
            Parent,
            Element,
            Attribute,
            Text,
        };

        Kind kind = Kind::Element;
        XPathName name;
    };

    struct PredicateTarget {
        enum class Kind {
            None,
            Literal,
            Text,
            Attribute,
            ContextNode,
            ChildPath,
            ContextText,
            Name,
            LocalName,
            NamespaceUri,
            Position,
            LastPosition,
            BooleanLiteral,
            Not,
            Boolean,
            Number,
            String,
            Concat,
            Translate,
            NormalizeSpace,
            StringLength,
            Count,
            Sum,
            Floor,
            Ceiling,
            Round,
            Add,
            Subtract,
            Multiply,
            Divide,
            Modulo,
            Substring,
            SubstringBefore,
            SubstringAfter,
        };

        Kind kind = Kind::None;
        std::string literal;
        bool numericLiteral = false;
        XPathName name;
        std::vector<PredicatePathSegment> path;
        std::vector<PredicateTarget> arguments;
    };

    struct Predicate {
        enum class Kind {
            Exists,
            Equals,
            NotEquals,
            LessThan,
            LessThanOrEqual,
            GreaterThan,
            GreaterThanOrEqual,
            True,
            False,
            Contains,
            StartsWith,
            EndsWith,
            PositionEquals,
            Last,
            And,
            Or,
            Not,
        };

        Kind kind = Kind::Exists;
        PredicateTarget target;
        PredicateTarget comparisonTarget;
        bool hasComparisonTarget = false;
        std::size_t position = 0;
        std::vector<Predicate> operands;
    };
    std::vector<Predicate> predicates;
};

struct XPathContext {
    const XmlNode* node = nullptr;
    std::shared_ptr<XmlNode> shared;
};

std::shared_ptr<XmlNode> FindSharedNode(const XmlNode& node) {
    const auto* parent = node.ParentNode();
    if (parent == nullptr) {
        return nullptr;
    }

    if (node.NodeType() == XmlNodeType::Attribute && parent->NodeType() == XmlNodeType::Element) {
        const auto& attributes = static_cast<const XmlElement*>(parent)->Attributes();
        const auto found = std::find_if(attributes.begin(), attributes.end(), [&node](const auto& attribute) {
            return attribute.get() == &node;
        });
        return found == attributes.end() ? nullptr : *found;
    }

    const auto& siblings = parent->ChildNodes();
    const auto found = std::find_if(siblings.begin(), siblings.end(), [&node](const auto& child) {
        return child.get() == &node;
    });
    return found == siblings.end() ? nullptr : *found;
}

const XmlElement* FindTopElement(const XmlElement& element) {
    const XmlElement* current = &element;
    while (current->ParentNode() != nullptr && current->ParentNode()->NodeType() == XmlNodeType::Element) {
        current = static_cast<const XmlElement*>(current->ParentNode());
    }
    return current;
}

XPathName ParseXPathName(std::string token, bool attribute) {
    XPathName name;
    name.attribute = attribute;
    name.name = std::move(token);
    name.wildcard = name.name == "*" || name.name == "@*";

    std::string working = name.name;
    if (attribute && !working.empty() && working.front() == '@') {
        working.erase(working.begin());
    }

    if (working == "*") {
        name.wildcard = true;
        name.localName.clear();
        return name;
    }

    const auto separator = working.find(':');
    if (separator == std::string::npos) {
        name.localName = working;
        return name;
    }

    name.prefix = working.substr(0, separator);
    name.localName = working.substr(separator + 1);
    if (name.localName == "*") {
        name.wildcard = true;
    }
    return name;
}

bool MatchesXPathQualifiedName(const XmlNode& node, const XPathName& name, const XmlNamespaceManager* namespaces);
bool IsXPathTextNode(const XmlNode& node);

[[noreturn]] void ThrowUnsupportedXPathFeature(const std::string& detail) {
    throw XmlException("Unsupported XPath feature: " + detail);
}

[[noreturn]] void ThrowInvalidXPathPredicate(const std::string& detail) {
    throw XmlException("Invalid XPath predicate: " + detail);
}

bool IsXPathNodeSetTargetKind(XPathStep::PredicateTarget::Kind kind) {
    return kind == XPathStep::PredicateTarget::Kind::ContextNode
        || kind == XPathStep::PredicateTarget::Kind::Text
        || kind == XPathStep::PredicateTarget::Kind::Attribute
        || kind == XPathStep::PredicateTarget::Kind::ChildPath;
}

bool IsXPathNumericTargetKind(const XPathStep::PredicateTarget& target) {
    return target.kind == XPathStep::PredicateTarget::Kind::Number
        || target.kind == XPathStep::PredicateTarget::Kind::Count
        || target.kind == XPathStep::PredicateTarget::Kind::StringLength
        || target.kind == XPathStep::PredicateTarget::Kind::Position
        || target.kind == XPathStep::PredicateTarget::Kind::LastPosition
        || target.kind == XPathStep::PredicateTarget::Kind::Sum
        || target.kind == XPathStep::PredicateTarget::Kind::Floor
        || target.kind == XPathStep::PredicateTarget::Kind::Ceiling
        || target.kind == XPathStep::PredicateTarget::Kind::Round
        || target.kind == XPathStep::PredicateTarget::Kind::Add
        || target.kind == XPathStep::PredicateTarget::Kind::Subtract
        || target.kind == XPathStep::PredicateTarget::Kind::Multiply
        || target.kind == XPathStep::PredicateTarget::Kind::Divide
        || target.kind == XPathStep::PredicateTarget::Kind::Modulo
        || (target.kind == XPathStep::PredicateTarget::Kind::Literal && target.numericLiteral);
}

bool IsXPathBooleanTargetKind(const XPathStep::PredicateTarget::Kind kind) {
    return kind == XPathStep::PredicateTarget::Kind::Boolean
    || kind == XPathStep::PredicateTarget::Kind::Not
        || kind == XPathStep::PredicateTarget::Kind::BooleanLiteral;
}

std::string TrimAsciiWhitespace(std::string value);
bool TryParseXPathNumber(const std::string& expression, double& value);

std::string FormatXPathNumber(double value) {
    if (std::isnan(value)) {
        return "NaN";
    }

    if (std::isinf(value)) {
        return value < 0 ? "-Infinity" : "Infinity";
    }

    std::ostringstream stream;
    stream << std::setprecision(15) << value;
    std::string text = stream.str();
    const auto exponent = text.find_first_of("eE");
    if (exponent != std::string::npos) {
        return text;
    }

    if (const auto decimal = text.find('.'); decimal != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }

    if (text == "-0") {
        return "0";
    }
    return text.empty() ? "0" : text;
}

double ParseXPathNumberOrNaN(const std::string& text) {
    double value = 0.0;
    if (TryParseXPathNumber(TrimAsciiWhitespace(text), value)) {
        return value;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

bool ParseXPathBooleanLiteral(const std::string& text) {
    return text == "true";
}

double RoundXPathNumber(double value) {
    if (std::isnan(value) || std::isinf(value) || value == 0.0) {
        return value;
    }

    return std::floor(value + 0.5);
}

std::string TranslateXPathString(const std::string& value, const std::string& from, const std::string& to) {
    std::string translated;
    translated.reserve(value.size());

    for (const char ch : value) {
        const auto position = from.find(ch);
        if (position == std::string::npos) {
            translated.push_back(ch);
            continue;
        }

        if (position < to.size()) {
            translated.push_back(to[position]);
        }
    }

    return translated;
}

std::vector<XPathStep::PredicatePathSegment> ParseXPathRelativeNamePath(const std::string& expression) {
    std::vector<XPathStep::PredicatePathSegment> path;
    std::size_t start = 0;
    while (start < expression.size()) {
        const auto separator = expression.find('/', start);
        const std::string segment = Trim(expression.substr(start, separator == std::string::npos ? std::string::npos : separator - start));
        if (segment.empty()) {
            ThrowUnsupportedXPathFeature("predicate path [" + expression + "]");
        }

        XPathStep::PredicatePathSegment parsed;
        if (segment == ".") {
            parsed.kind = XPathStep::PredicatePathSegment::Kind::Self;
        } else if (segment == "..") {
            parsed.kind = XPathStep::PredicatePathSegment::Kind::Parent;
        } else if (segment == "text()") {
            parsed.kind = XPathStep::PredicatePathSegment::Kind::Text;
        } else if (!segment.empty() && segment[0] == '@') {
            parsed.kind = XPathStep::PredicatePathSegment::Kind::Attribute;
            parsed.name = ParseXPathName(segment, true);
        } else {
            parsed.kind = XPathStep::PredicatePathSegment::Kind::Element;
            parsed.name = ParseXPathName(segment, false);
        }

        if (!path.empty()
            && path.back().kind != XPathStep::PredicatePathSegment::Kind::Element
            && path.back().kind != XPathStep::PredicatePathSegment::Kind::Self
            && path.back().kind != XPathStep::PredicatePathSegment::Kind::Parent) {
            ThrowUnsupportedXPathFeature("predicate path [" + expression + "]");
        }

        path.push_back(std::move(parsed));
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }

    if (path.empty()) {
        ThrowUnsupportedXPathFeature("predicate path [" + expression + "]");
    }

    return path;
}

bool MatchesXPathElementPathValue(
    const XmlNode& node,
    const std::vector<XPathStep::PredicatePathSegment>& path,
    const std::string& value,
    const XmlNamespaceManager* namespaces,
    std::size_t index = 0) {
    if (index >= path.size()) {
        return false;
    }

    const auto& segment = path[index];
    if (segment.kind == XPathStep::PredicatePathSegment::Kind::Self) {
        return MatchesXPathElementPathValue(node, path, value, namespaces, index + 1);
    }

    if (segment.kind == XPathStep::PredicatePathSegment::Kind::Parent) {
        const auto* parent = node.ParentNode();
        if (parent == nullptr) {
            return false;
        }

        if (index + 1 == path.size()) {
            return parent->InnerText() == value;
        }

        return MatchesXPathElementPathValue(*parent, path, value, namespaces, index + 1);
    }

    if (segment.kind == XPathStep::PredicatePathSegment::Kind::Text) {
        if (index + 1 != path.size()) {
            return false;
        }

        for (const auto& child : node.ChildNodes()) {
            if (child && IsXPathTextNode(*child) && child->Value() == value) {
                return true;
            }
        }
        return false;
    }

    if (segment.kind == XPathStep::PredicatePathSegment::Kind::Attribute) {
        if (index + 1 != path.size() || node.NodeType() != XmlNodeType::Element) {
            return false;
        }

        for (const auto& attribute : static_cast<const XmlElement*>(&node)->Attributes()) {
            if (MatchesXPathQualifiedName(*attribute, segment.name, namespaces) && attribute->Value() == value) {
                return true;
            }
        }
        return false;
    }

    for (const auto& child : node.ChildNodes()) {
        if (!child || child->NodeType() != XmlNodeType::Element || !MatchesXPathQualifiedName(*child, segment.name, namespaces)) {
            continue;
        }

        if (index + 1 == path.size()) {
            if (child->InnerText() == value) {
                return true;
            }
            continue;
        }

        if (MatchesXPathElementPathValue(*child, path, value, namespaces, index + 1)) {
            return true;
        }
    }

    return false;
}

std::string ResolveXPathNamespaceUri(const std::string& prefix, const XmlNamespaceManager* namespaces) {
    if (prefix.empty()) {
        return {};
    }

    if (prefix == "xml") {
        return "http://www.w3.org/XML/1998/namespace";
    }
    if (prefix == "xmlns") {
        return "http://www.w3.org/2000/xmlns/";
    }
    if (namespaces == nullptr) {
        return {};
    }

    const auto uri = namespaces->LookupNamespace(prefix);
    if (uri.empty()) {
        throw XmlException("Undefined XPath namespace prefix: " + prefix);
    }
    return uri;
}

bool MatchesXPathQualifiedName(const XmlNode& node, const XPathName& name, const XmlNamespaceManager* namespaces) {
    if (namespaces == nullptr) {
        if (name.wildcard) {
            if (!name.prefix.empty()) {
                const auto separator = node.Name().find(':');
                return separator != std::string::npos && node.Name().substr(0, separator) == name.prefix;
            }
            return true;
        }

        if (!name.prefix.empty()) {
            return node.Name() == name.name;
        }

        return node.Name() == name.name || node.LocalName() == name.localName;
    }

    if (name.prefix.empty()) {
        if (name.wildcard) {
            return true;
        }
        return node.LocalName() == name.localName && node.NamespaceURI().empty();
    }

    const auto expectedNamespace = ResolveXPathNamespaceUri(name.prefix, namespaces);
    if (name.wildcard) {
        return node.NamespaceURI() == expectedNamespace;
    }

    return node.LocalName() == name.localName && node.NamespaceURI() == expectedNamespace;
}

bool IsXPathTextNode(const XmlNode& node) {
    return node.NodeType() == XmlNodeType::Text || node.NodeType() == XmlNodeType::CDATA;
}

std::string TrimAsciiWhitespace(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    if (first == value.end()) {
        return {};
    }

    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    return std::string(first, last);
}

bool IsWrappedXPathExpression(std::string_view expression) {
    if (expression.size() < 2 || expression.front() != '(' || expression.back() != ')') {
        return false;
    }

    int depth = 0;
    char quote = '\0';
    for (std::size_t index = 0; index < expression.size(); ++index) {
        const char ch = expression[index];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }

        if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            --depth;
            if (depth == 0 && index + 1 < expression.size()) {
                return false;
            }
        }
    }

    return depth == 0;
}

std::vector<std::string> SplitXPathExpressionTopLevel(std::string_view expression, std::string_view separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    int depth = 0;
    char quote = '\0';

    for (std::size_t index = 0; index < expression.size(); ++index) {
        const char ch = expression[index];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }

        if (ch == '(') {
            ++depth;
            continue;
        }

        if (ch == ')') {
            --depth;
            continue;
        }

        if (depth == 0 && index + separator.size() <= expression.size()
            && expression.substr(index, separator.size()) == separator) {
            parts.push_back(TrimAsciiWhitespace(std::string(expression.substr(start, index - start))));
            start = index + separator.size();
            index += separator.size() - 1;
        }
    }

    if (start == 0) {
        return {};
    }

    parts.push_back(TrimAsciiWhitespace(std::string(expression.substr(start))));
    return parts;
}

std::size_t FindXPathExpressionTopLevelCharacter(std::string_view expression, char target) {
    int depth = 0;
    char quote = '\0';
    for (std::size_t index = 0; index < expression.size(); ++index) {
        const char ch = expression[index];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }

        if (ch == '(') {
            ++depth;
            continue;
        }

        if (ch == ')') {
            --depth;
            continue;
        }

        if (depth == 0 && ch == target) {
            return index;
        }
    }

    return std::string_view::npos;
}

std::string ParseXPathQuotedLiteral(const std::string& expression, const std::string& predicate) {
    if (expression.size() < 2) {
        ThrowUnsupportedXPathFeature("predicate [" + predicate + "]");
    }

    const char quote = expression.front();
    if ((quote != '\'' && quote != '"') || expression.back() != quote) {
        ThrowUnsupportedXPathFeature("predicate [" + predicate + "]");
    }

    return expression.substr(1, expression.size() - 2);
}

bool TryParseXPathNumber(const std::string& expression, double& value) {
    try {
        std::size_t consumed = 0;
        value = std::stod(expression, &consumed);
        return consumed == expression.size();
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<std::string> ParseXPathFunctionArguments(std::string_view expression) {
    std::vector<std::string> arguments;
    const std::string trimmed = TrimAsciiWhitespace(std::string(expression));
    if (trimmed.empty()) {
        return arguments;
    }

    std::size_t start = 0;
    int depth = 0;
    char quote = '\0';
    for (std::size_t index = 0; index < trimmed.size(); ++index) {
        const char ch = trimmed[index];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }

        if (ch == '(') {
            ++depth;
            continue;
        }

        if (ch == ')') {
            --depth;
            continue;
        }

        if (depth == 0 && ch == ',') {
            arguments.push_back(TrimAsciiWhitespace(trimmed.substr(start, index - start)));
            start = index + 1;
        }
    }

    arguments.push_back(TrimAsciiWhitespace(trimmed.substr(start)));
    return arguments;
}

XPathStep::PredicateTarget ParseXPathPredicateTarget(const std::string& expression, const std::string& predicate);

std::optional<std::vector<std::string>> TryParseXPathFunctionCallArguments(
    const std::string& normalized,
    std::string_view functionName) {
    if (normalized.rfind(functionName, 0) != 0 || normalized.size() <= functionName.size()) {
        return std::nullopt;
    }

    const std::string_view wrapped = std::string_view(normalized).substr(functionName.size());
    if (wrapped.empty() || !IsWrappedXPathExpression(wrapped)) {
        return std::nullopt;
    }

    return ParseXPathFunctionArguments(
        wrapped.substr(1, wrapped.size() - 2));
}

void AppendXPathParsedTargetArguments(
    XPathStep::PredicateTarget& target,
    const std::vector<std::string>& arguments,
    const std::string& expression) {
    for (const auto& argument : arguments) {
        target.arguments.push_back(ParseXPathPredicateTarget(argument, expression));
    }
}

bool TryParseXPathFixedArityTarget(
    XPathStep::PredicateTarget& target,
    const std::string& normalized,
    const std::string& expression,
    std::string_view functionName,
    XPathStep::PredicateTarget::Kind kind,
    std::size_t arity) {
    const auto arguments = TryParseXPathFunctionCallArguments(normalized, functionName);
    if (!arguments.has_value()) {
        return false;
    }

    if (arguments->size() != arity) {
        ThrowUnsupportedXPathFeature("predicate [" + expression + "]");
    }

    target.kind = kind;
    AppendXPathParsedTargetArguments(target, *arguments, expression);
    return true;
}

bool TryParseXPathUnaryOptionalTarget(
    XPathStep::PredicateTarget& target,
    const std::string& normalized,
    const std::string& expression,
    std::string_view functionName,
    XPathStep::PredicateTarget::Kind kind) {
    const auto arguments = TryParseXPathFunctionCallArguments(normalized, functionName);
    if (!arguments.has_value()) {
        return false;
    }

    if (arguments->size() > 1) {
        ThrowUnsupportedXPathFeature("predicate [" + expression + "]");
    }

    target.kind = kind;
    AppendXPathParsedTargetArguments(target, *arguments, expression);
    return true;
}

bool TryParseXPathBinaryPredicateFunction(
    XPathStep::Predicate& predicate,
    const std::string& expression,
    const std::string& fullPredicate,
    std::string_view functionName,
    XPathStep::Predicate::Kind kind) {
    const auto arguments = TryParseXPathFunctionCallArguments(expression, functionName);
    if (!arguments.has_value()) {
        return false;
    }

    if (arguments->size() != 2) {
        ThrowUnsupportedXPathFeature("predicate [" + fullPredicate + "]");
    }

    predicate.kind = kind;
    predicate.target = ParseXPathPredicateTarget((*arguments)[0], fullPredicate);
    predicate.comparisonTarget = ParseXPathPredicateTarget((*arguments)[1], fullPredicate);
    predicate.hasComparisonTarget = true;
    return true;
}

std::size_t FindXPathExpressionTopLevelOperator(std::string_view expression, std::string_view token) {
    int depth = 0;
    char quote = '\0';
    for (std::size_t index = 0; index + token.size() <= expression.size(); ++index) {
        const char ch = expression[index];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }

        if (ch == '(') {
            ++depth;
            continue;
        }

        if (ch == ')') {
            --depth;
            continue;
        }

        if (depth == 0 && expression.substr(index, token.size()) == token) {
            return index;
        }
    }

    return std::string_view::npos;
}

std::size_t FindXPathExpressionTopLevelOperatorRightmost(std::string_view expression, std::string_view token) {
    int depth = 0;
    char quote = '\0';
    std::size_t match = std::string_view::npos;
    for (std::size_t index = 0; index + token.size() <= expression.size(); ++index) {
        const char ch = expression[index];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }

        if (ch == '(') {
            ++depth;
            continue;
        }

        if (ch == ')') {
            --depth;
            continue;
        }

        if (depth == 0 && expression.substr(index, token.size()) == token) {
            match = index;
        }
    }

    return match;
}

std::size_t FindXPathTopLevelAdditiveOperator(std::string_view expression) {
    int depth = 0;
    char quote = '\0';
    std::size_t match = std::string_view::npos;
    for (std::size_t index = 0; index < expression.size(); ++index) {
        const char ch = expression[index];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }

        if (ch == '(') {
            ++depth;
            continue;
        }

        if (ch == ')') {
            --depth;
            continue;
        }

        if (depth != 0 || (ch != '+' && ch != '-')) {
            continue;
        }

        if (index == 0) {
            continue;
        }

        const char previous = expression[index - 1];
        const char next = index + 1 < expression.size() ? expression[index + 1] : '\0';
        if (previous == '+' || previous == '-' || previous == '*' || previous == '/' || previous == '(') {
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(previous)) != 0
            && std::isalpha(static_cast<unsigned char>(next)) != 0) {
            continue;
        }

        match = index;
    }

    return match;
}

std::size_t FindXPathTopLevelMultiplyOperator(std::string_view expression) {
    int depth = 0;
    char quote = '\0';
    std::size_t match = std::string_view::npos;
    for (std::size_t index = 0; index < expression.size(); ++index) {
        const char ch = expression[index];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }

        if (ch == '(') {
            ++depth;
            continue;
        }

        if (ch == ')') {
            --depth;
            continue;
        }

        if (depth == 0 && ch == '*') {
            if (index == 0 || index + 1 >= expression.size()) {
                continue;
            }

            const char previous = expression[index - 1];
            if (previous == ':' || previous == '@' || previous == '/') {
                continue;
            }

            match = index;
        }
    }

    return match;
}

bool XPathLanguageMatches(const std::string& language, const std::string& requested) {
    if (requested.empty() || language.empty()) {
        return false;
    }

    std::string normalizedLanguage = language;
    std::string normalizedRequested = requested;
    std::transform(normalizedLanguage.begin(), normalizedLanguage.end(), normalizedLanguage.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    std::transform(normalizedRequested.begin(), normalizedRequested.end(), normalizedRequested.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalizedLanguage == normalizedRequested) {
        return true;
    }

    return normalizedLanguage.size() > normalizedRequested.size()
        && normalizedLanguage.compare(0, normalizedRequested.size(), normalizedRequested) == 0
        && normalizedLanguage[normalizedRequested.size()] == '-';
}

std::string ResolveXPathLanguage(const XmlNode& node) {
    const XmlNode* current = &node;
    while (current != nullptr) {
        if (current->NodeType() == XmlNodeType::Element) {
            const auto* element = static_cast<const XmlElement*>(current);
            for (const auto& attribute : element->Attributes()) {
                if (attribute != nullptr && attribute->Name() == "xml:lang") {
                    return attribute->Value();
                }
            }
        }
        current = current->ParentNode();
    }
    return {};
}

std::string ParseXPathPredicateLiteral(const std::string& expression, const std::string& predicate) {
    const std::string normalized = TrimAsciiWhitespace(expression);
    if (normalized.empty()) {
        ThrowUnsupportedXPathFeature("predicate [" + predicate + "]");
    }

    if (normalized.front() == '\'' || normalized.front() == '"') {
        return ParseXPathQuotedLiteral(normalized, predicate);
    }

    double numericValue = 0.0;
    if (TryParseXPathNumber(normalized, numericValue)) {
        return normalized;
    }

    ThrowUnsupportedXPathFeature("predicate [" + predicate + "]");
}

std::string NormalizeXPathStringValue(const std::string& value) {
    std::string normalized;
    bool inWhitespace = true;
    for (unsigned char ch : value) {
        if (std::isspace(ch) != 0) {
            inWhitespace = true;
            continue;
        }

        if (!normalized.empty() && inWhitespace) {
            normalized.push_back(' ');
        }
        normalized.push_back(static_cast<char>(ch));
        inWhitespace = false;
    }
    return normalized;
}

XPathStep::PredicateTarget ParseXPathPredicateTarget(const std::string& expression, const std::string&) {
    const std::string normalized = TrimAsciiWhitespace(expression);
    XPathStep::PredicateTarget target;
    if (!normalized.empty() && (normalized.front() == '\'' || normalized.front() == '"')) {
        target.kind = XPathStep::PredicateTarget::Kind::Literal;
        target.literal = ParseXPathQuotedLiteral(normalized, expression);
        return target;
    }

    double numericValue = 0.0;
    if (TryParseXPathNumber(normalized, numericValue)) {
        target.kind = XPathStep::PredicateTarget::Kind::Literal;
        target.literal = normalized;
        target.numericLiteral = true;
        return target;
    }

    if (normalized == ".") {
        target.kind = XPathStep::PredicateTarget::Kind::ContextNode;
        return target;
    }

    if (normalized == "text()") {
        target.kind = XPathStep::PredicateTarget::Kind::Text;
        return target;
    }

    if (normalized == "name()") {
        target.kind = XPathStep::PredicateTarget::Kind::Name;
        return target;
    }

    if (normalized == "local-name()") {
        target.kind = XPathStep::PredicateTarget::Kind::LocalName;
        return target;
    }

    if (normalized == "namespace-uri()") {
        target.kind = XPathStep::PredicateTarget::Kind::NamespaceUri;
        return target;
    }

    if (normalized == "position()") {
        target.kind = XPathStep::PredicateTarget::Kind::Position;
        return target;
    }

    if (normalized == "last()") {
        target.kind = XPathStep::PredicateTarget::Kind::LastPosition;
        return target;
    }

    if (normalized == "true()") {
        target.kind = XPathStep::PredicateTarget::Kind::BooleanLiteral;
        target.literal = "true";
        return target;
    }

    if (normalized == "false()") {
        target.kind = XPathStep::PredicateTarget::Kind::BooleanLiteral;
        target.literal = "false";
        return target;
    }

    if (TryParseXPathFixedArityTarget(target, normalized, expression, "not", XPathStep::PredicateTarget::Kind::Not, 1)) {
        return target;
    }

    if (TryParseXPathFixedArityTarget(target, normalized, expression, "lang", XPathStep::PredicateTarget::Kind::Boolean, 1)) {
        target.literal = "lang";
        return target;
    }

    const auto additive = FindXPathTopLevelAdditiveOperator(normalized);
    if (additive != std::string::npos) {
        target.kind = normalized[additive] == '+'
            ? XPathStep::PredicateTarget::Kind::Add
            : XPathStep::PredicateTarget::Kind::Subtract;
        target.arguments.push_back(ParseXPathPredicateTarget(normalized.substr(0, additive), expression));
        target.arguments.push_back(ParseXPathPredicateTarget(normalized.substr(additive + 1), expression));
        return target;
    }

    const auto divOperator = FindXPathExpressionTopLevelOperatorRightmost(normalized, " div ");
    if (divOperator != std::string::npos) {
        target.kind = XPathStep::PredicateTarget::Kind::Divide;
        target.arguments.push_back(ParseXPathPredicateTarget(normalized.substr(0, divOperator), expression));
        target.arguments.push_back(ParseXPathPredicateTarget(normalized.substr(divOperator + 5), expression));
        return target;
    }

    const auto modOperator = FindXPathExpressionTopLevelOperatorRightmost(normalized, " mod ");
    if (modOperator != std::string::npos) {
        target.kind = XPathStep::PredicateTarget::Kind::Modulo;
        target.arguments.push_back(ParseXPathPredicateTarget(normalized.substr(0, modOperator), expression));
        target.arguments.push_back(ParseXPathPredicateTarget(normalized.substr(modOperator + 5), expression));
        return target;
    }

    const auto multiplyOperator = FindXPathTopLevelMultiplyOperator(normalized);
    if (multiplyOperator != std::string::npos) {
        target.kind = XPathStep::PredicateTarget::Kind::Multiply;
        target.arguments.push_back(ParseXPathPredicateTarget(normalized.substr(0, multiplyOperator), expression));
        target.arguments.push_back(ParseXPathPredicateTarget(normalized.substr(multiplyOperator + 1), expression));
        return target;
    }

    if (TryParseXPathFixedArityTarget(target, normalized, expression, "boolean", XPathStep::PredicateTarget::Kind::Boolean, 1)) {
        return target;
    }

    if (TryParseXPathUnaryOptionalTarget(target, normalized, expression, "number", XPathStep::PredicateTarget::Kind::Number)) {
        return target;
    }

    if (TryParseXPathUnaryOptionalTarget(target, normalized, expression, "string", XPathStep::PredicateTarget::Kind::String)) {
        return target;
    }

    if (const auto arguments = TryParseXPathFunctionCallArguments(normalized, "concat"); arguments.has_value()) {
        if (arguments->size() < 2) {
            ThrowUnsupportedXPathFeature("predicate [" + expression + "]");
        }
        target.kind = XPathStep::PredicateTarget::Kind::Concat;
        AppendXPathParsedTargetArguments(target, *arguments, expression);
        return target;
    }

    if (TryParseXPathFixedArityTarget(target, normalized, expression, "translate", XPathStep::PredicateTarget::Kind::Translate, 3)) {
        return target;
    }

    if (TryParseXPathUnaryOptionalTarget(target, normalized, expression, "normalize-space", XPathStep::PredicateTarget::Kind::NormalizeSpace)) {
        return target;
    }

    if (TryParseXPathUnaryOptionalTarget(target, normalized, expression, "string-length", XPathStep::PredicateTarget::Kind::StringLength)) {
        return target;
    }

    if (TryParseXPathFixedArityTarget(target, normalized, expression, "count", XPathStep::PredicateTarget::Kind::Count, 1)) {
        return target;
    }

    if (TryParseXPathFixedArityTarget(target, normalized, expression, "sum", XPathStep::PredicateTarget::Kind::Sum, 1)) {
        return target;
    }

    if (TryParseXPathFixedArityTarget(target, normalized, expression, "floor", XPathStep::PredicateTarget::Kind::Floor, 1)) {
        return target;
    }

    if (TryParseXPathFixedArityTarget(target, normalized, expression, "ceiling", XPathStep::PredicateTarget::Kind::Ceiling, 1)) {
        return target;
    }

    if (TryParseXPathFixedArityTarget(target, normalized, expression, "round", XPathStep::PredicateTarget::Kind::Round, 1)) {
        return target;
    }

    if (TryParseXPathFixedArityTarget(target, normalized, expression, "substring-before", XPathStep::PredicateTarget::Kind::SubstringBefore, 2)) {
        return target;
    }

    if (TryParseXPathFixedArityTarget(target, normalized, expression, "substring-after", XPathStep::PredicateTarget::Kind::SubstringAfter, 2)) {
        return target;
    }

    if (const auto arguments = TryParseXPathFunctionCallArguments(normalized, "substring"); arguments.has_value()) {
        if (arguments->size() != 2 && arguments->size() != 3) {
            ThrowUnsupportedXPathFeature("predicate [" + expression + "]");
        }
        target.kind = XPathStep::PredicateTarget::Kind::Substring;
        AppendXPathParsedTargetArguments(target, *arguments, expression);
        return target;
    }

    if (!normalized.empty() && normalized[0] == '@') {
        target.kind = XPathStep::PredicateTarget::Kind::Attribute;
        target.name = ParseXPathName(normalized, true);
        return target;
    }

    target.kind = XPathStep::PredicateTarget::Kind::ChildPath;
    target.path = ParseXPathRelativeNamePath(normalized);
    for (const auto& segment : target.path) {
        if (segment.kind == XPathStep::PredicatePathSegment::Kind::Element) {
            target.name = segment.name;
            break;
        }
    }
    return target;
}

XPathStep::Predicate ParseXPathPredicateExpression(const std::string& predicate);

XPathStep::Predicate ParseXPathFunctionPredicate(const std::string& expression, const std::string& predicate) {
    XPathStep::Predicate result;
    if (TryParseXPathBinaryPredicateFunction(result, expression, predicate, "contains", XPathStep::Predicate::Kind::Contains)
        || TryParseXPathBinaryPredicateFunction(result, expression, predicate, "starts-with", XPathStep::Predicate::Kind::StartsWith)
        || TryParseXPathBinaryPredicateFunction(result, expression, predicate, "ends-with", XPathStep::Predicate::Kind::EndsWith)) {
        return result;
    }

    ThrowUnsupportedXPathFeature("predicate [" + predicate + "]");
}

std::string CoerceXPathTargetValuesToString(
    const XPathStep::PredicateTarget& target,
    const std::vector<std::string>& values) {
    if (values.empty()) {
        return {};
    }

    if (IsXPathBooleanTargetKind(target.kind)) {
        return ParseXPathBooleanLiteral(values.front()) ? "true" : "false";
    }

    if (IsXPathNumericTargetKind(target)) {
        return FormatXPathNumber(ParseXPathNumberOrNaN(values.front()));
    }

    return values.front();
}

bool CoerceXPathTargetValuesToBoolean(
    const XPathStep::PredicateTarget& target,
    const std::vector<std::string>& values) {
    if (IsXPathBooleanTargetKind(target.kind)) {
        return !values.empty() && ParseXPathBooleanLiteral(values.front());
    }

    if (IsXPathNumericTargetKind(target)) {
        const double value = values.empty() ? std::numeric_limits<double>::quiet_NaN() : ParseXPathNumberOrNaN(values.front());
        return !std::isnan(value) && value != 0.0;
    }

    if (IsXPathNodeSetTargetKind(target.kind)) {
        return !values.empty();
    }

    return !CoerceXPathTargetValuesToString(target, values).empty();
}

double CoerceXPathTargetValuesToNumber(
    const XPathStep::PredicateTarget& target,
    const std::vector<std::string>& values) {
    if (IsXPathBooleanTargetKind(target.kind)) {
        return CoerceXPathTargetValuesToBoolean(target, values) ? 1.0 : 0.0;
    }

    return ParseXPathNumberOrNaN(CoerceXPathTargetValuesToString(target, values));
}

bool CompareXPathStrings(
    const std::vector<std::string>& leftValues,
    const std::vector<std::string>& rightValues,
    bool equal) {
    for (const auto& left : leftValues) {
        for (const auto& right : rightValues) {
            if ((left == right) == equal) {
                return true;
            }
        }
    }
    return false;
}

bool CompareXPathNumbers(
    const std::vector<std::string>& leftValues,
    const std::vector<std::string>& rightValues,
    const std::function<bool(double, double)>& comparison) {
    for (const auto& leftText : leftValues) {
        const double left = ParseXPathNumberOrNaN(leftText);
        if (std::isnan(left)) {
            continue;
        }

        for (const auto& rightText : rightValues) {
            const double right = ParseXPathNumberOrNaN(rightText);
            if (!std::isnan(right) && comparison(left, right)) {
                return true;
            }
        }
    }
    return false;
}

void CollectXPathElementPathValues(
    const XmlNode& node,
    const std::vector<XPathStep::PredicatePathSegment>& path,
    const XmlNamespaceManager* namespaces,
    std::vector<std::string>& values,
    std::size_t index = 0) {
    if (index >= path.size()) {
        return;
    }

    const auto& segment = path[index];
    if (segment.kind == XPathStep::PredicatePathSegment::Kind::Self) {
        if (index + 1 == path.size()) {
            values.push_back(node.InnerText());
            return;
        }

        CollectXPathElementPathValues(node, path, namespaces, values, index + 1);
        return;
    }

    if (segment.kind == XPathStep::PredicatePathSegment::Kind::Parent) {
        const auto* parent = node.ParentNode();
        if (parent == nullptr) {
            return;
        }

        if (index + 1 == path.size()) {
            values.push_back(parent->InnerText());
            return;
        }

        CollectXPathElementPathValues(*parent, path, namespaces, values, index + 1);
        return;
    }

    if (segment.kind == XPathStep::PredicatePathSegment::Kind::Text) {
        if (index + 1 != path.size()) {
            return;
        }

        for (const auto& child : node.ChildNodes()) {
            if (child && IsXPathTextNode(*child)) {
                values.push_back(child->Value());
            }
        }
        return;
    }

    if (segment.kind == XPathStep::PredicatePathSegment::Kind::Attribute) {
        if (index + 1 != path.size() || node.NodeType() != XmlNodeType::Element) {
            return;
        }

        for (const auto& attribute : static_cast<const XmlElement*>(&node)->Attributes()) {
            if (MatchesXPathQualifiedName(*attribute, segment.name, namespaces)) {
                values.push_back(attribute->Value());
            }
        }
        return;
    }

    for (const auto& child : node.ChildNodes()) {
        if (!child || child->NodeType() != XmlNodeType::Element || !MatchesXPathQualifiedName(*child, segment.name, namespaces)) {
            continue;
        }

        if (index + 1 == path.size()) {
            values.push_back(child->InnerText());
            continue;
        }

        CollectXPathElementPathValues(*child, path, namespaces, values, index + 1);
    }
}

std::vector<std::string> ExtractXPathPredicateTargetValues(
    const XmlNode& node,
    const XPathStep::PredicateTarget& target,
    const XmlNamespaceManager* namespaces,
    std::size_t position = 0,
    std::size_t count = 0) {
    std::vector<std::string> values;
    auto extractArgumentValues = [&](const XPathStep::PredicateTarget& argument) {
        return ExtractXPathPredicateTargetValues(node, argument, namespaces, position, count);
    };
    auto firstValueOrEmpty = [&](const XPathStep::PredicateTarget& argument) {
        const auto argumentValues = extractArgumentValues(argument);
        return argumentValues.empty() ? std::string{} : argumentValues.front();
    };

    if (target.kind == XPathStep::PredicateTarget::Kind::Literal) {
        values.push_back(target.literal);
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::ContextNode) {
        values.push_back(node.InnerText());
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::ContextText) {
        values.push_back(node.InnerText());
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::Text) {
        for (const auto& child : node.ChildNodes()) {
            if (child && IsXPathTextNode(*child)) {
                values.push_back(child->Value());
            }
        }
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::Name) {
        values.push_back(node.Name());
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::LocalName) {
        values.push_back(node.LocalName());
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::NamespaceUri) {
        values.push_back(node.NamespaceURI());
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::Position) {
        values.push_back(std::to_string(position));
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::LastPosition) {
        values.push_back(std::to_string(count));
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::Boolean) {
        if (target.literal == "lang") {
            if (target.arguments.size() != 1) {
                return values;
            }

            const auto requestedLanguage = CoerceXPathTargetValuesToString(
                target.arguments.front(),
                extractArgumentValues(target.arguments.front()));
            values.push_back(XPathLanguageMatches(ResolveXPathLanguage(node), requestedLanguage) ? "true" : "false");
            return values;
        }

        if (target.arguments.size() != 1) {
            return values;
        }

        const auto argumentValues = extractArgumentValues(target.arguments.front());
        values.push_back(CoerceXPathTargetValuesToBoolean(target.arguments.front(), argumentValues) ? "true" : "false");
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::BooleanLiteral) {
        values.push_back(target.literal == "true" ? "true" : "false");
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::Not) {
        if (target.arguments.size() != 1) {
            return values;
        }

        const auto argumentValues = extractArgumentValues(target.arguments.front());
        values.push_back(CoerceXPathTargetValuesToBoolean(target.arguments.front(), argumentValues) ? "false" : "true");
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::Number) {
        const XPathStep::PredicateTarget* argument = target.arguments.empty() ? nullptr : &target.arguments.front();
        const auto sourceValues = argument == nullptr
            ? std::vector<std::string>{node.InnerText()}
            : extractArgumentValues(*argument);
        const auto number = argument == nullptr
            ? ParseXPathNumberOrNaN(node.InnerText())
            : CoerceXPathTargetValuesToNumber(*argument, sourceValues);
        values.push_back(FormatXPathNumber(number));
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::String) {
        const XPathStep::PredicateTarget* argument = target.arguments.empty() ? nullptr : &target.arguments.front();
        const auto sourceValues = argument == nullptr
            ? std::vector<std::string>{node.InnerText()}
            : extractArgumentValues(*argument);
        values.push_back(argument == nullptr ? node.InnerText() : CoerceXPathTargetValuesToString(*argument, sourceValues));
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::Concat) {
        std::string concatenated;
        for (const auto& argument : target.arguments) {
            concatenated += CoerceXPathTargetValuesToString(argument, extractArgumentValues(argument));
        }
        values.push_back(concatenated);
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::Translate) {
        if (target.arguments.size() != 3) {
            return values;
        }

        values.push_back(TranslateXPathString(
            CoerceXPathTargetValuesToString(target.arguments[0], extractArgumentValues(target.arguments[0])),
            CoerceXPathTargetValuesToString(target.arguments[1], extractArgumentValues(target.arguments[1])),
            CoerceXPathTargetValuesToString(target.arguments[2], extractArgumentValues(target.arguments[2]))));
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::NormalizeSpace) {
        const XPathStep::PredicateTarget* argument = target.arguments.empty() ? nullptr : &target.arguments.front();
        const auto sourceValues = argument == nullptr
            ? std::vector<std::string>{node.InnerText()}
            : extractArgumentValues(*argument);
        values.reserve(sourceValues.size());
        for (const auto& sourceValue : sourceValues) {
            values.push_back(NormalizeXPathStringValue(sourceValue));
        }
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::StringLength) {
        const XPathStep::PredicateTarget* argument = target.arguments.empty() ? nullptr : &target.arguments.front();
        const auto sourceValues = argument == nullptr
            ? std::vector<std::string>{node.InnerText()}
            : extractArgumentValues(*argument);
        values.reserve(sourceValues.size());
        for (const auto& sourceValue : sourceValues) {
            values.push_back(std::to_string(sourceValue.size()));
        }
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::Count) {
        if (target.arguments.size() != 1) {
            return values;
        }
        values.push_back(std::to_string(extractArgumentValues(target.arguments.front()).size()));
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::Sum) {
        if (target.arguments.size() != 1) {
            return values;
        }

        const auto sourceValues = extractArgumentValues(target.arguments.front());
        double sum = 0.0;
        bool sawNaN = false;
        for (const auto& sourceValue : sourceValues) {
            const double numeric = ParseXPathNumberOrNaN(sourceValue);
            if (std::isnan(numeric)) {
                sawNaN = true;
                break;
            }
            sum += numeric;
        }

        values.push_back(FormatXPathNumber(sawNaN ? std::numeric_limits<double>::quiet_NaN() : sum));
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::Floor
        || target.kind == XPathStep::PredicateTarget::Kind::Ceiling
        || target.kind == XPathStep::PredicateTarget::Kind::Round) {
        if (target.arguments.size() != 1) {
            return values;
        }

        const auto sourceValues = extractArgumentValues(target.arguments.front());
        double numeric = CoerceXPathTargetValuesToNumber(target.arguments.front(), sourceValues);
        if (!std::isnan(numeric) && !std::isinf(numeric)) {
            if (target.kind == XPathStep::PredicateTarget::Kind::Floor) {
                numeric = std::floor(numeric);
            } else if (target.kind == XPathStep::PredicateTarget::Kind::Ceiling) {
                numeric = std::ceil(numeric);
            } else {
                numeric = RoundXPathNumber(numeric);
            }
        }

        values.push_back(FormatXPathNumber(numeric));
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::Add
        || target.kind == XPathStep::PredicateTarget::Kind::Subtract
        || target.kind == XPathStep::PredicateTarget::Kind::Multiply
        || target.kind == XPathStep::PredicateTarget::Kind::Divide
        || target.kind == XPathStep::PredicateTarget::Kind::Modulo) {
        if (target.arguments.size() != 2) {
            return values;
        }

        const double left = CoerceXPathTargetValuesToNumber(
            target.arguments[0],
            extractArgumentValues(target.arguments[0]));
        const double right = CoerceXPathTargetValuesToNumber(
            target.arguments[1],
            extractArgumentValues(target.arguments[1]));

        double result = std::numeric_limits<double>::quiet_NaN();
        if (!std::isnan(left) && !std::isnan(right)) {
            switch (target.kind) {
            case XPathStep::PredicateTarget::Kind::Add:
                result = left + right;
                break;
            case XPathStep::PredicateTarget::Kind::Subtract:
                result = left - right;
                break;
            case XPathStep::PredicateTarget::Kind::Multiply:
                result = left * right;
                break;
            case XPathStep::PredicateTarget::Kind::Divide:
                result = left / right;
                break;
            case XPathStep::PredicateTarget::Kind::Modulo:
                result = std::fmod(left, right);
                break;
            default:
                break;
            }
        }

        values.push_back(FormatXPathNumber(result));
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::SubstringBefore) {
        if (target.arguments.size() != 2) {
            return values;
        }
        const auto text = firstValueOrEmpty(target.arguments[0]);
        const auto marker = firstValueOrEmpty(target.arguments[1]);
        const auto markerPosition = text.find(marker);
        values.push_back(markerPosition == std::string::npos ? std::string{} : text.substr(0, markerPosition));
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::SubstringAfter) {
        if (target.arguments.size() != 2) {
            return values;
        }
        const auto text = firstValueOrEmpty(target.arguments[0]);
        const auto marker = firstValueOrEmpty(target.arguments[1]);
        const auto markerPosition = text.find(marker);
        values.push_back(markerPosition == std::string::npos ? std::string{} : text.substr(markerPosition + marker.size()));
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::Substring) {
        if (target.arguments.size() != 2 && target.arguments.size() != 3) {
            return values;
        }

        const auto text = firstValueOrEmpty(target.arguments[0]);
        double startNumeric = 0.0;
        if (!TryParseXPathNumber(firstValueOrEmpty(target.arguments[1]), startNumeric)) {
            values.push_back({});
            return values;
        }

        const long long startIndex = std::llround(startNumeric) - 1;
        if (target.arguments.size() == 2) {
            const auto safeStart = static_cast<std::size_t>(std::max<long long>(0, startIndex));
            values.push_back(safeStart >= text.size() ? std::string{} : text.substr(safeStart));
            return values;
        }

        double lengthNumeric = 0.0;
        if (!TryParseXPathNumber(firstValueOrEmpty(target.arguments[2]), lengthNumeric)) {
            values.push_back({});
            return values;
        }

        const long long safeStart = std::max<long long>(0, startIndex);
        const long long safeLength = std::max<long long>(0, std::llround(lengthNumeric));
        if (static_cast<std::size_t>(safeStart) >= text.size() || safeLength == 0) {
            values.push_back({});
            return values;
        }
        values.push_back(text.substr(static_cast<std::size_t>(safeStart), static_cast<std::size_t>(safeLength)));
        return values;
    }

    if (node.NodeType() != XmlNodeType::Element) {
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::Attribute) {
        for (const auto& attribute : static_cast<const XmlElement*>(&node)->Attributes()) {
            if (MatchesXPathQualifiedName(*attribute, target.name, namespaces)) {
                values.push_back(attribute->Value());
            }
        }
        return values;
    }

    if (target.kind == XPathStep::PredicateTarget::Kind::ChildPath) {
        CollectXPathElementPathValues(node, target.path, namespaces, values);
    }

    return values;
}

bool EvaluateXPathPredicate(
    const XPathContext& context,
    const XPathStep::Predicate& predicate,
    const XmlNamespaceManager* namespaces,
    std::size_t position,
    std::size_t count) {
    if (predicate.kind == XPathStep::Predicate::Kind::And) {
        return std::all_of(predicate.operands.begin(), predicate.operands.end(), [&](const auto& operand) {
            return EvaluateXPathPredicate(context, operand, namespaces, position, count);
        });
    }

    if (predicate.kind == XPathStep::Predicate::Kind::Or) {
        return std::any_of(predicate.operands.begin(), predicate.operands.end(), [&](const auto& operand) {
            return EvaluateXPathPredicate(context, operand, namespaces, position, count);
        });
    }

    if (predicate.kind == XPathStep::Predicate::Kind::Not) {
        return predicate.operands.size() == 1
            && !EvaluateXPathPredicate(context, predicate.operands.front(), namespaces, position, count);
    }

    if (predicate.kind == XPathStep::Predicate::Kind::True) {
        return true;
    }

    if (predicate.kind == XPathStep::Predicate::Kind::False) {
        return false;
    }

    if (predicate.kind == XPathStep::Predicate::Kind::Last) {
        return count != 0 && position == count;
    }

    if (predicate.kind == XPathStep::Predicate::Kind::PositionEquals) {
        return predicate.position != 0 && position == predicate.position;
    }

    if (context.node == nullptr) {
        return false;
    }

    const auto values = ExtractXPathPredicateTargetValues(*context.node, predicate.target, namespaces, position, count);
    if (predicate.kind == XPathStep::Predicate::Kind::Exists) {
        return CoerceXPathTargetValuesToBoolean(predicate.target, values);
    }

    const auto comparisonValues = predicate.hasComparisonTarget
        ? ExtractXPathPredicateTargetValues(*context.node, predicate.comparisonTarget, namespaces, position, count)
        : std::vector<std::string>{};

    if (predicate.kind == XPathStep::Predicate::Kind::Equals) {
        if (IsXPathBooleanTargetKind(predicate.target.kind) || IsXPathBooleanTargetKind(predicate.comparisonTarget.kind)) {
            return CoerceXPathTargetValuesToBoolean(predicate.target, values)
                == CoerceXPathTargetValuesToBoolean(predicate.comparisonTarget, comparisonValues);
        }

        if (IsXPathNumericTargetKind(predicate.target) || IsXPathNumericTargetKind(predicate.comparisonTarget)) {
            return CompareXPathNumbers(values, comparisonValues, [](double left, double right) {
                return left == right;
            });
        }

        return CompareXPathStrings(values, comparisonValues, true);
    }

    if (predicate.kind == XPathStep::Predicate::Kind::NotEquals) {
        if (IsXPathBooleanTargetKind(predicate.target.kind) || IsXPathBooleanTargetKind(predicate.comparisonTarget.kind)) {
            return CoerceXPathTargetValuesToBoolean(predicate.target, values)
                != CoerceXPathTargetValuesToBoolean(predicate.comparisonTarget, comparisonValues);
        }

        if (IsXPathNumericTargetKind(predicate.target) || IsXPathNumericTargetKind(predicate.comparisonTarget)) {
            return CompareXPathNumbers(values, comparisonValues, [](double left, double right) {
                return left != right;
            });
        }

        return CompareXPathStrings(values, comparisonValues, false);
    }

    if (predicate.kind == XPathStep::Predicate::Kind::LessThan
        || predicate.kind == XPathStep::Predicate::Kind::LessThanOrEqual
        || predicate.kind == XPathStep::Predicate::Kind::GreaterThan
        || predicate.kind == XPathStep::Predicate::Kind::GreaterThanOrEqual) {
        return CompareXPathNumbers(values, comparisonValues, [&](double left, double right) {
            if (predicate.kind == XPathStep::Predicate::Kind::LessThan) {
                return left < right;
            }
            if (predicate.kind == XPathStep::Predicate::Kind::LessThanOrEqual) {
                return left <= right;
            }
            if (predicate.kind == XPathStep::Predicate::Kind::GreaterThan) {
                return left > right;
            }
            return left >= right;
        });
    }

    if (predicate.kind == XPathStep::Predicate::Kind::Contains) {
        const auto marker = CoerceXPathTargetValuesToString(predicate.comparisonTarget, comparisonValues);
        return std::any_of(values.begin(), values.end(), [&](const auto& value) {
            return value.find(marker) != std::string::npos;
        });
    }

    if (predicate.kind == XPathStep::Predicate::Kind::StartsWith) {
        const auto prefix = CoerceXPathTargetValuesToString(predicate.comparisonTarget, comparisonValues);
        return std::any_of(values.begin(), values.end(), [&](const auto& value) {
            return value.rfind(prefix, 0) == 0;
        });
    }

    if (predicate.kind == XPathStep::Predicate::Kind::EndsWith) {
        const auto suffix = CoerceXPathTargetValuesToString(predicate.comparisonTarget, comparisonValues);
        return std::any_of(values.begin(), values.end(), [&](const auto& value) {
            return value.size() >= suffix.size()
                && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
        });
    }

    return false;
}

std::string ExtractXPathPredicate(std::string_view token, std::size_t& position) {
    if (position >= token.size() || token[position] != '[') {
        ThrowInvalidXPathPredicate(std::string(token));
    }

    int depth = 0;
    const auto start = position + 1;
    while (position < token.size()) {
        if (token[position] == '[') {
            ++depth;
        } else if (token[position] == ']') {
            --depth;
            if (depth == 0) {
                const auto value = std::string(token.substr(start, position - start));
                ++position;
                return value;
            }
        }
        ++position;
    }

    ThrowInvalidXPathPredicate(std::string(token));
}

XPathStep::Predicate ParseXPathPredicate(const std::string& predicate) {
    const std::string normalized = TrimAsciiWhitespace(predicate);
    return ParseXPathPredicateExpression(normalized);
}

XPathStep::Predicate ParseXPathPredicateExpression(const std::string& predicate) {
    std::string normalized = TrimAsciiWhitespace(predicate);
    while (IsWrappedXPathExpression(normalized)) {
        normalized = TrimAsciiWhitespace(normalized.substr(1, normalized.size() - 2));
    }

    XPathStep::Predicate result;
    const auto orParts = SplitXPathExpressionTopLevel(normalized, " or ");
    if (!orParts.empty()) {
        result.kind = XPathStep::Predicate::Kind::Or;
        result.operands.reserve(orParts.size());
        for (const auto& part : orParts) {
            result.operands.push_back(ParseXPathPredicateExpression(part));
        }
        return result;
    }

    const auto andParts = SplitXPathExpressionTopLevel(normalized, " and ");
    if (!andParts.empty()) {
        result.kind = XPathStep::Predicate::Kind::And;
        result.operands.reserve(andParts.size());
        for (const auto& part : andParts) {
            result.operands.push_back(ParseXPathPredicateExpression(part));
        }
        return result;
    }

    if (const auto arguments = TryParseXPathFunctionCallArguments(normalized, "not"); arguments.has_value()) {
        if (arguments->size() != 1) {
            ThrowUnsupportedXPathFeature("predicate [" + predicate + "]");
        }

        result.kind = XPathStep::Predicate::Kind::Not;
        result.operands.push_back(ParseXPathPredicateExpression((*arguments)[0]));
        return result;
    }

    if (normalized == "last()") {
        result.kind = XPathStep::Predicate::Kind::Last;
        return result;
    }

    if (normalized == "true()") {
        result.kind = XPathStep::Predicate::Kind::True;
        return result;
    }

    if (normalized == "false()") {
        result.kind = XPathStep::Predicate::Kind::False;
        return result;
    }

    if (!normalized.empty() && std::all_of(normalized.begin(), normalized.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        })) {
        result.kind = XPathStep::Predicate::Kind::PositionEquals;
        std::size_t pos = 0;
        for (char ch : normalized) {
            pos = pos * 10 + (ch - '0');
        }
        result.position = pos;
        return result;
    }

    if (normalized.rfind("contains(", 0) == 0
        || normalized.rfind("starts-with(", 0) == 0
        || normalized.rfind("ends-with(", 0) == 0) {
        return ParseXPathFunctionPredicate(normalized, predicate);
    }

    std::size_t operatorPosition = FindXPathExpressionTopLevelOperator(normalized, "!=");
    if (operatorPosition != std::string::npos) {
        result.kind = XPathStep::Predicate::Kind::NotEquals;
        result.target = ParseXPathPredicateTarget(normalized.substr(0, operatorPosition), predicate);
        result.comparisonTarget = ParseXPathPredicateTarget(normalized.substr(operatorPosition + 2), predicate);
        result.hasComparisonTarget = true;
        return result;
    }

    operatorPosition = FindXPathExpressionTopLevelOperator(normalized, ">=");
    if (operatorPosition != std::string::npos) {
        result.kind = XPathStep::Predicate::Kind::GreaterThanOrEqual;
        result.target = ParseXPathPredicateTarget(normalized.substr(0, operatorPosition), predicate);
        result.comparisonTarget = ParseXPathPredicateTarget(normalized.substr(operatorPosition + 2), predicate);
        result.hasComparisonTarget = true;
        return result;
    }

    operatorPosition = FindXPathExpressionTopLevelOperator(normalized, "<=");
    if (operatorPosition != std::string::npos) {
        result.kind = XPathStep::Predicate::Kind::LessThanOrEqual;
        result.target = ParseXPathPredicateTarget(normalized.substr(0, operatorPosition), predicate);
        result.comparisonTarget = ParseXPathPredicateTarget(normalized.substr(operatorPosition + 2), predicate);
        result.hasComparisonTarget = true;
        return result;
    }

    operatorPosition = FindXPathExpressionTopLevelOperator(normalized, "=");
    if (operatorPosition != std::string::npos) {
        result.kind = XPathStep::Predicate::Kind::Equals;
        result.target = ParseXPathPredicateTarget(normalized.substr(0, operatorPosition), predicate);
        result.comparisonTarget = ParseXPathPredicateTarget(normalized.substr(operatorPosition + 1), predicate);
        result.hasComparisonTarget = true;
        return result;
    }

    operatorPosition = FindXPathExpressionTopLevelOperator(normalized, ">");
    if (operatorPosition != std::string::npos) {
        result.kind = XPathStep::Predicate::Kind::GreaterThan;
        result.target = ParseXPathPredicateTarget(normalized.substr(0, operatorPosition), predicate);
        result.comparisonTarget = ParseXPathPredicateTarget(normalized.substr(operatorPosition + 1), predicate);
        result.hasComparisonTarget = true;
        return result;
    }

    operatorPosition = FindXPathExpressionTopLevelOperator(normalized, "<");
    if (operatorPosition != std::string::npos) {
        result.kind = XPathStep::Predicate::Kind::LessThan;
        result.target = ParseXPathPredicateTarget(normalized.substr(0, operatorPosition), predicate);
        result.comparisonTarget = ParseXPathPredicateTarget(normalized.substr(operatorPosition + 1), predicate);
        result.hasComparisonTarget = true;
        return result;
    }

    result.kind = XPathStep::Predicate::Kind::Exists;
    result.target = ParseXPathPredicateTarget(normalized, predicate);
    return result;
}

void ApplyXPathPredicates(
    std::vector<XPathContext>& results,
    const std::vector<XPathStep::Predicate>& predicates,
    const XmlNamespaceManager* namespaces) {
    for (const auto& predicate : predicates) {
        std::vector<XPathContext> filtered;
        filtered.reserve(results.size());
        for (std::size_t index = 0; index < results.size(); ++index) {
            if (EvaluateXPathPredicate(results[index], predicate, namespaces, index + 1, results.size())) {
                filtered.push_back(results[index]);
            }
        }
        results.swap(filtered);
    }
}

std::vector<std::string> SplitXPathUnion(const std::string& xpath) {
    std::vector<std::string> parts;
    int bracketDepth = 0;
    char quote = '\0';
    std::size_t start = 0;

    for (std::size_t index = 0; index < xpath.size(); ++index) {
        const char ch = xpath[index];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }
        if (ch == '[') {
            ++bracketDepth;
            continue;
        }
        if (ch == ']') {
            --bracketDepth;
            continue;
        }
        if (ch == '(' ) {
            ++bracketDepth;
            continue;
        }
        if (ch == ')') {
            --bracketDepth;
            continue;
        }
        if (bracketDepth == 0 && ch == '|') {
            parts.push_back(TrimAsciiWhitespace(xpath.substr(start, index - start)));
            start = index + 1;
        }
    }
    parts.push_back(TrimAsciiWhitespace(xpath.substr(start)));
    return parts;
}

std::vector<XPathStep> ParseXPathSteps(const std::string& xpath, bool& absolutePath);

struct CompiledXPathBranch {
    std::string expression;
    bool absolutePath = false;
    std::vector<XPathStep> steps;
};

struct CompiledXPathExpression {
    std::vector<CompiledXPathBranch> branches;
};

CompiledXPathExpression CompileXPathExpression(const std::string& xpath) {
    const auto parts = SplitXPathUnion(xpath);
    CompiledXPathExpression compiled;
    compiled.branches.reserve(parts.size());
    for (const auto& part : parts) {
        if (part.empty()) {
            throw XmlException("XPath expression cannot be empty");
        }

        bool absolutePath = false;
        auto steps = ParseXPathSteps(part, absolutePath);
        compiled.branches.push_back(CompiledXPathBranch{part, absolutePath, std::move(steps)});
    }
    return compiled;
}

std::shared_ptr<const CompiledXPathExpression> GetCompiledXPathExpression(const std::string& xpath) {
    static std::mutex cacheMutex;
    static std::unordered_map<std::string, std::shared_ptr<const CompiledXPathExpression>> cache;
    static constexpr std::size_t kMaxCachedXPathExpressions = 256;

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        const auto found = cache.find(xpath);
        if (found != cache.end()) {
            return found->second;
        }
    }

    auto compiled = std::make_shared<const CompiledXPathExpression>(CompileXPathExpression(xpath));

    std::lock_guard<std::mutex> lock(cacheMutex);
    const auto found = cache.find(xpath);
    if (found != cache.end()) {
        return found->second;
    }
    if (cache.size() >= kMaxCachedXPathExpressions) {
        cache.clear();
    }
    cache.emplace(xpath, compiled);
    return compiled;
}

std::vector<XPathStep> ParseXPathSteps(const std::string& xpath, bool& absolutePath) {
    if (xpath.empty()) {
        throw XmlException("XPath expression cannot be empty");
    }

    std::vector<XPathStep> steps;
    std::size_t position = 0;
    absolutePath = false;

    if (xpath[position] == '/') {
        absolutePath = true;
    }

    while (position < xpath.size()) {
        XPathStep step;
        if (xpath[position] == '/') {
            if (position + 1 < xpath.size() && xpath[position + 1] == '/') {
                step.descendant = true;
                position += 2;
            } else {
                ++position;
            }
        }

        if (position >= xpath.size()) {
            break;
        }

        const auto tokenStart = position;
        int bracketDepth = 0;
        while (position < xpath.size()) {
            const char ch = xpath[position];
            if (ch == '[') {
                ++bracketDepth;
            } else if (ch == ']') {
                --bracketDepth;
            } else if (ch == '/' && bracketDepth == 0) {
                break;
            }
            ++position;
        }

        std::string token = xpath.substr(tokenStart, position - tokenStart);
        if (token.empty()) {
            continue;
        }

        if (token == ".") {
            step.self = true;
            step.axis = XPathStep::Axis::Self;
            steps.push_back(std::move(step));
            continue;
        }

        if (token == "..") {
            step.axis = XPathStep::Axis::Parent;
            step.nodeTest = true;
            steps.push_back(std::move(step));
            continue;
        }

        if (token[0] == '@') {
            step.attribute = true;
            step.axis = XPathStep::Axis::Attribute;
            step.name = ParseXPathName(token, true);
            steps.push_back(std::move(step));
            continue;
        }

        const auto predicateStart = token.find('[');
        std::string stepName = predicateStart == std::string::npos ? token : token.substr(0, predicateStart);

        // Parse explicit axis specifiers
        const auto axisDelimiter = stepName.find("::");
        if (axisDelimiter != std::string::npos) {
            const std::string axisName = stepName.substr(0, axisDelimiter);
            std::string nodeTestPart = stepName.substr(axisDelimiter + 2);

            if (axisName == "child") {
                step.axis = XPathStep::Axis::Child;
            } else if (axisName == "descendant") {
                step.axis = XPathStep::Axis::Descendant;
                step.descendant = true;
            } else if (axisName == "descendant-or-self") {
                step.axis = XPathStep::Axis::DescendantOrSelf;
                step.descendant = true;
            } else if (axisName == "parent") {
                step.axis = XPathStep::Axis::Parent;
            } else if (axisName == "ancestor") {
                step.axis = XPathStep::Axis::Ancestor;
            } else if (axisName == "ancestor-or-self") {
                step.axis = XPathStep::Axis::AncestorOrSelf;
            } else if (axisName == "following-sibling") {
                step.axis = XPathStep::Axis::FollowingSibling;
            } else if (axisName == "preceding-sibling") {
                step.axis = XPathStep::Axis::PrecedingSibling;
            } else if (axisName == "following") {
                step.axis = XPathStep::Axis::Following;
            } else if (axisName == "preceding") {
                step.axis = XPathStep::Axis::Preceding;
            } else if (axisName == "self") {
                step.axis = XPathStep::Axis::Self;
                // Don't set step.self=true; the axis + name filter handles self::name
            } else if (axisName == "attribute") {
                step.axis = XPathStep::Axis::Attribute;
                step.attribute = true;
                nodeTestPart = "@" + nodeTestPart;
            } else {
                ThrowUnsupportedXPathFeature("axis [" + axisName + "]");
            }

            stepName = nodeTestPart;
        }

        if (stepName == "node()") {
            step.nodeTest = true;
        } else if (stepName == "text()") {
            step.textNode = true;
        } else if (step.attribute || step.axis == XPathStep::Axis::Attribute) {
            step.name = ParseXPathName(stepName, true);
        } else {
            step.name = ParseXPathName(stepName, false);
        }

        if (predicateStart != std::string::npos) {
            std::size_t predicatePosition = predicateStart;
            while (predicatePosition < token.size()) {
                if (token[predicatePosition] != '[') {
                    ThrowInvalidXPathPredicate(token);
                }
                const auto predicate = ExtractXPathPredicate(token, predicatePosition);
                step.predicates.push_back(ParseXPathPredicate(predicate));
            }
        }

        steps.push_back(std::move(step));
    }

    return steps;
}

bool MatchesXPathElement(const XmlNode& node, const XPathStep& step, const XmlNamespaceManager* namespaces) {
    if (step.nodeTest) {
        return true;  // node() matches any node
    }
    if (step.textNode) {
        return IsXPathTextNode(node);
    }
    return node.NodeType() == XmlNodeType::Element && MatchesXPathQualifiedName(node, step.name, namespaces);
}

bool MatchesXPathAttribute(const XmlAttribute& attribute, const XPathStep& step, const XmlNamespaceManager* namespaces) {
    return MatchesXPathQualifiedName(attribute, step.name, namespaces);
}

void CollectDescendantElementContexts(const XmlNode& node, std::vector<XPathContext>& contexts) {
    for (const auto& child : node.ChildNodes()) {
        contexts.push_back({child.get(), child});
        CollectDescendantElementContexts(*child, contexts);
    }
}

void CollectAncestorContexts(const XmlNode& node, std::vector<XPathContext>& contexts) {
    const XmlNode* current = node.ParentNode();
    while (current != nullptr) {
        auto shared = FindSharedNode(*current);
        if (shared == nullptr && current->NodeType() == XmlNodeType::Document) {
            // Document node itself
        } else if (shared != nullptr) {
            contexts.push_back({current, shared});
        }
        current = current->ParentNode();
    }
}

void CollectFollowingSiblingContexts(const XmlNode& node, std::vector<XPathContext>& contexts) {
    const XmlNode* parent = node.ParentNode();
    if (parent == nullptr) {
        return;
    }
    const auto& siblings = parent->ChildNodes();
    bool found = false;
    for (const auto& sibling : siblings) {
        if (found) {
            contexts.push_back({sibling.get(), sibling});
        } else if (sibling.get() == &node) {
            found = true;
        }
    }
}

void CollectPrecedingSiblingContexts(const XmlNode& node, std::vector<XPathContext>& contexts) {
    const XmlNode* parent = node.ParentNode();
    if (parent == nullptr) {
        return;
    }
    const auto& siblings = parent->ChildNodes();
    std::vector<XPathContext> before;
    for (const auto& sibling : siblings) {
        if (sibling.get() == &node) {
            break;
        }
        before.push_back({sibling.get(), sibling});
    }
    // XPath preceding-sibling returns in reverse document order
    for (auto reverseIndex = before.size(); reverseIndex > 0; --reverseIndex) {
        contexts.push_back(before[reverseIndex - 1]);
    }
}

void CollectFollowingContexts(const XmlNode& node, std::vector<XPathContext>& contexts) {
    // Following axis: all nodes after the current node in document order,
    // excluding descendants.
    const XmlNode* current = &node;
    while (current != nullptr) {
        const XmlNode* parent = current->ParentNode();
        if (parent == nullptr) {
            break;
        }
        const auto& siblings = parent->ChildNodes();
        bool found = false;
        for (const auto& sibling : siblings) {
            if (found) {
                contexts.push_back({sibling.get(), sibling});
                CollectDescendantElementContexts(*sibling, contexts);
            } else if (sibling.get() == current) {
                found = true;
            }
        }
        current = parent;
    }
}

void CollectPrecedingContexts(const XmlNode& node, std::vector<XPathContext>& contexts) {
    // Preceding axis: all nodes before the current node in reverse document order,
    // excluding ancestors.
    std::unordered_set<const XmlNode*> ancestors;
    for (const XmlNode* ancestor = node.ParentNode(); ancestor != nullptr; ancestor = ancestor->ParentNode()) {
        ancestors.insert(ancestor);
    }

    // Collect all nodes in document order from the root, then filter
    const XmlNode* root = &node;
    while (root->ParentNode() != nullptr) {
        root = root->ParentNode();
    }

    std::vector<XPathContext> allNodes;
    for (const auto& child : root->ChildNodes()) {
        allNodes.push_back({child.get(), child});
        CollectDescendantElementContexts(*child, allNodes);
    }

    // Get all nodes before the current node that are not ancestors
    std::vector<XPathContext> preceding;
    for (const auto& candidate : allNodes) {
        if (candidate.node == &node) {
            break;
        }
        if (ancestors.find(candidate.node) == ancestors.end()) {
            preceding.push_back(candidate);
        }
    }

    // Reverse for preceding axis (reverse document order)
    for (auto reverseIndex = preceding.size(); reverseIndex > 0; --reverseIndex) {
        contexts.push_back(preceding[reverseIndex - 1]);
    }
}

std::vector<XPathContext> ApplyXPathStep(const std::vector<XPathContext>& current, const XPathStep& step, const XmlNamespaceManager* namespaces) {
    std::vector<XPathContext> results;

    for (const auto& context : current) {
        if (context.node == nullptr) {
            continue;
        }

        if (step.self || (step.axis == XPathStep::Axis::Self && !step.nodeTest && !step.textNode && step.name.name.empty())) {
            if (context.shared != nullptr) {
                results.push_back(context);
            }
            continue;
        }

        if (step.axis == XPathStep::Axis::Self) {
            if (context.shared != nullptr && MatchesXPathElement(*context.node, step, namespaces)) {
                results.push_back(context);
            }
            continue;
        }

        if (step.axis == XPathStep::Axis::Parent) {
            const XmlNode* parent = context.node->ParentNode();
            if (parent != nullptr) {
                auto shared = FindSharedNode(*parent);
                if (shared != nullptr && MatchesXPathElement(*parent, step, namespaces)) {
                    results.push_back({parent, shared});
                }
            }
            continue;
        }

        if (step.axis == XPathStep::Axis::Ancestor || step.axis == XPathStep::Axis::AncestorOrSelf) {
            if (step.axis == XPathStep::Axis::AncestorOrSelf && context.shared != nullptr) {
                if (MatchesXPathElement(*context.node, step, namespaces)) {
                    results.push_back(context);
                }
            }
            std::vector<XPathContext> ancestors;
            CollectAncestorContexts(*context.node, ancestors);
            for (const auto& ancestor : ancestors) {
                if (MatchesXPathElement(*ancestor.node, step, namespaces)) {
                    results.push_back(ancestor);
                }
            }
            continue;
        }

        if (step.axis == XPathStep::Axis::FollowingSibling) {
            std::vector<XPathContext> siblings;
            CollectFollowingSiblingContexts(*context.node, siblings);
            for (const auto& sibling : siblings) {
                if (MatchesXPathElement(*sibling.node, step, namespaces)) {
                    results.push_back(sibling);
                }
            }
            continue;
        }

        if (step.axis == XPathStep::Axis::PrecedingSibling) {
            std::vector<XPathContext> siblings;
            CollectPrecedingSiblingContexts(*context.node, siblings);
            for (const auto& sibling : siblings) {
                if (MatchesXPathElement(*sibling.node, step, namespaces)) {
                    results.push_back(sibling);
                }
            }
            continue;
        }

        if (step.axis == XPathStep::Axis::Following) {
            std::vector<XPathContext> following;
            CollectFollowingContexts(*context.node, following);
            for (const auto& candidate : following) {
                if (MatchesXPathElement(*candidate.node, step, namespaces)) {
                    results.push_back(candidate);
                }
            }
            continue;
        }

        if (step.axis == XPathStep::Axis::Preceding) {
            std::vector<XPathContext> preceding;
            CollectPrecedingContexts(*context.node, preceding);
            for (const auto& candidate : preceding) {
                if (MatchesXPathElement(*candidate.node, step, namespaces)) {
                    results.push_back(candidate);
                }
            }
            continue;
        }

        if (step.attribute) {
            std::vector<XPathContext> attributeCandidates;
            if (step.descendant) {
                std::vector<XPathContext> descendantElements;
                CollectDescendantElementContexts(*context.node, descendantElements);
                for (const auto& candidate : descendantElements) {
                    if (candidate.node->NodeType() != XmlNodeType::Element) {
                        continue;
                    }
                    for (const auto& attribute : static_cast<const XmlElement*>(candidate.node)->Attributes()) {
                        attributeCandidates.push_back({attribute.get(), attribute});
                    }
                }
            } else if (context.node->NodeType() == XmlNodeType::Element) {
                for (const auto& attribute : static_cast<const XmlElement*>(context.node)->Attributes()) {
                    attributeCandidates.push_back({attribute.get(), attribute});
                }
            }

            for (const auto& candidate : attributeCandidates) {
                if (MatchesXPathAttribute(*static_cast<const XmlAttribute*>(candidate.node), step, namespaces)) {
                    results.push_back(candidate);
                }
            }
            continue;
        }

        std::vector<XPathContext> candidates;
        if (step.axis == XPathStep::Axis::DescendantOrSelf
            && context.shared != nullptr
            && MatchesXPathElement(*context.node, step, namespaces)) {
            results.push_back(context);
        }

        if (step.descendant) {
            CollectDescendantElementContexts(*context.node, candidates);
        } else {
            for (const auto& child : context.node->ChildNodes()) {
                candidates.push_back({child.get(), child});
            }
        }

        for (const auto& candidate : candidates) {
            if (MatchesXPathElement(*candidate.node, step, namespaces)) {
                results.push_back(candidate);
            }
        }
    }

    // Deduplicate results (XPath node-sets do not contain duplicates)
    if (step.axis == XPathStep::Axis::Parent
        || step.axis == XPathStep::Axis::Ancestor
        || step.axis == XPathStep::Axis::AncestorOrSelf
        || step.axis == XPathStep::Axis::FollowingSibling
        || step.axis == XPathStep::Axis::PrecedingSibling
        || step.axis == XPathStep::Axis::Following
        || step.axis == XPathStep::Axis::Preceding) {
        std::unordered_set<const XmlNode*> seen;
        std::vector<XPathContext> unique;
        unique.reserve(results.size());
        for (auto& context : results) {
            if (seen.insert(context.node).second) {
                unique.push_back(std::move(context));
            }
        }
        results = std::move(unique);
    }

    ApplyXPathPredicates(results, step.predicates, namespaces);

    return results;
}

std::vector<XPathContext> FilterXPathStepMatches(const std::vector<XPathContext>& current, const XPathStep& step, const XmlNamespaceManager* namespaces) {
    std::vector<XPathContext> results;
    for (const auto& context : current) {
        if (context.node == nullptr) {
            continue;
        }

        if (step.self) {
            results.push_back(context);
            continue;
        }

        if (step.attribute) {
            if (context.node->NodeType() != XmlNodeType::Element) {
                continue;
            }
            for (const auto& attribute : static_cast<const XmlElement*>(context.node)->Attributes()) {
                if (MatchesXPathAttribute(*attribute, step, namespaces)) {
                    results.push_back({attribute.get(), attribute});
                }
            }
            continue;
        }

        if (MatchesXPathElement(*context.node, step, namespaces)) {
            results.push_back(context);
        }
    }

    ApplyXPathPredicates(results, step.predicates, namespaces);

    return results;
}

XmlNodeList EvaluateCompiledXPathSingleFromDocument(
    const XmlDocument& document,
    const CompiledXPathBranch& compiled,
    const XmlNamespaceManager* namespaces) {
    if (compiled.steps.empty()) {
        return {};
    }

    std::vector<XPathContext> current;
    std::size_t stepIndex = 0;

    if (!compiled.absolutePath) {
        for (const auto& child : document.ChildNodes()) {
            current.push_back({child.get(), child});
        }
    } else {
        if (compiled.steps.front().descendant || compiled.steps.front().axis == XPathStep::Axis::Descendant
            || compiled.steps.front().axis == XPathStep::Axis::DescendantOrSelf) {
            current.push_back({&document, {}});
            current = ApplyXPathStep(current, compiled.steps.front(), namespaces);
        } else {
            const auto root = document.DocumentElement();
            if (root == nullptr) {
                return {};
            }
            current.push_back({root.get(), root});
            current = FilterXPathStepMatches(current, compiled.steps.front(), namespaces);
        }
        stepIndex = 1;
    }

    for (; stepIndex < compiled.steps.size(); ++stepIndex) {
        current = ApplyXPathStep(current, compiled.steps[stepIndex], namespaces);
        if (current.empty()) {
            break;
        }
    }

    std::vector<std::shared_ptr<XmlNode>> nodes;
    nodes.reserve(current.size());
    for (const auto& context : current) {
        if (context.shared != nullptr) {
            nodes.push_back(context.shared);
        }
    }
    return XmlNodeList(std::move(nodes));
}

XmlNodeList EvaluateXPathSingleFromDocument(const XmlDocument& document, const std::string& xpath, const XmlNamespaceManager* namespaces) {
    const auto compiled = GetCompiledXPathExpression(xpath);
    return EvaluateCompiledXPathSingleFromDocument(document, compiled->branches.front(), namespaces);
}

XmlNodeList EvaluateXPathFromDocument(const XmlDocument& document, const std::string& xpath, const XmlNamespaceManager* namespaces = nullptr) {
    const auto compiled = GetCompiledXPathExpression(xpath);
    if (compiled->branches.size() == 1) {
        return EvaluateCompiledXPathSingleFromDocument(document, compiled->branches.front(), namespaces);
    }

    std::vector<std::shared_ptr<XmlNode>> merged;
    std::unordered_set<const XmlNode*> seen;
    for (const auto& branch : compiled->branches) {
        const auto partResult = EvaluateCompiledXPathSingleFromDocument(document, branch, namespaces);
        for (std::size_t i = 0; i < partResult.Count(); ++i) {
            const auto& node = partResult.Item(i);
            if (seen.insert(node.get()).second) {
                merged.push_back(node);
            }
        }
    }
    return XmlNodeList(std::move(merged));
}

XmlNodeList EvaluateXPathSingleFromElement(const XmlElement& element, const std::string& xpath, const XmlNamespaceManager* namespaces);

XmlNodeList EvaluateCompiledXPathSingleFromElement(
    const XmlElement& element,
    const CompiledXPathBranch& compiled,
    const XmlNamespaceManager* namespaces) {
    if (compiled.steps.empty()) {
        return {};
    }

    std::vector<XPathContext> current;
    std::size_t stepIndex = 0;

    if (compiled.absolutePath) {
        if (element.OwnerDocument() != nullptr) {
            return EvaluateCompiledXPathSingleFromDocument(*element.OwnerDocument(), compiled, namespaces);
        }

        const auto* topElement = FindTopElement(element);
        auto sharedTop = FindSharedNode(*topElement);
        if (sharedTop == nullptr) {
            return {};
        }
        current.push_back({topElement, sharedTop});
        if (compiled.steps.front().descendant) {
            current = ApplyXPathStep(current, compiled.steps.front(), namespaces);
        } else {
            current = FilterXPathStepMatches(current, compiled.steps.front(), namespaces);
        }
        stepIndex = 1;
    } else {
        current.push_back({&element, FindSharedNode(element)});
    }

    for (; stepIndex < compiled.steps.size(); ++stepIndex) {
        current = ApplyXPathStep(current, compiled.steps[stepIndex], namespaces);
        if (current.empty()) {
            break;
        }
    }

    std::vector<std::shared_ptr<XmlNode>> nodes;
    nodes.reserve(current.size());
    for (const auto& context : current) {
        if (context.shared != nullptr) {
            nodes.push_back(context.shared);
        }
    }
    return XmlNodeList(std::move(nodes));
}

XmlNodeList EvaluateXPathSingleFromElement(const XmlElement& element, const std::string& xpath, const XmlNamespaceManager* namespaces) {
    const auto compiled = GetCompiledXPathExpression(xpath);
    return EvaluateCompiledXPathSingleFromElement(element, compiled->branches.front(), namespaces);
}

XmlNodeList EvaluateXPathFromElement(const XmlElement& element, const std::string& xpath, const XmlNamespaceManager* namespaces = nullptr) {
    const auto compiled = GetCompiledXPathExpression(xpath);
    if (compiled->branches.size() == 1) {
        return EvaluateCompiledXPathSingleFromElement(element, compiled->branches.front(), namespaces);
    }

    std::vector<std::shared_ptr<XmlNode>> merged;
    std::unordered_set<const XmlNode*> seen;
    for (const auto& branch : compiled->branches) {
        const auto partResult = EvaluateCompiledXPathSingleFromElement(element, branch, namespaces);
        for (std::size_t i = 0; i < partResult.Count(); ++i) {
            const auto& node = partResult.Item(i);
            if (seen.insert(node.get()).second) {
                merged.push_back(node);
            }
        }
    }
    return XmlNodeList(std::move(merged));
}

std::size_t FindChildIndexOrThrow(const XmlNode& parent, const std::shared_ptr<XmlNode>& child) {
    const auto& children = parent.ChildNodes();
    const auto found = std::find(children.begin(), children.end(), child);
    if (found == children.end()) {
        throw XmlException("Reference child is not a child of the current node");
    }

    return static_cast<std::size_t>(std::distance(children.begin(), found));
}

bool SubsetStartsWithAt(std::string_view text, std::size_t position, std::string_view token) {
    return text.substr(position, token.size()) == token;
}

std::string_view ParseNameAtView(std::string_view text, std::size_t& position) {
    if (position >= text.size() || !IsNameStartChar(text[position])) {
        return {};
    }

    const auto start = position++;
    while (position < text.size() && IsNameChar(text[position])) {
        ++position;
    }

    return std::string_view(text.data() + start, position - start);
}

std::string ParseNameAt(std::string_view text, std::size_t& position) {
    const std::string_view name = ParseNameAtView(text, position);
    return std::string(name);
}

void SkipXmlWhitespaceAt(std::string_view text, std::size_t& position) noexcept {
    while (position < text.size() && IsWhitespace(text[position])) {
        ++position;
    }
}

bool SkipTagAt(std::string_view text, std::size_t& position, bool& isEmptyElement) {
    if (position >= text.size() || text[position] != '<') {
        return false;
    }

    ++position;
    if (ParseNameAtView(text, position).empty()) {
        return false;
    }

    while (position < text.size()) {
        const char ch = text[position];
        if (ch == '\"' || ch == '\'') {
            const char quote = ch;
            ++position;
            while (position < text.size() && text[position] != quote) {
                ++position;
            }
            if (position == text.size()) {
                return false;
            }
            ++position;
            continue;
        }

        if (SubsetStartsWithAt(text, position, "/>")) {
            position += 2;
            isEmptyElement = true;
            return true;
        }

        if (ch == '>') {
            ++position;
            isEmptyElement = false;
            return true;
        }

        ++position;
    }

    return false;
}

std::pair<std::size_t, std::size_t> ComputeLineColumnAt(std::string_view text, std::size_t position) noexcept {
    std::size_t line = 1;
    std::size_t column = 1;

    for (std::size_t index = 0; index < position && index < text.size(); ++index) {
        if (text[index] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }

    return {line, column};
}

std::pair<std::size_t, std::size_t> FindElementClose(std::string_view text, std::size_t position) {
    int depth = 1;

    while (position < text.size()) {
        if (text[position] != '<') {
            ++position;
            continue;
        }

        if (SubsetStartsWithAt(text, position, "<!--")) {
            const auto end = text.find("-->", position + 4);
            if (end == std::string_view::npos) {
                return {std::string_view::npos, std::string_view::npos};
            }
            position = end + 3;
            continue;
        }

        if (SubsetStartsWithAt(text, position, "<![CDATA[")) {
            const auto end = text.find("]]>", position + 9);
            if (end == std::string_view::npos) {
                return {std::string_view::npos, std::string_view::npos};
            }
            position = end + 3;
            continue;
        }

        if (SubsetStartsWithAt(text, position, "<?")) {
            const auto end = text.find("?>", position + 2);
            if (end == std::string_view::npos) {
                return {std::string_view::npos, std::string_view::npos};
            }
            position = end + 2;
            continue;
        }

        if (SubsetStartsWithAt(text, position, "</")) {
            const auto closeStart = position;
            position += 2;
            if (ParseNameAtView(text, position).empty()) {
                return {std::string_view::npos, std::string_view::npos};
            }
            while (position < text.size() && text[position] != '>') {
                ++position;
            }
            if (position == text.size()) {
                return {std::string_view::npos, std::string_view::npos};
            }
            ++position;
            --depth;
            if (depth == 0) {
                return {closeStart, position};
            }
            continue;
        }

        if (SubsetStartsWithAt(text, position, "<!")) {
            const auto end = text.find('>', position + 2);
            if (end == std::string_view::npos) {
                return {std::string_view::npos, std::string_view::npos};
            }
            position = end + 1;
            continue;
        }

        bool isEmptyElement = false;
        if (!SkipTagAt(text, position, isEmptyElement)) {
            return {std::string_view::npos, std::string_view::npos};
        }
        if (!isEmptyElement) {
            ++depth;
        }
    }

    return {std::string_view::npos, std::string_view::npos};
}

std::string EscapeText(const std::string& value, const XmlWriterSettings& settings) {
    std::string source;
    if (settings.NewLineHandling == XmlNewLineHandling::Replace) {
        source = NormalizeNewLines(value, settings.NewLineChars);
    } else if (settings.NewLineHandling == XmlNewLineHandling::Entitize) {
        source = value; // newlines are entitized per-character below
    } else {
        source = value;
    }
    std::string escaped;
    escaped.reserve(source.size());

    for (std::size_t i = 0; i < source.size(); ++i) {
        const char ch = source[i];
        switch (ch) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '\r':
            if (settings.NewLineHandling == XmlNewLineHandling::Entitize) {
                escaped += "&#xD;";
            } else {
                escaped.push_back(ch);
            }
            break;
        case '\n':
            if (settings.NewLineHandling == XmlNewLineHandling::Entitize) {
                escaped += "&#xA;";
            } else {
                escaped.push_back(ch);
            }
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }

    return escaped;
}

std::string EscapeAttribute(const std::string& value, const XmlWriterSettings& settings) {
    std::string source;
    if (settings.NewLineHandling == XmlNewLineHandling::Replace) {
        source = NormalizeNewLines(value, settings.NewLineChars);
    } else {
        source = value;
    }
    std::string escaped;
    escaped.reserve(source.size());

    for (const char ch : source) {
        switch (ch) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '\"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "&apos;";
            break;
        case '\r':
            if (settings.NewLineHandling == XmlNewLineHandling::Entitize) {
                escaped += "&#xD;";
            } else {
                escaped.push_back(ch);
            }
            break;
        case '\n':
            if (settings.NewLineHandling == XmlNewLineHandling::Entitize) {
                escaped += "&#xA;";
            } else {
                escaped.push_back(ch);
            }
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }

    return escaped;
}

class XmlParser final {
public:
    enum class MarkupKind {
        None,
        XmlDeclaration,
        Comment,
        CData,
        ProcessingInstruction,
        EndTag,
        DocumentType,
        UnsupportedDeclaration,
        Element,
    };

    explicit XmlParser(std::string_view input) : input_(input), position_(0) {
        if (input_.size() >= 3 && input_[0] == '\xEF' && input_[1] == '\xBB' && input_[2] == '\xBF') {
            position_ = 3;
        }
    }

    void ParseInto(XmlDocument& document) {
        document_ = &document;
        document.RemoveAll();

        auto appendParsedNode = [this, &document](const std::shared_ptr<XmlNode>& node, std::size_t errorPosition) {
            try {
                document.AppendChild(node);
            } catch (const XmlException& exception) {
                if (exception.Line() != 0 || exception.Column() != 0) {
                    throw;
                }

                const auto [line, column] = ComputeLineColumn(errorPosition);
                throw XmlException(exception.what(), line, column);
            }
        };

        SkipWhitespace();
        if (ClassifyMarkup() == XmlMarkupKind::XmlDeclaration) {
            const auto nodeStart = position_;
            appendParsedNode(ParseDeclaration(document), nodeStart);
        }

        SkipWhitespace();
        while (true) {
            bool consumedMarkup = true;
            switch (ClassifyMarkup()) {
            case XmlMarkupKind::Comment: {
                const auto nodeStart = position_;
                appendParsedNode(ParseComment(document), nodeStart);
                break;
            }
            case XmlMarkupKind::ProcessingInstruction: {
                const auto nodeStart = position_;
                appendParsedNode(ParseProcessingInstruction(document), nodeStart);
                break;
            }
            case XmlMarkupKind::DocumentType: {
                const auto nodeStart = position_;
                appendParsedNode(ParseDocumentType(document), nodeStart);
                break;
            }
            default:
                consumedMarkup = false;
                break;
            }

            if (!consumedMarkup) {
                break;
            }

            SkipWhitespace();
        }

        if (Peek() != '<') {
            Throw("Document root element is missing");
        }

        if (ClassifyMarkup() == XmlMarkupKind::EndTag) {
            Consume("</");
            const std::string closeName = ParseName();
            Throw("Unexpected closing tag: </" + closeName + ">");
        }

        if (ClassifyMarkup() == XmlMarkupKind::UnsupportedDeclaration) {
            Throw("Unsupported markup declaration");
        }

        const auto rootStart = position_;
        appendParsedNode(ParseElement(document), rootStart);
        SkipWhitespace();

        while (!IsEnd()) {
            switch (ClassifyMarkup()) {
            case XmlMarkupKind::Comment: {
                const auto nodeStart = position_;
                appendParsedNode(ParseComment(document), nodeStart);
                break;
            }
            case XmlMarkupKind::ProcessingInstruction: {
                const auto nodeStart = position_;
                appendParsedNode(ParseProcessingInstruction(document), nodeStart);
                break;
            }
            case XmlMarkupKind::DocumentType:
                Throw("DOCTYPE must appear before the root element");
                break;
            default:
                if (IsWhitespace(Peek())) {
                    SkipWhitespace();
                } else {
                    Throw("Unexpected content after the root element");
                }
                break;
            }
        }
    }

private:
    XmlMarkupKind ClassifyMarkup() const noexcept {
        return ClassifyXmlMarkupWithCharAt(
            position_,
            [this](std::size_t position) noexcept {
                return position < input_.size() ? input_[position] : '\0';
            });
    }

    bool IsEnd() const noexcept {
        return position_ >= input_.size();
    }

    char Peek() const noexcept {
        return IsEnd() ? '\0' : input_[position_];
    }

    char Read() {
        if (IsEnd()) {
            Throw("Unexpected end of XML document");
        }

        return input_[position_++];
    }

    bool StartsWith(std::string_view token) const noexcept {
        return input_.substr(position_, token.size()) == token;
    }

    void Consume(std::string_view token) {
        if (!StartsWith(token)) {
            Throw("Expected '" + std::string(token) + "'");
        }

        position_ += token.size();
    }

    void SkipWhitespace() {
        SkipXmlWhitespaceAt(input_, position_);
    }

    void Expect(char ch) {
        if (Read() != ch) {
            Throw(std::string("Expected '") + ch + "'");
        }
    }

    [[noreturn]] void Throw(const std::string& message) const {
        auto [line, column] = ComputeLineColumn(position_);
        throw XmlException(message, line, column);
    }

    std::pair<std::size_t, std::size_t> ComputeLineColumn(std::size_t position) const noexcept {
        std::size_t line = 1;
        std::size_t column = 1;

        for (std::size_t index = 0; index < position && index < input_.size(); ++index) {
            if (input_[index] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }

        return {line, column};
    }

    std::string ParseName() {
        const auto start = position_;
        const std::string name = ParseNameAt(input_, position_);
        if (name.empty()) {
            position_ = start;
            Throw("Invalid XML name");
        }

        return name;
    }

    std::string DecodeEntities(std::string_view value) const {
        return DecodeEntityText(
            value,
            [this](const std::string& entity) {
                return LookupDocumentInternalEntityDeclaration(document_, entity);
            },
            [this](const std::string& message) {
                auto [line, column] = ComputeLineColumn(position_);
                throw XmlException(message, line, column);
            });
    }

    std::string ParseQuotedValue(bool decodeEntities = true) {
        const char quote = Read();
        if (quote != '\"' && quote != '\'') {
            Throw("Expected quoted value");
        }

        const auto start = position_;
        const auto quoteEnd = ScanQuotedValueEndAt(start - 1, [this](std::size_t position) noexcept {
            return position < input_.size() ? input_[position] : '\0';
        });
        if (quoteEnd == std::string::npos) {
            Throw("Unterminated quoted value");
        }

        const auto raw = input_.substr(start, quoteEnd - start);
        position_ = quoteEnd + 1;
        return decodeEntities ? DecodeEntities(raw) : std::string(raw);
    }

    std::shared_ptr<XmlDeclaration> ParseDeclaration(XmlDocument& document) {
        Consume("<?xml");

        std::string version = "1.0";
        std::string encoding;
        std::string standalone;

        while (true) {
            SkipWhitespace();
            if (StartsWith("?>")) {
                Consume("?>");
                break;
            }

            const std::string name = ParseName();
            SkipWhitespace();
            if (Read() != '=') {
                Throw("Expected '=' in XML declaration");
            }
            SkipWhitespace();
            const std::string value = ParseQuotedValue();

            if (name == "version") {
                version = value;
            } else if (name == "encoding") {
                encoding = value;
            } else if (name == "standalone") {
                standalone = value;
            } else {
                Throw("Unsupported XML declaration attribute: " + name);
            }
        }

        return document.CreateXmlDeclaration(version, encoding, standalone);
    }

    std::shared_ptr<XmlComment> ParseComment(XmlDocument& document) {
        Consume("<!--");
        const auto end = ScanDelimitedSectionEndAt(position_, "-->", [this](std::string_view terminator, std::size_t position) noexcept {
            return input_.find(terminator, position);
        });
        if (end == std::string_view::npos) {
            Throw("Unterminated comment");
        }

        const auto text = std::string(input_.substr(position_, end - position_));
        position_ = end + 3;
        return document.CreateComment(text);
    }

    std::shared_ptr<XmlCDataSection> ParseCData(XmlDocument& document) {
        Consume("<![CDATA[");
        const auto end = ScanDelimitedSectionEndAt(position_, "]]>", [this](std::string_view terminator, std::size_t position) noexcept {
            return input_.find(terminator, position);
        });
        if (end == std::string_view::npos) {
            Throw("Unterminated CDATA section");
        }

        const auto value = std::string(input_.substr(position_, end - position_));
        position_ = end + 3;
        return document.CreateCDataSection(value);
    }

    std::shared_ptr<XmlProcessingInstruction> ParseProcessingInstruction(XmlDocument& document) {
        Consume("<?");
        const std::string target = ParseName();
        if (target == "xml") {
            Throw("XML declaration is only allowed at the beginning of the document");
        }

        std::string data;
        if (!StartsWith("?>")) {
            const auto end = ScanDelimitedSectionEndAt(position_, "?>", [this](std::string_view terminator, std::size_t position) noexcept {
                return input_.find(terminator, position);
            });
            if (end == std::string_view::npos) {
                Throw("Unterminated processing instruction");
            }

            data = Trim(std::string(input_.substr(position_, end - position_)));
            position_ = end;
        }

        Consume("?>");
        return document.CreateProcessingInstruction(target, data);
    }

    std::shared_ptr<XmlDocumentType> ParseDocumentType(XmlDocument& document) {
        Consume("<!DOCTYPE");
        SkipWhitespace();

        const std::string name = ParseName();
        SkipWhitespace();

        std::string publicId;
        std::string systemId;
        std::string internalSubset;

        if (StartsWith("PUBLIC")) {
            Consume("PUBLIC");
            SkipWhitespace();
            publicId = ParseQuotedValue(false);
            SkipWhitespace();
            systemId = ParseQuotedValue(false);
            SkipWhitespace();
        } else if (StartsWith("SYSTEM")) {
            Consume("SYSTEM");
            SkipWhitespace();
            systemId = ParseQuotedValue(false);
            SkipWhitespace();
        }

        if (Peek() == '[') {
            Read();
            const auto subsetStart = position_;
            int bracketDepth = 1;
            bool inQuote = false;
            char quote = '\0';

            while (!IsEnd() && bracketDepth > 0) {
                const char ch = Read();
                if (inQuote) {
                    if (ch == quote) {
                        inQuote = false;
                    }
                    continue;
                }

                if (ch == '\"' || ch == '\'') {
                    inQuote = true;
                    quote = ch;
                } else if (ch == '[') {
                    ++bracketDepth;
                } else if (ch == ']') {
                    --bracketDepth;
                }
            }

            if (bracketDepth != 0) {
                Throw("Unterminated DOCTYPE internal subset");
            }

            internalSubset = Trim(std::string(input_.substr(subsetStart, position_ - subsetStart - 1)));
            SkipWhitespace();
        }

        if (Read() != '>') {
            Throw("Expected '>' after DOCTYPE");
        }
        return document.CreateDocumentType(name, publicId, systemId, internalSubset);
    }

    void AppendParsedCharacterData(XmlDocument& document, XmlElement& element) {
        std::string textBuffer;
        std::size_t rawSegmentStart = std::string::npos;
        bool textBufferMaterialized = false;
        bool sawEntityReference = false;
        auto flushText = [&](std::size_t segmentEnd, bool preserveWhitespaceOnly = false) {
            if (rawSegmentStart == std::string::npos) {
                return;
            }

            if (!textBufferMaterialized) {
                textBuffer.assign(input_.substr(rawSegmentStart, segmentEnd - rawSegmentStart));
                textBufferMaterialized = true;
            }

            AppendParsedCharacterDataNode(document, element, textBuffer, preserveWhitespaceOnly);
            textBuffer.clear();
            rawSegmentStart = std::string::npos;
            textBufferMaterialized = false;
        };

        while (!IsEnd() && Peek() != '<') {
            if (Peek() != '&') {
                if (rawSegmentStart == std::string::npos) {
                    rawSegmentStart = position_;
                }
                if (textBufferMaterialized) {
                    textBuffer.push_back(Read());
                } else {
                    ++position_;
                }
                continue;
            }

            const auto semicolon = input_.find(';', position_);
            if (semicolon == std::string_view::npos) {
                Throw("Unterminated entity reference");
            }

            const auto entityStart = position_;
            const std::string entity(input_.substr(position_ + 1, semicolon - position_ - 1));
            position_ = semicolon + 1;

            const auto predefined = ResolvePredefinedEntityReferenceValueView(entity);
            if (!predefined.empty()) {
                if (rawSegmentStart == std::string::npos) {
                    rawSegmentStart = entityStart;
                }
                if (!textBufferMaterialized) {
                    textBuffer.assign(input_.substr(rawSegmentStart, entityStart - rawSegmentStart));
                    textBufferMaterialized = true;
                }
                textBuffer.append(predefined.data(), predefined.size());
                continue;
            }

            if (!entity.empty() && entity.front() == '#') {
                if (rawSegmentStart == std::string::npos) {
                    rawSegmentStart = entityStart;
                }
                if (!textBufferMaterialized) {
                    textBuffer.assign(input_.substr(rawSegmentStart, entityStart - rawSegmentStart));
                    textBufferMaterialized = true;
                }
                unsigned int codePoint = 0;
                if (!TryParseNumericEntityReferenceCodePoint(entity, codePoint)) {
                    Throw("Invalid numeric entity reference: &" + entity + ';');
                }
                AppendCodePointUtf8(textBuffer, codePoint);
                continue;
            }

            if (!HasDocumentEntityDeclaration(document_, entity)) {
                Throw("Unknown entity reference: &" + entity + ';');
            }

            flushText(entityStart, true);
            sawEntityReference = true;
            element.AppendChild(document.CreateEntityReference(entity));
        }

        flushText(position_, sawEntityReference);
    }

    std::shared_ptr<XmlElement> ParseElement(XmlDocument& document) {
        Expect('<');
        const std::string name = ParseName();
        auto element = document.CreateElement(name);
        const std::string_view text = input_;

        while (true) {
            SkipWhitespace();
            bool isEmptyElement = false;
            if (ConsumeXmlStartTagCloseAt(
                    position_,
                    isEmptyElement,
                    [this](std::size_t position) noexcept {
                        return position < input_.size() ? input_[position] : '\0';
                    },
                    [&text](std::size_t position, std::string_view token) noexcept {
                        return SubsetStartsWithAt(text, position, token);
                    })) {
                if (isEmptyElement) {
                    return element;
                }
                break;
            }

            std::size_t cursor = position_;
            const auto attributeToken = ParseXmlAttributeAssignmentAt(
                cursor,
                [&text](std::size_t& position) {
                    return ParseNameAt(text, position);
                },
                [&text](std::size_t& position) noexcept {
                    SkipXmlWhitespaceAt(text, position);
                },
                [this](std::size_t position) noexcept {
                    return position < input_.size() ? input_[position] : '\0';
                },
                [this](std::size_t position) noexcept {
                    return ScanQuotedValueEndAt(position, [this](std::size_t probe) noexcept {
                        return probe < input_.size() ? input_[probe] : '\0';
                    });
                });

            if (attributeToken.name.empty()) {
                Throw("Invalid XML name");
            }
            if (!attributeToken.valid) {
                position_ = cursor;
                if (!attributeToken.sawEquals) {
                    if (!IsEnd()) {
                        ++position_;
                    }
                    Throw("Expected '=' after attribute name");
                }
                if (!IsEnd()) {
                    ++position_;
                }
                Throw("Expected quoted value");
            }

            position_ = cursor;
            element->SetAttribute(
                attributeToken.name,
                DecodeEntities(input_.substr(attributeToken.rawValueStart, attributeToken.rawValueEnd - attributeToken.rawValueStart)));
        }

        while (true) {
            switch (ClassifyMarkup()) {
            case XmlMarkupKind::EndTag: {
                Consume("</");
                const std::string closeName = ParseName();
                if (closeName != name) {
                    Throw("Mismatched closing tag. Expected </" + name + "> but found </" + closeName + ">");
                }

                std::size_t cursor = position_;
                if (!ConsumeXmlEndTagCloseAt(
                        cursor,
                        [&text](std::size_t& position) noexcept {
                            SkipXmlWhitespaceAt(text, position);
                        },
                        [this](std::size_t position) noexcept {
                            return position < input_.size() ? input_[position] : '\0';
                        })) {
                    position_ = cursor;
                    if (!IsEnd()) {
                        ++position_;
                    }
                    Throw("Expected '>' after closing tag");
                }
                position_ = cursor;
                return element;
            }
            case XmlMarkupKind::Comment:
                element->AppendChild(ParseComment(document));
                break;
            case XmlMarkupKind::CData:
                element->AppendChild(ParseCData(document));
                break;
            case XmlMarkupKind::ProcessingInstruction:
                element->AppendChild(ParseProcessingInstruction(document));
                break;
            case XmlMarkupKind::DocumentType:
                Throw("DOCTYPE is not allowed inside an element");
                break;
            case XmlMarkupKind::UnsupportedDeclaration:
                Throw("Unsupported markup declaration");
                break;
            case XmlMarkupKind::Element:
                element->AppendChild(ParseElement(document));
                break;
            case XmlMarkupKind::None:
                if (IsEnd()) {
                    Throw("Unexpected end of input inside element <" + name + ">");
                }
                AppendParsedCharacterData(document, *element);
                break;
            case XmlMarkupKind::XmlDeclaration:
                position_ += 5;
                Throw("XML declaration is only allowed at the beginning of the document");
                break;
            }
        }
    }

    std::string_view input_;
    std::size_t position_;
    XmlDocument* document_ = nullptr;
};

void SkipSubsetWhitespace(std::string_view text, std::size_t& position) {
    SkipXmlWhitespaceAt(text, position);
}

bool StartsWithAt(std::string_view text, std::size_t position, std::string_view token) {
    return position + token.size() <= text.size() && text.substr(position, token.size()) == token;
}

std::string ReadSubsetName(std::string_view text, std::size_t& position) {
    return ParseNameAt(text, position);
}

std::string ReadSubsetQuotedValue(std::string_view text, std::size_t& position) {
    if (position >= text.size() || (text[position] != '\'' && text[position] != '"')) {
        return {};
    }

    const auto start = position + 1;
    const auto quoteEnd = ScanQuotedValueEndAt(position, [&text](std::size_t probe) noexcept {
        return probe < text.size() ? text[probe] : '\0';
    });

    if (quoteEnd == std::string::npos) {
        position = text.size();
        return std::string(text.substr(start));
    }

    position = quoteEnd + 1;

    return std::string(text.substr(start, quoteEnd - start));
}

ParsedXmlDeclarationData ParseXmlDeclarationValue(std::string_view value) {
    ParsedXmlDeclarationData declaration;
    std::size_t position = 0;

    while (position < value.size()) {
        SkipXmlWhitespaceAt(value, position);
        if (position >= value.size()) {
            break;
        }

        const auto token = ParseXmlAttributeAssignmentAt(
            position,
            [&value](std::size_t& probe) {
                return ParseNameAt(value, probe);
            },
            [&value](std::size_t& probe) noexcept {
                SkipXmlWhitespaceAt(value, probe);
            },
            [&value](std::size_t probe) noexcept {
                return probe < value.size() ? value[probe] : '\0';
            },
            [&value](std::size_t probe) noexcept {
                return ScanQuotedValueEndAt(probe, [&value](std::size_t innerProbe) noexcept {
                    return innerProbe < value.size() ? value[innerProbe] : '\0';
                });
            });

        if (token.name.empty() || !token.valid) {
            throw XmlException("Malformed XML declaration");
        }

        if (token.name == "version") {
            declaration.version = std::string(value.substr(token.rawValueStart, token.rawValueEnd - token.rawValueStart));
        } else if (token.name == "encoding") {
            declaration.encoding = std::string(value.substr(token.rawValueStart, token.rawValueEnd - token.rawValueStart));
        } else if (token.name == "standalone") {
            declaration.standalone = std::string(value.substr(token.rawValueStart, token.rawValueEnd - token.rawValueStart));
        } else {
            throw XmlException("Malformed XML declaration");
        }
    }

    return declaration;
}

ParsedDocumentTypeDeclaration ParseDocumentTypeDeclaration(std::string_view outerXml) {
    std::size_t position = 0;
    if (!StartsWithAt(outerXml, position, "<!DOCTYPE")) {
        throw XmlException("Malformed DOCTYPE declaration");
    }

    position += 9;
    SkipSubsetWhitespace(outerXml, position);

    ParsedDocumentTypeDeclaration declaration;
    declaration.name = ParseNameAt(outerXml, position);
    if (declaration.name.empty()) {
        throw XmlException("Malformed DOCTYPE declaration");
    }

    SkipSubsetWhitespace(outerXml, position);
    if (StartsWithAt(outerXml, position, "PUBLIC")) {
        position += 6;
        SkipSubsetWhitespace(outerXml, position);
        declaration.publicId = ReadSubsetQuotedValue(outerXml, position);
        if (declaration.publicId.empty()) {
            throw XmlException("Malformed DOCTYPE declaration");
        }
        SkipSubsetWhitespace(outerXml, position);
        declaration.systemId = ReadSubsetQuotedValue(outerXml, position);
        if (declaration.systemId.empty()) {
            throw XmlException("Malformed DOCTYPE declaration");
        }
        SkipSubsetWhitespace(outerXml, position);
    } else if (StartsWithAt(outerXml, position, "SYSTEM")) {
        position += 6;
        SkipSubsetWhitespace(outerXml, position);
        declaration.systemId = ReadSubsetQuotedValue(outerXml, position);
        if (declaration.systemId.empty()) {
            throw XmlException("Malformed DOCTYPE declaration");
        }
        SkipSubsetWhitespace(outerXml, position);
    }

    if (position < outerXml.size() && outerXml[position] == '[') {
        ++position;
        const auto subsetEnd = ScanDocumentTypeInternalSubsetEnd(outerXml, position);
        if (subsetEnd == std::string::npos) {
            throw XmlException("Unterminated DOCTYPE internal subset");
        }
        declaration.internalSubset = Trim(std::string(outerXml.substr(position, subsetEnd - position)));
        position = subsetEnd + 1;
        SkipSubsetWhitespace(outerXml, position);
    }

    if (position >= outerXml.size() || outerXml[position] != '>') {
        throw XmlException("Malformed DOCTYPE declaration");
    }

    return declaration;
}

void AppendSchemaValidationNode(
    XmlDocument& document,
    const std::vector<std::shared_ptr<XmlElement>>& elementStack,
    const std::shared_ptr<XmlNode>& node) {
    if (node == nullptr) {
        return;
    }
    if (elementStack.empty()) {
        document.AppendChild(node);
        return;
    }
    elementStack.back()->AppendChild(node);
}



std::shared_ptr<XmlDocument> BuildXmlDocumentFromReader(XmlReader& reader) {
    auto document = std::make_shared<XmlDocument>();
    LoadXmlDocumentFromReader(reader, *document);
    return document;
}

bool SkipSubsetDeclaration(std::string_view text, std::size_t& position) {
    char quote = '\0';
    while (position < text.size()) {
        const char ch = text[position++];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }

        if (ch == '>') {
            return true;
        }
    }

    return false;
}

[[noreturn]] void ThrowUnsupportedDtdDeclaration(const std::string& kind) {
    throw XmlException("Unsupported DTD declaration: " + kind);
}

[[noreturn]] void ThrowMalformedDtdDeclaration(const std::string& kind) {
    throw XmlException("Malformed DTD " + kind + " declaration");
}

void ParseDocumentTypeInternalSubset(
    const std::string& internalSubset,
    std::vector<std::shared_ptr<XmlNode>>& entities,
    std::vector<std::shared_ptr<XmlNode>>& notations) {
    std::string_view text(internalSubset);
    std::size_t position = 0;
    while (position < text.size()) {
        SkipSubsetWhitespace(text, position);
        if (position >= text.size()) {
            break;
        }

        if (StartsWithAt(text, position, "<!--")) {
            const auto end = text.find("-->", position + 4);
            position = end == std::string_view::npos ? text.size() : end + 3;
            continue;
        }

        if (StartsWithAt(text, position, "<?")) {
            const auto end = text.find("?>", position + 2);
            position = end == std::string_view::npos ? text.size() : end + 2;
            continue;
        }

        if (!SubsetStartsWithAt(text, position, "<!")) {
            ++position;
            continue;
        }

        if (SubsetStartsWithAt(text, position, "<!ENTITY")) {
            position += 8;
            SkipSubsetWhitespace(text, position);
            if (position < text.size() && text[position] == '%') {
                ThrowUnsupportedDtdDeclaration("parameter entity");
            }

            const auto name = ReadSubsetName(text, position);
            if (name.empty()) {
                ThrowMalformedDtdDeclaration("entity");
            }
            SkipSubsetWhitespace(text, position);
            std::string replacementText;
            std::string publicId;
            std::string systemId;
            std::string notationName;

            if (position < text.size() && (text[position] == '\'' || text[position] == '"')) {
                replacementText = ReadSubsetQuotedValue(text, position);
            } else if (SubsetStartsWithAt(text, position, "PUBLIC")) {
                position += 6;
                SkipSubsetWhitespace(text, position);
                publicId = ReadSubsetQuotedValue(text, position);
                SkipSubsetWhitespace(text, position);
                systemId = ReadSubsetQuotedValue(text, position);
                if (publicId.empty() || systemId.empty()) {
                    ThrowMalformedDtdDeclaration("entity");
                }
            } else if (SubsetStartsWithAt(text, position, "SYSTEM")) {
                position += 6;
                SkipSubsetWhitespace(text, position);
                systemId = ReadSubsetQuotedValue(text, position);
                if (systemId.empty()) {
                    ThrowMalformedDtdDeclaration("entity");
                }
            } else {
                ThrowMalformedDtdDeclaration("entity");
            }

            SkipSubsetWhitespace(text, position);
            if (SubsetStartsWithAt(text, position, "NDATA")) {
                position += 5;
                SkipSubsetWhitespace(text, position);
                notationName = ReadSubsetName(text, position);
                if (notationName.empty()) {
                    ThrowMalformedDtdDeclaration("entity");
                }
            }

            if (!SkipSubsetDeclaration(text, position)) {
                ThrowMalformedDtdDeclaration("entity");
            }
            if (!name.empty()) {
                entities.push_back(std::make_shared<XmlEntity>(name, replacementText, publicId, systemId, notationName));
            }
            continue;
        }

        if (SubsetStartsWithAt(text, position, "<!NOTATION")) {
            position += 10;
            SkipSubsetWhitespace(text, position);
            const auto name = ReadSubsetName(text, position);
            if (name.empty()) {
                ThrowMalformedDtdDeclaration("notation");
            }
            SkipSubsetWhitespace(text, position);

            std::string publicId;
            std::string systemId;
            if (SubsetStartsWithAt(text, position, "PUBLIC")) {
                position += 6;
                SkipSubsetWhitespace(text, position);
                publicId = ReadSubsetQuotedValue(text, position);
                if (publicId.empty()) {
                    ThrowMalformedDtdDeclaration("notation");
                }
                SkipSubsetWhitespace(text, position);
                if (position < text.size() && (text[position] == '\'' || text[position] == '"')) {
                    systemId = ReadSubsetQuotedValue(text, position);
                }
            } else if (SubsetStartsWithAt(text, position, "SYSTEM")) {
                position += 6;
                SkipSubsetWhitespace(text, position);
                systemId = ReadSubsetQuotedValue(text, position);
                if (systemId.empty()) {
                    ThrowMalformedDtdDeclaration("notation");
                }
            } else {
                ThrowMalformedDtdDeclaration("notation");
            }

            if (!SkipSubsetDeclaration(text, position)) {
                ThrowMalformedDtdDeclaration("notation");
            }
            if (!name.empty()) {
                notations.push_back(std::make_shared<XmlNotation>(name, publicId, systemId));
            }
            continue;
        }

        if (SubsetStartsWithAt(text, position, "<!ELEMENT")) {
            if (!SkipSubsetDeclaration(text, position)) {
                ThrowMalformedDtdDeclaration("ELEMENT");
            }
            continue;
        }

        if (SubsetStartsWithAt(text, position, "<!ATTLIST")) {
            if (!SkipSubsetDeclaration(text, position)) {
                ThrowMalformedDtdDeclaration("ATTLIST");
            }
            continue;
        }

        if (SubsetStartsWithAt(text, position, "<![")) {
            ThrowUnsupportedDtdDeclaration("conditional section");
        }

        if (!SkipSubsetDeclaration(text, position)) {
            ThrowMalformedDtdDeclaration("declaration");
        }
    }
}

void AppendIndent(std::string& output, int depth, const XmlWriterSettings& settings) {
    for (int index = 0; index < depth; ++index) {
        output += settings.IndentChars;
    }
}

bool ChildrenCanBeIndented(const XmlNode& node) {
    if (!node.HasChildNodes()) {
        return false;
    }

    for (const auto& child : node.ChildNodes()) {
        if (IsTextLike(child->NodeType())) {
            return false;
        }
    }

    return true;
}

void SerializeNode(const XmlNode& node, const XmlWriterSettings& settings, std::string& output, int depth);

void SerializeNodeToStream(const XmlNode& node, const XmlWriterSettings& settings, std::ostream& stream, int depth);

void WriteChildren(const XmlNode& node, const XmlWriterSettings& settings, std::string& output, int depth, bool indented) {
    for (std::size_t index = 0; index < node.ChildNodes().size(); ++index) {
        const auto& child = node.ChildNodes()[index];
        if (indented) {
            output += settings.NewLineChars;
            AppendIndent(output, depth, settings);
        }

        SerializeNode(*child, settings, output, depth);
    }
}

void WriteChildrenToStream(const XmlNode& node, const XmlWriterSettings& settings, std::ostream& stream, int depth, bool indented) {
    for (std::size_t index = 0; index < node.ChildNodes().size(); ++index) {
        const auto& child = node.ChildNodes()[index];
        if (indented) {
            stream << settings.NewLineChars;
            std::string indentation;
            AppendIndent(indentation, depth, settings);
            stream << indentation;
        }

        SerializeNodeToStream(*child, settings, stream, depth);
    }
}

void SerializeNode(const XmlNode& node, const XmlWriterSettings& settings, std::string& output, int depth) {
    switch (node.NodeType()) {
    case XmlNodeType::Document: {
        bool firstWritten = false;
        for (const auto& child : node.ChildNodes()) {
            if (settings.OmitXmlDeclaration && child->NodeType() == XmlNodeType::XmlDeclaration) {
                continue;
            }

            if (firstWritten && settings.Indent) {
                output += settings.NewLineChars;
            }

            SerializeNode(*child, settings, output, depth);
            firstWritten = true;
        }
        break;
    }
    case XmlNodeType::DocumentFragment: {
        bool firstWritten = false;
        for (const auto& child : node.ChildNodes()) {
            if (firstWritten && settings.Indent) {
                output += settings.NewLineChars;
            }

            SerializeNode(*child, settings, output, depth);
            firstWritten = true;
        }
        break;
    }
    case XmlNodeType::XmlDeclaration: {
        const auto& declaration = static_cast<const XmlDeclaration&>(node);
        output += "<?xml version=\"" + EscapeAttribute(declaration.Version(), settings) + "\"";
        if (!declaration.Encoding().empty()) {
            output += " encoding=\"" + EscapeAttribute(declaration.Encoding(), settings) + "\"";
        }
        if (!declaration.Standalone().empty()) {
            output += " standalone=\"" + EscapeAttribute(declaration.Standalone(), settings) + "\"";
        }
        output += "?>";
        break;
    }
    case XmlNodeType::DocumentType: {
        const auto& documentType = static_cast<const XmlDocumentType&>(node);
        output += "<!DOCTYPE " + documentType.Name();
        if (!documentType.PublicId().empty()) {
            output += " PUBLIC \"" + documentType.PublicId() + "\" \"" + documentType.SystemId() + "\"";
        } else if (!documentType.SystemId().empty()) {
            output += " SYSTEM \"" + documentType.SystemId() + "\"";
        }
        if (!documentType.InternalSubset().empty()) {
            output += " [" + documentType.InternalSubset() + "]";
        }
        output += '>';
        break;
    }
    case XmlNodeType::Element: {
        const auto& element = static_cast<const XmlElement&>(node);
        output += '<';
        output += element.Name();
        for (const auto& attribute : element.Attributes()) {
            output += ' ';
            output += attribute->Name();
            output += "=\"";
            output += EscapeAttribute(attribute->Value(), settings);
            output += '\"';
        }

        if (!element.HasChildNodes() && !element.WritesFullEndElement()) {
            output += "/>";
            break;
        }

        const bool indentedChildren = settings.Indent && ChildrenCanBeIndented(element);
        output += '>';
        WriteChildren(element, settings, output, indentedChildren ? depth + 1 : depth, indentedChildren);
        if (indentedChildren) {
            output += settings.NewLineChars;
            AppendIndent(output, depth, settings);
        }
        output += "</" + element.Name() + ">";
        break;
    }
    case XmlNodeType::Attribute:
        output += node.Name();
        output += "=\"";
        output += EscapeAttribute(node.Value(), settings);
        output += '"';
        break;
    case XmlNodeType::Text:
    case XmlNodeType::Whitespace:
    case XmlNodeType::SignificantWhitespace:
        output += EscapeText(node.Value(), settings);
        break;
    case XmlNodeType::EntityReference:
        output += '&';
        output += node.Name();
        output += ';';
        break;
    case XmlNodeType::Entity: {
        const auto& entity = static_cast<const XmlEntity&>(node);
        output += "<!ENTITY " + entity.Name();
        if (!entity.PublicId().empty()) {
            output += " PUBLIC \"" + entity.PublicId() + "\" \"" + entity.SystemId() + "\"";
        } else if (!entity.SystemId().empty()) {
            output += " SYSTEM \"" + entity.SystemId() + "\"";
        } else {
            output += " \"" + entity.Value() + "\"";
        }
        if (!entity.NotationName().empty()) {
            output += " NDATA " + entity.NotationName();
        }
        output += '>';
        break;
    }
    case XmlNodeType::Notation: {
        const auto& notation = static_cast<const XmlNotation&>(node);
        output += "<!NOTATION " + notation.Name();
        if (!notation.PublicId().empty()) {
            output += " PUBLIC \"" + notation.PublicId() + "\"";
            if (!notation.SystemId().empty()) {
                output += " \"" + notation.SystemId() + "\"";
            }
        } else if (!notation.SystemId().empty()) {
            output += " SYSTEM \"" + notation.SystemId() + "\"";
        }
        output += '>';
        break;
    }
    case XmlNodeType::CDATA:
        output += "<![CDATA[" + node.Value() + "]]>";
        break;
    case XmlNodeType::Comment:
        output += "<!--" + node.Value() + "-->";
        break;
    case XmlNodeType::ProcessingInstruction: {
        const auto& instruction = static_cast<const XmlProcessingInstruction&>(node);
        output += "<?" + instruction.Target();
        if (!instruction.Data().empty()) {
            output += ' ';
            output += instruction.Data();
        }
        output += "?>";
        break;
    }
    default:
        throw XmlException("Unsupported node type for serialization");
    }
}

void SerializeNodeToStream(const XmlNode& node, const XmlWriterSettings& settings, std::ostream& stream, int depth) {
    switch (node.NodeType()) {
    case XmlNodeType::Document: {
        bool firstWritten = false;
        for (const auto& child : node.ChildNodes()) {
            if (settings.OmitXmlDeclaration && child->NodeType() == XmlNodeType::XmlDeclaration) {
                continue;
            }

            if (firstWritten && settings.Indent) {
                stream << settings.NewLineChars;
            }

            SerializeNodeToStream(*child, settings, stream, depth);
            firstWritten = true;
        }
        break;
    }
    case XmlNodeType::DocumentFragment: {
        bool firstWritten = false;
        for (const auto& child : node.ChildNodes()) {
            if (firstWritten && settings.Indent) {
                stream << settings.NewLineChars;
            }

            SerializeNodeToStream(*child, settings, stream, depth);
            firstWritten = true;
        }
        break;
    }
    case XmlNodeType::XmlDeclaration: {
        const auto& declaration = static_cast<const XmlDeclaration&>(node);
        stream << "<?xml version=\"" << EscapeAttribute(declaration.Version(), settings) << '\"';
        if (!declaration.Encoding().empty()) {
            stream << " encoding=\"" << EscapeAttribute(declaration.Encoding(), settings) << '\"';
        }
        if (!declaration.Standalone().empty()) {
            stream << " standalone=\"" << EscapeAttribute(declaration.Standalone(), settings) << '\"';
        }
        stream << "?>";
        break;
    }
    case XmlNodeType::DocumentType: {
        const auto& documentType = static_cast<const XmlDocumentType&>(node);
        stream << "<!DOCTYPE " << documentType.Name();
        if (!documentType.PublicId().empty()) {
            stream << " PUBLIC \"" << documentType.PublicId() << "\" \"" << documentType.SystemId() << "\"";
        } else if (!documentType.SystemId().empty()) {
            stream << " SYSTEM \"" << documentType.SystemId() << "\"";
        }
        if (!documentType.InternalSubset().empty()) {
            stream << " [" << documentType.InternalSubset() << ']';
        }
        stream << '>';
        break;
    }
    case XmlNodeType::Element: {
        const auto& element = static_cast<const XmlElement&>(node);
        stream << '<' << element.Name();
        for (const auto& attribute : element.Attributes()) {
            stream << ' ' << attribute->Name() << "=\"" << EscapeAttribute(attribute->Value(), settings) << '\"';
        }

        if (!element.HasChildNodes() && !element.WritesFullEndElement()) {
            stream << "/>";
            break;
        }

        const bool indentedChildren = settings.Indent && ChildrenCanBeIndented(element);
        stream << '>';
        WriteChildrenToStream(element, settings, stream, indentedChildren ? depth + 1 : depth, indentedChildren);
        if (indentedChildren) {
            stream << settings.NewLineChars;
            std::string indentation;
            AppendIndent(indentation, depth, settings);
            stream << indentation;
        }
        stream << "</" << element.Name() << ">";
        break;
    }
    case XmlNodeType::Attribute:
        stream << node.Name() << "=\"" << EscapeAttribute(node.Value(), settings) << '\"';
        break;
    case XmlNodeType::Text:
    case XmlNodeType::Whitespace:
    case XmlNodeType::SignificantWhitespace:
        stream << EscapeText(node.Value(), settings);
        break;
    case XmlNodeType::EntityReference:
        stream << '&' << node.Name() << ';';
        break;
    case XmlNodeType::Entity: {
        const auto& entity = static_cast<const XmlEntity&>(node);
        stream << "<!ENTITY " << entity.Name();
        if (!entity.PublicId().empty()) {
            stream << " PUBLIC \"" << entity.PublicId() << "\" \"" << entity.SystemId() << "\"";
        } else if (!entity.SystemId().empty()) {
            stream << " SYSTEM \"" << entity.SystemId() << "\"";
        } else {
            stream << " \"" << entity.Value() << "\"";
        }
        if (!entity.NotationName().empty()) {
            stream << " NDATA " << entity.NotationName();
        }
        stream << '>';
        break;
    }
    case XmlNodeType::Notation: {
        const auto& notation = static_cast<const XmlNotation&>(node);
        stream << "<!NOTATION " << notation.Name();
        if (!notation.PublicId().empty()) {
            stream << " PUBLIC \"" << notation.PublicId() << "\"";
            if (!notation.SystemId().empty()) {
                stream << " \"" << notation.SystemId() << "\"";
            }
        } else if (!notation.SystemId().empty()) {
            stream << " SYSTEM \"" << notation.SystemId() << "\"";
        }
        stream << '>';
        break;
    }
    case XmlNodeType::CDATA:
        stream << "<![CDATA[" << node.Value() << "]]>";
        break;
    case XmlNodeType::Comment:
        stream << "<!--" << node.Value() << "-->";
        break;
    case XmlNodeType::ProcessingInstruction: {
        const auto& instruction = static_cast<const XmlProcessingInstruction&>(node);
        stream << "<?" << instruction.Target();
        if (!instruction.Data().empty()) {
            stream << ' ' << instruction.Data();
        }
        stream << "?>";
        break;
    }
    default:
        throw XmlException("Unsupported node type for serialization");
    }
}


}  // namespace

class XmlReaderInputSource {
public:
    virtual ~XmlReaderInputSource() = default;

    virtual char CharAt(std::size_t position) const noexcept = 0;
    virtual const char* PtrAt(std::size_t position, std::size_t& available) const noexcept = 0;
    virtual std::size_t Find(const std::string& token, std::size_t position) const noexcept = 0;
    virtual std::string Slice(std::size_t start, std::size_t count) const = 0;
    virtual void AppendSliceTo(std::string& target, std::size_t start, std::size_t count) const = 0;
    virtual void EnableReplay() const {}
    virtual void DiscardBefore(std::size_t) const {}
};


class StringXmlReaderInputSource final : public XmlReaderInputSource {
public:
    explicit StringXmlReaderInputSource(std::shared_ptr<const std::string> text)
        : text_(std::move(text)) {
    }

    const std::shared_ptr<const std::string>& Text() const noexcept {
        return text_;
    }

    char CharAt(std::size_t position) const noexcept override {
        return position < text_->size() ? (*text_)[position] : '\0';
    }

    const char* PtrAt(std::size_t position, std::size_t& available) const noexcept override {
        available = 0;
        if (text_ == nullptr || position >= text_->size()) {
            return nullptr;
        }

        available = text_->size() - position;
        return text_->data() + static_cast<std::ptrdiff_t>(position);
    }

    std::size_t Find(const std::string& token, std::size_t position) const noexcept override {
        if (token.size() == 1) {
            return text_->find(token.front(), position);
        }
        return text_->find(token, position);
    }

    std::size_t FindFirstOf(std::string_view tokens, std::size_t position) const noexcept {
        if (tokens.empty()) {
            return position;
        }
        return text_->find_first_of(tokens, position);
    }

    std::size_t ScanQuotedValueEnd(std::size_t quoteStart) const noexcept {
        if (text_ == nullptr || quoteStart >= text_->size()) {
            return std::string::npos;
        }

        const char quote = (*text_)[quoteStart];
        if (quote != '"' && quote != '\'') {
            return std::string::npos;
        }

        const std::size_t scanStart = quoteStart + 1;
        const std::size_t remaining = scanStart < text_->size() ? text_->size() - scanStart : 0;
        const std::size_t offset = FindQuoteOrNulInBuffer(text_->data() + scanStart, remaining, quote);
        if (offset == std::string::npos) {
            return std::string::npos;
        }

        const std::size_t match = scanStart + offset;
        return (*text_)[match] == quote ? match : std::string::npos;
    }

    std::string Slice(std::size_t start, std::size_t count) const override {
        return text_->substr(start, count);
    }

    void AppendSliceTo(std::string& target, std::size_t start, std::size_t count) const override {
        if (text_ == nullptr || start >= text_->size()) {
            return;
        }

        const std::size_t available = text_->size() - start;
        const std::size_t actualCount = count == std::string::npos ? available : (std::min)(count, available);
        target.append(*text_, start, actualCount);
    }

private:
    std::shared_ptr<const std::string> text_;
};

}  // namespace System::Xml
