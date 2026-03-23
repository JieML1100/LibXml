#include "System/Xml/Xml.h"

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

template <typename CharAtFn>
XmlMarkupKind ClassifyXmlMarkupWithCharAt(std::size_t position, CharAtFn&& charAt) noexcept {
    if (charAt(position) != '<') {
        return XmlMarkupKind::None;
    }

    const char ch1 = charAt(position + 1);
    if (ch1 == '?') {
        const char ch2 = charAt(position + 2);
        if (ch2 == 'x'
            && charAt(position + 3) == 'm'
            && charAt(position + 4) == 'l') {
            return XmlMarkupKind::XmlDeclaration;
        }
        return XmlMarkupKind::ProcessingInstruction;
    }

    if (ch1 == '!') {
        const char ch2 = charAt(position + 2);
        if (ch2 == '-') {
            if (charAt(position + 3) == '-' ) {
                return XmlMarkupKind::Comment;
            }
            return XmlMarkupKind::UnsupportedDeclaration;
        }
        if (ch2 == '[') {
            if (charAt(position + 3) == 'C'
                && charAt(position + 4) == 'D'
                && charAt(position + 5) == 'A'
                && charAt(position + 6) == 'T'
                && charAt(position + 7) == 'A'
                && charAt(position + 8) == '[') {
                return XmlMarkupKind::CData;
            }
            return XmlMarkupKind::UnsupportedDeclaration;
        }
        if (ch2 == 'D'
            && charAt(position + 3) == 'O'
            && charAt(position + 4) == 'C'
            && charAt(position + 5) == 'T'
            && charAt(position + 6) == 'Y'
            && charAt(position + 7) == 'P'
            && charAt(position + 8) == 'E') {
            return XmlMarkupKind::DocumentType;
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

XmlMarkupKind ClassifyXmlMarkupAt(const char* p, std::size_t available) noexcept
{
    if (p == nullptr || available == 0 || p[0] != '<')
        return XmlMarkupKind::None;

    if (available < 2)
        return XmlMarkupKind::None;

    const char c1 = p[1];

    if (c1 == '?')
    {
        if (available >= 3 && p[2] != 'x')
            return XmlMarkupKind::ProcessingInstruction;

        if (available >= 4 && p[2] == 'x' && p[3] != 'm')
            return XmlMarkupKind::ProcessingInstruction;

        if (available < 5)
            return XmlMarkupKind::None;

        if (p[2] == 'x' && p[3] == 'm' && p[4] == 'l')
            return XmlMarkupKind::XmlDeclaration;

        return XmlMarkupKind::ProcessingInstruction;
    }

    if (c1 == '!')
    {
        if (available < 3)
            return XmlMarkupKind::None;

        const char c2 = p[2];

        if (c2 == '-') {
            if (available < 4)
                return XmlMarkupKind::None;
            return p[3] == '-' ? XmlMarkupKind::Comment : XmlMarkupKind::UnsupportedDeclaration;
        }

        if (c2 == '[') {
            if (available < 9)
                return XmlMarkupKind::None;
            return p[3] == 'C' && p[4] == 'D' && p[5] == 'A'
                && p[6] == 'T' && p[7] == 'A' && p[8] == '['
                ? XmlMarkupKind::CData
                : XmlMarkupKind::UnsupportedDeclaration;
        }

        if (c2 == 'D') {
            if (available < 9)
                return XmlMarkupKind::None;
            return p[3] == 'O' && p[4] == 'C' && p[5] == 'T'
                && p[6] == 'Y' && p[7] == 'P' && p[8] == 'E'
                ? XmlMarkupKind::DocumentType
                : XmlMarkupKind::UnsupportedDeclaration;
        }

        return XmlMarkupKind::UnsupportedDeclaration;
    }

    if (c1 == '/')
        return XmlMarkupKind::EndTag;

    if (c1 == '\0')
        return XmlMarkupKind::None;

    return XmlMarkupKind::Element;
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
                payload += overrideChildElement->OuterXml();
            }
            continue;
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
            Contains,
            StartsWith,
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
        if (segment.empty() || segment == "..") {
            ThrowUnsupportedXPathFeature("predicate path [" + expression + "]");
        }

        XPathStep::PredicatePathSegment parsed;
        if (segment == ".") {
            parsed.kind = XPathStep::PredicatePathSegment::Kind::Self;
        } else if (segment == "text()") {
            parsed.kind = XPathStep::PredicatePathSegment::Kind::Text;
        } else if (!segment.empty() && segment[0] == '@') {
            parsed.kind = XPathStep::PredicatePathSegment::Kind::Attribute;
            parsed.name = ParseXPathName(segment, true);
        } else {
            parsed.kind = XPathStep::PredicatePathSegment::Kind::Element;
            parsed.name = ParseXPathName(segment, false);
        }

        if (!path.empty() && path.back().kind != XPathStep::PredicatePathSegment::Kind::Element) {
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
    const std::string prefix = std::string(functionName) + '(';
    if (normalized.rfind(prefix, 0) != 0 || normalized.empty() || normalized.back() != ')') {
        return std::nullopt;
    }

    return ParseXPathFunctionArguments(
        normalized.substr(prefix.size(), normalized.size() - prefix.size() - 1));
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
        || TryParseXPathBinaryPredicateFunction(result, expression, predicate, "starts-with", XPathStep::Predicate::Kind::StartsWith)) {
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

    if (normalized.rfind("not(", 0) == 0 && normalized.back() == ')') {
        result.kind = XPathStep::Predicate::Kind::Not;
        result.operands.push_back(ParseXPathPredicateExpression(normalized.substr(4, normalized.size() - 5)));
        return result;
    }

    if (normalized == "last()") {
        result.kind = XPathStep::Predicate::Kind::Last;
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

    if (normalized.rfind("contains(", 0) == 0 || normalized.rfind("starts-with(", 0) == 0) {
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

} // namespace

std::string LookupNamespaceUriOnElement(const XmlElement* element, std::string_view prefix) {
    for (auto current = element; current != nullptr; ) {
        std::string_view value;
        if (current->TryFindNamespaceDeclarationValueView(prefix, value)) {
            return std::string(value);
        }

        const XmlNode* parent = current->ParentNode();
        current = parent != nullptr && parent->NodeType() == XmlNodeType::Element
            ? static_cast<const XmlElement*>(parent)
            : nullptr;
    }

    if (prefix == "xml") {
        return "http://www.w3.org/XML/1998/namespace";
    }
    if (prefix == "xmlns") {
        return "http://www.w3.org/2000/xmlns/";
    }

    return {};
}

void LoadXmlDocumentFromReader(XmlReader& reader, XmlDocument& document) {
    document.RemoveAll();
    std::vector<std::shared_ptr<XmlElement>> elementStack;
    int entityExpansionDepth = 0;
    const bool useFastAppend = !document.HasNodeChangeHandlers();

    const auto createOwnedTextNode = [&document, &reader]() {
        static_cast<void>(reader.Value());
        auto node = std::make_shared<XmlText>(std::move(reader.currentValue_));
        node->SetOwnerDocument(&document);
        return node;
    };

    const auto createOwnedWhitespaceNode = [&document, &reader]() {
        static_cast<void>(reader.Value());
        auto node = std::make_shared<XmlWhitespace>(std::move(reader.currentValue_));
        node->SetOwnerDocument(&document);
        return node;
    };

    const auto createOwnedCDataNode = [&document, &reader]() {
        static_cast<void>(reader.Value());
        auto node = std::make_shared<XmlCDataSection>(std::move(reader.currentValue_));
        node->SetOwnerDocument(&document);
        return node;
    };

    const auto createOwnedCommentNode = [&document, &reader]() {
        static_cast<void>(reader.Value());
        auto node = std::make_shared<XmlComment>(std::move(reader.currentValue_));
        node->SetOwnerDocument(&document);
        return node;
    };

    const auto createOwnedElementNode = [&document, &reader]() {
        auto node = std::make_shared<XmlElement>(std::move(reader.currentName_));
        node->SetOwnerDocument(&document);
        reader.AppendCurrentAttributesForLoad(*node);
        return node;
    };

    const auto appendNode = [&](std::shared_ptr<XmlNode> node, bool ownerAlreadyAssigned) {
        if (node == nullptr) {
            return;
        }
        if (elementStack.empty()) {
            if (useFastAppend) {
                if (ownerAlreadyAssigned) {
                    document.AppendChildForOwnedLoad(node);
                } else {
                    document.AppendChildForLoad(node);
                }
            } else {
                document.AppendChild(node);
            }
            return;
        }

        if (useFastAppend) {
            if (ownerAlreadyAssigned) {
                elementStack.back()->AppendChildForOwnedLoad(node);
            } else {
                elementStack.back()->AppendChildForLoad(node);
            }
        } else {
            elementStack.back()->AppendChild(node);
        }
    };

    const auto appendCurrentReaderNode = [&]() {
        const auto nodeType = reader.NodeType();
        if (entityExpansionDepth > 0) {
            if (nodeType == XmlNodeType::EntityReference) {
                ++entityExpansionDepth;
            } else if (nodeType == XmlNodeType::EndEntity) {
                --entityExpansionDepth;
            }
            return;
        }

        switch (nodeType) {
        case XmlNodeType::Element: {
            auto element = createOwnedElementNode();
            appendNode(element, true);
            if (!reader.IsEmptyElement()) {
                elementStack.push_back(std::move(element));
            }
            break;
        }
        case XmlNodeType::EndElement:
            if (!elementStack.empty()) {
                elementStack.pop_back();
            }
            break;
        case XmlNodeType::Text:
            appendNode(createOwnedTextNode(), true);
            break;
        case XmlNodeType::CDATA:
            appendNode(createOwnedCDataNode(), true);
            break;
        case XmlNodeType::Whitespace:
            if (document.PreserveWhitespace()) {
                appendNode(createOwnedWhitespaceNode(), true);
            }
            break;
        case XmlNodeType::SignificantWhitespace:
            appendNode(createOwnedTextNode(), true);
            break;
        case XmlNodeType::Comment:
            appendNode(createOwnedCommentNode(), true);
            break;
        case XmlNodeType::ProcessingInstruction:
            appendNode(document.CreateProcessingInstruction(reader.Name(), reader.Value()), true);
            break;
        case XmlNodeType::XmlDeclaration: {
            const auto declaration = ParseXmlDeclarationValue(reader.Value());
            appendNode(document.CreateXmlDeclaration(declaration.version, declaration.encoding, declaration.standalone), true);
            break;
        }
        case XmlNodeType::DocumentType: {
            const auto declaration = ParseDocumentTypeDeclaration(reader.ReadOuterXml());
            appendNode(document.CreateDocumentType(
                declaration.name,
                declaration.publicId,
                declaration.systemId,
                declaration.internalSubset),
                false);
            break;
        }
        case XmlNodeType::EntityReference:
            appendNode(document.CreateEntityReference(reader.Name()), true);
            entityExpansionDepth = 1;
            break;
        case XmlNodeType::EndEntity:
        case XmlNodeType::None:
        case XmlNodeType::Attribute:
        case XmlNodeType::Entity:
        case XmlNodeType::DocumentFragment:
        case XmlNodeType::Notation:
            break;
        }
    };

    if (reader.GetReadState() == ReadState::Interactive && reader.NodeType() != XmlNodeType::None) {
        appendCurrentReaderNode();
    }

    while (reader.Read()) {
        appendCurrentReaderNode();
    }
}

namespace {

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
            ThrowUnsupportedDtdDeclaration("ELEMENT");
        }

        if (SubsetStartsWithAt(text, position, "<!ATTLIST")) {
            ThrowUnsupportedDtdDeclaration("ATTLIST");
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

void ValidateXmlReaderInputAgainstSchemas(const std::string& xml, const XmlReaderSettings& settings);
void ValidateXmlReaderInputAgainstSchemas(XmlReader& reader, const XmlReaderSettings& settings);

XmlException::XmlException(const std::string& message, std::size_t line, std::size_t column)
    : std::runtime_error(BuildExceptionMessage(message, line, column)), line_(line), column_(column) {
}

std::string XmlResolver::ResolveUri(const std::string& baseUri, const std::string& relativeUri) const {
    if (relativeUri.empty()) {
        return baseUri;
    }
    if (baseUri.empty()) {
        return relativeUri;
    }
    return (std::filesystem::path(baseUri).parent_path() / std::filesystem::path(relativeUri)).lexically_normal().string();
}

std::string XmlResolver::GetEntity(const std::string& absoluteUri) const {
    std::ifstream stream(std::filesystem::path(absoluteUri), std::ios::binary);
    if (!stream) {
        throw XmlException("Failed to resolve XML entity: " + absoluteUri);
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string XmlUrlResolver::ResolveUri(const std::string& baseUri, const std::string& relativeUri) const {
    return XmlResolver::ResolveUri(baseUri, relativeUri);
}

std::string XmlUrlResolver::GetEntity(const std::string& absoluteUri) const {
    return XmlResolver::GetEntity(absoluteUri);
}

XmlNameTable::XmlNameTable() = default;

XmlNameTable::XmlNameTable(XmlNameTable&& other) noexcept
    : values_(std::move(other.values_)),
      firstBlock_(other.firstBlock_),
      currentBlock_(other.currentBlock_) {
    other.firstBlock_ = nullptr;
    other.currentBlock_ = nullptr;
}

XmlNameTable& XmlNameTable::operator=(XmlNameTable&& other) noexcept {
    if (this != &other) {
        auto* current = firstBlock_;
        while (current != nullptr) {
            for (std::size_t i = 0; i < current->used; ++i) {
                reinterpret_cast<std::string*>(current->storage + i * sizeof(std::string))->~basic_string();
            }
            auto* next = current->next;
            delete current;
            current = next;
        }

        values_ = std::move(other.values_);
        firstBlock_ = other.firstBlock_;
        currentBlock_ = other.currentBlock_;

        other.firstBlock_ = nullptr;
        other.currentBlock_ = nullptr;
    }
    return *this;
}

XmlNameTable::~XmlNameTable() {
    auto* current = firstBlock_;
    while (current != nullptr) {
        for (std::size_t i = 0; i < current->used; ++i) {
            reinterpret_cast<std::string*>(current->storage + i * sizeof(std::string))->~basic_string();
        }
        auto* next = current->next;
        delete current;
        current = next;
    }
}

const std::string& XmlNameTable::AllocateString(std::string_view value) {
    if (currentBlock_ == nullptr || currentBlock_->used == ArenaBlock::Capacity) {
        auto* nextBlock = new ArenaBlock();
        if (currentBlock_ != nullptr) {
            currentBlock_->next = nextBlock;
        } else {
            firstBlock_ = nextBlock;
        }
        currentBlock_ = nextBlock;
    }

    std::string* newString = new (currentBlock_->storage + currentBlock_->used * sizeof(std::string)) std::string(value);
    ++currentBlock_->used;
    return *newString;
}

const std::string& XmlNameTable::Add(std::string_view value) {
    auto it = values_.find(value);
    if (it != values_.end()) {
        return **it;
    }
    const std::string& newStr = AllocateString(value);
    values_.insert(&newStr);
    return newStr;
}

const std::string* XmlNameTable::Get(std::string_view value) const {
    auto it = values_.find(value);
    if (it != values_.end()) {
        return *it;
    }
    return nullptr;
}

std::size_t XmlNameTable::Count() const noexcept {
    return values_.size();
}

XmlNodeList::XmlNodeList(std::vector<std::shared_ptr<XmlNode>> nodes) : nodes_(std::move(nodes)) {
}

std::size_t XmlNodeList::Count() const noexcept {
    return nodes_.size();
}

std::shared_ptr<XmlNode> XmlNodeList::Item(std::size_t index) const {
    return index < nodes_.size() ? nodes_[index] : nullptr;
}

bool XmlNodeList::Empty() const noexcept {
    return nodes_.empty();
}

std::vector<std::shared_ptr<XmlNode>>::const_iterator XmlNodeList::begin() const noexcept {
    return nodes_.begin();
}

std::vector<std::shared_ptr<XmlNode>>::const_iterator XmlNodeList::end() const noexcept {
    return nodes_.end();
}

XmlAttributeCollection::XmlAttributeCollection(std::vector<std::shared_ptr<XmlAttribute>> attributes)
    : attributes_(std::move(attributes)) {
}

std::size_t XmlAttributeCollection::Count() const noexcept {
    return attributes_.size();
}

std::shared_ptr<XmlAttribute> XmlAttributeCollection::Item(std::size_t index) const {
    return index < attributes_.size() ? attributes_[index] : nullptr;
}

std::shared_ptr<XmlAttribute> XmlAttributeCollection::Item(const std::string& name) const {
    const auto found = std::find_if(attributes_.begin(), attributes_.end(), [&name](const auto& attribute) {
        return attribute->Name() == name || attribute->LocalName() == name;
    });
    return found == attributes_.end() ? nullptr : *found;
}

bool XmlAttributeCollection::Empty() const noexcept {
    return attributes_.empty();
}

std::vector<std::shared_ptr<XmlAttribute>>::const_iterator XmlAttributeCollection::begin() const noexcept {
    return attributes_.begin();
}

std::vector<std::shared_ptr<XmlAttribute>>::const_iterator XmlAttributeCollection::end() const noexcept {
    return attributes_.end();
}

XmlNamedNodeMap::XmlNamedNodeMap(std::vector<std::shared_ptr<XmlNode>> nodes) : nodes_(std::move(nodes)) {
}

std::size_t XmlNamedNodeMap::Count() const noexcept {
    return nodes_.size();
}

std::shared_ptr<XmlNode> XmlNamedNodeMap::Item(std::size_t index) const {
    return index < nodes_.size() ? nodes_[index] : nullptr;
}

std::shared_ptr<XmlNode> XmlNamedNodeMap::GetNamedItem(const std::string& name) const {
    const auto found = std::find_if(nodes_.begin(), nodes_.end(), [&name](const auto& node) {
        return node != nullptr && node->Name() == name;
    });
    return found == nodes_.end() ? nullptr : *found;
}

bool XmlNamedNodeMap::Empty() const noexcept {
    return nodes_.empty();
}

std::vector<std::shared_ptr<XmlNode>>::const_iterator XmlNamedNodeMap::begin() const noexcept {
    return nodes_.begin();
}

std::vector<std::shared_ptr<XmlNode>>::const_iterator XmlNamedNodeMap::end() const noexcept {
    return nodes_.end();
}

std::size_t XmlException::Line() const noexcept {
    return line_;
}

std::size_t XmlException::Column() const noexcept {
    return column_;
}

XmlNode::XmlNode(XmlNodeType nodeType, std::string name, std::string value)
    : nodeType_(nodeType), name_(std::move(name)), value_(std::move(value)), parent_(nullptr), ownerDocument_(nullptr) {
}

XmlNodeType XmlNode::NodeType() const noexcept {
    return nodeType_;
}

const std::string& XmlNode::Name() const noexcept {
    return name_;
}

std::string XmlNode::LocalName() const {
    return std::string{SplitQualifiedNameView(Name()).second};
}

std::string XmlNode::Prefix() const {
    return std::string{SplitQualifiedNameView(Name()).first};
}

std::string XmlNode::NamespaceURI() const {
    return ResolveNodeNamespaceUri(*this);
}

std::string XmlNode::GetNamespaceOfPrefix(const std::string& prefix) const {
    if (NodeType() == XmlNodeType::Element) {
        return LookupNamespaceUriOnElement(static_cast<const XmlElement*>(this), prefix);
    }
    if (NodeType() == XmlNodeType::Attribute) {
        const auto* parent = ParentNode();
        if (parent != nullptr && parent->NodeType() == XmlNodeType::Element) {
            return LookupNamespaceUriOnElement(static_cast<const XmlElement*>(parent), prefix);
        }
    }
    // Walk up to nearest element ancestor
    for (const auto* current = ParentNode(); current != nullptr; current = current->ParentNode()) {
        if (current->NodeType() == XmlNodeType::Element) {
            return LookupNamespaceUriOnElement(static_cast<const XmlElement*>(current), prefix);
        }
    }
    if (prefix == "xml") return "http://www.w3.org/XML/1998/namespace";
    if (prefix == "xmlns") return "http://www.w3.org/2000/xmlns/";
    return {};
}

std::string XmlNode::GetPrefixOfNamespace(const std::string& namespaceUri) const {
    if (namespaceUri.empty()) return {};
    if (NodeType() == XmlNodeType::Element) {
        return LookupPrefixOnElement(static_cast<const XmlElement*>(this), namespaceUri);
    }
    if (NodeType() == XmlNodeType::Attribute) {
        const auto* parent = ParentNode();
        if (parent != nullptr && parent->NodeType() == XmlNodeType::Element) {
            return LookupPrefixOnElement(static_cast<const XmlElement*>(parent), namespaceUri);
        }
    }
    for (const auto* current = ParentNode(); current != nullptr; current = current->ParentNode()) {
        if (current->NodeType() == XmlNodeType::Element) {
            return LookupPrefixOnElement(static_cast<const XmlElement*>(current), namespaceUri);
        }
    }
    if (namespaceUri == "http://www.w3.org/XML/1998/namespace") return "xml";
    if (namespaceUri == "http://www.w3.org/2000/xmlns/") return "xmlns";
    return {};
}

const std::string& XmlNode::Value() const noexcept {
    return value_;
}

void XmlNode::SetValue(const std::string& value) {
    switch (NodeType()) {
    case XmlNodeType::Attribute:
    case XmlNodeType::Text:
    case XmlNodeType::CDATA:
    case XmlNodeType::Comment:
    case XmlNodeType::ProcessingInstruction:
    case XmlNodeType::Whitespace:
    case XmlNodeType::SignificantWhitespace: {
        const std::string oldValue = value_;
        if (ownerDocument_ != nullptr) {
            ownerDocument_->FireNodeChanging(this, oldValue, value);
        }
        value_ = value;
        if (ownerDocument_ != nullptr) {
            ownerDocument_->FireNodeChanged(this, oldValue, value_);
        }
        return;
    }
    default:
        throw XmlException(std::string("Cannot set a value on node type '") + NodeTypeDisplayName(NodeType()) + "'.");
    }
}

XmlNode* XmlNode::ParentNode() noexcept {
    return parent_;
}

const XmlNode* XmlNode::ParentNode() const noexcept {
    return parent_;
}

XmlDocument* XmlNode::OwnerDocument() noexcept {
    return ownerDocument_;
}

const XmlDocument* XmlNode::OwnerDocument() const noexcept {
    return ownerDocument_;
}

const std::vector<std::shared_ptr<XmlNode>>& XmlNode::ChildNodes() const noexcept {
    return childNodes_;
}

XmlNodeList XmlNode::ChildNodeList() const {
    return XmlNodeList(childNodes_);
}

std::vector<std::shared_ptr<XmlNode>>& XmlNode::MutableChildNodes() noexcept {
    return childNodes_;
}

std::shared_ptr<XmlNode> XmlNode::FirstChild() const {
    return childNodes_.empty() ? nullptr : childNodes_.front();
}

std::shared_ptr<XmlNode> XmlNode::LastChild() const {
    return childNodes_.empty() ? nullptr : childNodes_.back();
}

std::shared_ptr<XmlNode> XmlNode::PreviousSibling() const {
    if (parent_ == nullptr) {
        return nullptr;
    }

    const auto& siblings = parent_->ChildNodes();
    const auto found = std::find_if(siblings.begin(), siblings.end(), [this](const auto& sibling) {
        return sibling.get() == this;
    });

    if (found == siblings.begin() || found == siblings.end()) {
        return nullptr;
    }

    return *(found - 1);
}

std::shared_ptr<XmlNode> XmlNode::NextSibling() const {
    if (parent_ == nullptr) {
        return nullptr;
    }

    const auto& siblings = parent_->ChildNodes();
    const auto found = std::find_if(siblings.begin(), siblings.end(), [this](const auto& sibling) {
        return sibling.get() == this;
    });

    if (found == siblings.end() || found + 1 == siblings.end()) {
        return nullptr;
    }

    return *(found + 1);
}

bool XmlNode::HasChildNodes() const noexcept {
    return !childNodes_.empty();
}

void XmlNode::ValidateChildInsertion(const std::shared_ptr<XmlNode>& child, const XmlNode* replacingChild) const {
    if (!child) {
        throw XmlException("Cannot append a null child node");
    }
    if (child.get() == this) {
        throw XmlException("A node cannot be appended to itself");
    }
    if (child->OwnerDocument() != nullptr && OwnerDocument() != nullptr && child->OwnerDocument() != OwnerDocument()) {
        throw XmlException("The node belongs to a different document");
    }
    if (child->NodeType() == XmlNodeType::DocumentFragment) {
        for (const auto& fragmentChild : child->ChildNodes()) {
            if (!fragmentChild) {
                throw XmlException("Document fragments cannot contain null child nodes");
            }
            if (fragmentChild.get() == this) {
                throw XmlException("A node cannot be appended to itself");
            }
            if (fragmentChild->NodeType() == XmlNodeType::Attribute) {
                throw XmlException("Attributes cannot be appended as child nodes");
            }
            if (fragmentChild->OwnerDocument() != nullptr
                && OwnerDocument() != nullptr
                && fragmentChild->OwnerDocument() != OwnerDocument()) {
                throw XmlException("The node belongs to a different document");
            }
            if (fragmentChild->ParentNode() != nullptr
                && fragmentChild->ParentNode() != child.get()
                && fragmentChild->ParentNode() != this) {
                throw XmlException("The node already has a parent");
            }
            if (fragmentChild->ParentNode() == this && fragmentChild.get() != replacingChild) {
                throw XmlException("The node already belongs to the current parent");
            }
        }
        return;
    }
    if (child->NodeType() == XmlNodeType::Attribute) {
        throw XmlException("Attributes cannot be appended as child nodes");
    }
    if (child->ParentNode() != nullptr && child->ParentNode() != this) {
        throw XmlException("The node already has a parent");
    }
    if (child->ParentNode() == this && child.get() != replacingChild) {
        throw XmlException("The node already belongs to the current parent");
    }
}

std::shared_ptr<XmlNode> XmlNode::AppendChild(const std::shared_ptr<XmlNode>& child) {
    ValidateChildInsertion(child);

    if (child->NodeType() == XmlNodeType::DocumentFragment) {
        while (child->FirstChild() != nullptr) {
            auto fragmentChild = child->RemoveChild(child->FirstChild());
            AppendChild(fragmentChild);
        }
        return child;
    }

    if (ownerDocument_ != nullptr) {
        ownerDocument_->FireNodeInserting(child.get(), this);
    }
    child->SetParent(this);
    if (child->OwnerDocument() != ownerDocument_) {
        child->SetOwnerDocumentRecursive(ownerDocument_);
    }
    childNodes_.push_back(child);
    if (ownerDocument_ != nullptr) {
        ownerDocument_->FireNodeInserted(child.get(), this);
    }
    return child;
}

std::shared_ptr<XmlNode> XmlNode::AppendChildForLoad(const std::shared_ptr<XmlNode>& child) {
    if (child == nullptr) {
        return child;
    }

    child->SetParent(this);
    if (child->OwnerDocument() != ownerDocument_) {
        child->SetOwnerDocumentRecursive(ownerDocument_);
    }
    childNodes_.push_back(child);
    return child;
}

std::shared_ptr<XmlNode> XmlNode::AppendChildForOwnedLoad(const std::shared_ptr<XmlNode>& child) {
    if (child == nullptr) {
        return child;
    }

    child->SetParent(this);
    childNodes_.push_back(child);
    return child;
}

std::shared_ptr<XmlNode> XmlNode::InsertBefore(
    const std::shared_ptr<XmlNode>& newChild,
    const std::shared_ptr<XmlNode>& referenceChild) {
    if (!referenceChild) {
        return AppendChild(newChild);
    }

    const auto index = FindChildIndexOrThrow(*this, referenceChild);
    ValidateChildInsertion(newChild);

    if (newChild->NodeType() == XmlNodeType::DocumentFragment) {
        auto insertionIndex = index;
        while (newChild->FirstChild() != nullptr) {
            auto fragmentChild = newChild->RemoveChild(newChild->FirstChild());
            ValidateChildInsertion(fragmentChild);
            if (ownerDocument_ != nullptr) {
                ownerDocument_->FireNodeInserting(fragmentChild.get(), this);
            }
            fragmentChild->SetParent(this);
            fragmentChild->SetOwnerDocumentRecursive(ownerDocument_);
            MutableChildNodes().insert(MutableChildNodes().begin() + static_cast<std::ptrdiff_t>(insertionIndex), fragmentChild);
            if (ownerDocument_ != nullptr) {
                ownerDocument_->FireNodeInserted(fragmentChild.get(), this);
            }
            ++insertionIndex;
        }
        return newChild;
    }

    if (ownerDocument_ != nullptr) {
        ownerDocument_->FireNodeInserting(newChild.get(), this);
    }
    newChild->SetParent(this);
    newChild->SetOwnerDocumentRecursive(ownerDocument_);
    MutableChildNodes().insert(MutableChildNodes().begin() + static_cast<std::ptrdiff_t>(index), newChild);
    if (ownerDocument_ != nullptr) {
        ownerDocument_->FireNodeInserted(newChild.get(), this);
    }
    return newChild;
}

std::shared_ptr<XmlNode> XmlNode::InsertAfter(
    const std::shared_ptr<XmlNode>& newChild,
    const std::shared_ptr<XmlNode>& referenceChild) {
    if (!referenceChild) {
        ValidateChildInsertion(newChild);
        if (ownerDocument_ != nullptr) {
            ownerDocument_->FireNodeInserting(newChild.get(), this);
        }
        newChild->SetParent(this);
        newChild->SetOwnerDocumentRecursive(ownerDocument_);
        MutableChildNodes().insert(MutableChildNodes().begin(), newChild);
        if (ownerDocument_ != nullptr) {
            ownerDocument_->FireNodeInserted(newChild.get(), this);
        }
        return newChild;
    }

    const auto index = FindChildIndexOrThrow(*this, referenceChild);
    ValidateChildInsertion(newChild);

    if (newChild->NodeType() == XmlNodeType::DocumentFragment) {
        auto insertionIndex = index + 1;
        while (newChild->FirstChild() != nullptr) {
            auto fragmentChild = newChild->RemoveChild(newChild->FirstChild());
            ValidateChildInsertion(fragmentChild);
            if (ownerDocument_ != nullptr) {
                ownerDocument_->FireNodeInserting(fragmentChild.get(), this);
            }
            fragmentChild->SetParent(this);
            fragmentChild->SetOwnerDocumentRecursive(ownerDocument_);
            MutableChildNodes().insert(MutableChildNodes().begin() + static_cast<std::ptrdiff_t>(insertionIndex), fragmentChild);
            if (ownerDocument_ != nullptr) {
                ownerDocument_->FireNodeInserted(fragmentChild.get(), this);
            }
            ++insertionIndex;
        }
        return newChild;
    }

    if (ownerDocument_ != nullptr) {
        ownerDocument_->FireNodeInserting(newChild.get(), this);
    }
    newChild->SetParent(this);
    newChild->SetOwnerDocumentRecursive(ownerDocument_);
    MutableChildNodes().insert(MutableChildNodes().begin() + static_cast<std::ptrdiff_t>(index + 1), newChild);
    if (ownerDocument_ != nullptr) {
        ownerDocument_->FireNodeInserted(newChild.get(), this);
    }
    return newChild;
}

std::shared_ptr<XmlNode> XmlNode::ReplaceChild(
    const std::shared_ptr<XmlNode>& newChild,
    const std::shared_ptr<XmlNode>& oldChild) {
    if (newChild == oldChild) {
        return oldChild;
    }

    const auto index = FindChildIndexOrThrow(*this, oldChild);
    ValidateChildInsertion(newChild, oldChild.get());

    if (newChild->NodeType() == XmlNodeType::DocumentFragment) {
        if (ownerDocument_ != nullptr) {
            ownerDocument_->FireNodeRemoving(oldChild.get(), this);
        }
        oldChild->SetParent(nullptr);
        MutableChildNodes().erase(MutableChildNodes().begin() + static_cast<std::ptrdiff_t>(index));
        if (ownerDocument_ != nullptr) {
            ownerDocument_->FireNodeRemoved(oldChild.get(), this);
        }

        auto insertionIndex = index;
        while (newChild->FirstChild() != nullptr) {
            auto fragmentChild = newChild->RemoveChild(newChild->FirstChild());
            ValidateChildInsertion(fragmentChild);
            if (ownerDocument_ != nullptr) {
                ownerDocument_->FireNodeInserting(fragmentChild.get(), this);
            }
            fragmentChild->SetParent(this);
            fragmentChild->SetOwnerDocumentRecursive(ownerDocument_);
            MutableChildNodes().insert(MutableChildNodes().begin() + static_cast<std::ptrdiff_t>(insertionIndex), fragmentChild);
            if (ownerDocument_ != nullptr) {
                ownerDocument_->FireNodeInserted(fragmentChild.get(), this);
            }
            ++insertionIndex;
        }
        return oldChild;
    }

    if (ownerDocument_ != nullptr) {
        ownerDocument_->FireNodeRemoving(oldChild.get(), this);
        ownerDocument_->FireNodeInserting(newChild.get(), this);
    }
    oldChild->SetParent(nullptr);
    newChild->SetParent(this);
    newChild->SetOwnerDocumentRecursive(ownerDocument_);
    MutableChildNodes()[index] = newChild;
    if (ownerDocument_ != nullptr) {
        ownerDocument_->FireNodeRemoved(oldChild.get(), this);
        ownerDocument_->FireNodeInserted(newChild.get(), this);
    }
    return oldChild;
}

std::shared_ptr<XmlNode> XmlNode::RemoveChild(const std::shared_ptr<XmlNode>& child) {
    const auto index = FindChildIndexOrThrow(*this, child);
    auto removed = MutableChildNodes()[index];
    if (ownerDocument_ != nullptr) {
        ownerDocument_->FireNodeRemoving(removed.get(), this);
    }
    removed->SetParent(nullptr);
    MutableChildNodes().erase(MutableChildNodes().begin() + static_cast<std::ptrdiff_t>(index));
    if (ownerDocument_ != nullptr) {
        ownerDocument_->FireNodeRemoved(removed.get(), this);
    }
    return removed;
}

void XmlNode::RemoveAllChildren() {
    for (const auto& child : MutableChildNodes()) {
        child->SetParent(nullptr);
    }
    MutableChildNodes().clear();
}

void XmlNode::RemoveAll() {
    if (auto* element = dynamic_cast<XmlElement*>(this)) {
        element->RemoveAllAttributes();
    }
    RemoveAllChildren();
}

std::shared_ptr<XmlNode> XmlNode::SelectSingleNode(const std::string& xpath) const {
    const auto nodes = SelectNodes(xpath);
    return nodes.Item(0);
}

std::shared_ptr<XmlNode> XmlNode::SelectSingleNode(const std::string& xpath, const XmlNamespaceManager& namespaces) const {
    const auto nodes = SelectNodes(xpath, namespaces);
    return nodes.Item(0);
}

XmlNodeList XmlNode::SelectNodes(const std::string& xpath) const {
    if (NodeType() == XmlNodeType::Document) {
        return EvaluateXPathFromDocument(static_cast<const XmlDocument&>(*this), xpath);
    }
    if (NodeType() == XmlNodeType::Element) {
        return EvaluateXPathFromElement(static_cast<const XmlElement&>(*this), xpath);
    }
    // For other node types, evaluate from the parent element or document context
    if (ParentNode() != nullptr) {
        return ParentNode()->SelectNodes(xpath);
    }
    return XmlNodeList{};
}

XmlNodeList XmlNode::SelectNodes(const std::string& xpath, const XmlNamespaceManager& namespaces) const {
    if (NodeType() == XmlNodeType::Document) {
        return EvaluateXPathFromDocument(static_cast<const XmlDocument&>(*this), xpath, &namespaces);
    }
    if (NodeType() == XmlNodeType::Element) {
        return EvaluateXPathFromElement(static_cast<const XmlElement&>(*this), xpath, &namespaces);
    }
    if (ParentNode() != nullptr) {
        return ParentNode()->SelectNodes(xpath, namespaces);
    }
    return XmlNodeList{};
}

std::string XmlNode::InnerText() const {
    if (NodeType() == XmlNodeType::Text
        || NodeType() == XmlNodeType::EntityReference
        || NodeType() == XmlNodeType::CDATA
        || NodeType() == XmlNodeType::Whitespace
        || NodeType() == XmlNodeType::SignificantWhitespace
        || NodeType() == XmlNodeType::Comment
        || NodeType() == XmlNodeType::ProcessingInstruction
        || NodeType() == XmlNodeType::XmlDeclaration) {
        return Value();
    }

    std::string text;
    for (const auto& child : childNodes_) {
        if (child->NodeType() == XmlNodeType::Comment
            || child->NodeType() == XmlNodeType::ProcessingInstruction
            || child->NodeType() == XmlNodeType::XmlDeclaration
            || child->NodeType() == XmlNodeType::DocumentType) {
            continue;
        }
        text += child->InnerText();
    }
    return text;
}

void XmlNode::SetInnerText(const std::string& text) {
    switch (NodeType()) {
    case XmlNodeType::Document:
    case XmlNodeType::DocumentType:
    case XmlNodeType::DocumentFragment:
        throw XmlException("Cannot set InnerText on " + Name());
    case XmlNodeType::Text:
    case XmlNodeType::CDATA:
    case XmlNodeType::Comment:
    case XmlNodeType::Whitespace:
    case XmlNodeType::SignificantWhitespace:
    case XmlNodeType::ProcessingInstruction:
        SetValue(text);
        return;
    default:
        break;
    }
    RemoveAllChildren();
    if (!text.empty()) {
        auto ownerDoc = OwnerDocument();
        auto textNode = ownerDoc ? ownerDoc->CreateTextNode(text) : std::make_shared<XmlText>(text);
        AppendChild(textNode);
    }
}

std::string XmlNode::InnerXml(const XmlWriterSettings& settings) const {
    std::string xml;
    for (const auto& child : childNodes_) {
        xml += XmlWriter::WriteToString(*child, settings);
    }
    return xml;
}

void XmlNode::SetInnerXml(const std::string& xml) {
    (void)xml;
    throw XmlException("Setting InnerXml is not supported for " + Name());
}

std::string XmlNode::OuterXml(const XmlWriterSettings& settings) const {
    return XmlWriter::WriteToString(*this, settings);
}

std::shared_ptr<XmlNode> XmlNode::CloneNode(bool deep) const {
    std::shared_ptr<XmlNode> clone;

    switch (NodeType()) {
    case XmlNodeType::Document: {
        auto documentClone = std::make_shared<XmlDocument>();
        documentClone->SetPreserveWhitespace(static_cast<const XmlDocument*>(this)->PreserveWhitespace());
        if (deep) {
            for (const auto& child : ChildNodes()) {
                auto childClone = child->CloneNode(true);
                childClone->SetOwnerDocumentRecursive(documentClone.get());
                documentClone->AppendChild(childClone);
            }
        }
        return documentClone;
    }
    case XmlNodeType::Element: {
        auto elementClone = std::make_shared<XmlElement>(Name());
        const auto* sourceElement = static_cast<const XmlElement*>(this);
        elementClone->writeFullEndElement_ = sourceElement->writeFullEndElement_;
        for (const auto& attribute : sourceElement->Attributes()) {
            elementClone->SetAttribute(attribute->Name(), attribute->Value());
        }
        clone = elementClone;
        break;
    }
    case XmlNodeType::Attribute:
        clone = std::make_shared<XmlAttribute>(Name(), Value());
        break;
    case XmlNodeType::Text:
        clone = std::make_shared<XmlText>(Value());
        break;
    case XmlNodeType::EntityReference:
        clone = std::make_shared<XmlEntityReference>(Name(), Value());
        break;
    case XmlNodeType::Entity: {
        const auto* entity = static_cast<const XmlEntity*>(this);
        clone = std::make_shared<XmlEntity>(
            entity->Name(),
            entity->Value(),
            entity->PublicId(),
            entity->SystemId(),
            entity->NotationName());
        break;
    }
    case XmlNodeType::Notation: {
        const auto* notation = static_cast<const XmlNotation*>(this);
        clone = std::make_shared<XmlNotation>(notation->Name(), notation->PublicId(), notation->SystemId());
        break;
    }
    case XmlNodeType::Whitespace:
        clone = std::make_shared<XmlWhitespace>(Value());
        break;
    case XmlNodeType::SignificantWhitespace:
        clone = std::make_shared<XmlSignificantWhitespace>(Value());
        break;
    case XmlNodeType::CDATA:
        clone = std::make_shared<XmlCDataSection>(Value());
        break;
    case XmlNodeType::Comment:
        clone = std::make_shared<XmlComment>(Value());
        break;
    case XmlNodeType::ProcessingInstruction: {
        const auto* instruction = static_cast<const XmlProcessingInstruction*>(this);
        clone = std::make_shared<XmlProcessingInstruction>(instruction->Target(), instruction->Data());
        break;
    }
    case XmlNodeType::XmlDeclaration: {
        const auto* declaration = static_cast<const XmlDeclaration*>(this);
        clone = std::make_shared<XmlDeclaration>(declaration->Version(), declaration->Encoding(), declaration->Standalone());
        break;
    }
    case XmlNodeType::DocumentType: {
        const auto* documentType = static_cast<const XmlDocumentType*>(this);
        clone = std::make_shared<XmlDocumentType>(
            documentType->Name(),
            documentType->PublicId(),
            documentType->SystemId(),
            documentType->InternalSubset());
        break;
    }
    case XmlNodeType::DocumentFragment:
        clone = std::make_shared<XmlDocumentFragment>();
        break;
    default:
        throw XmlException("CloneNode does not support the specified node type");
    }

    if (ownerDocument_ != nullptr) {
        clone->SetOwnerDocumentRecursive(ownerDocument_);
    }

    if (deep && NodeType() != XmlNodeType::Attribute) {
        for (const auto& child : ChildNodes()) {
            clone->AppendChild(child->CloneNode(true));
        }
    }

    return clone;
}

XPathNavigator XmlNode::CreateNavigator() const {
    return XPathNavigator(this);
}

void XmlNode::SetOwnerDocument(XmlDocument* ownerDocument) noexcept {
    ownerDocument_ = ownerDocument;
}

void XmlNode::SetOwnerDocumentRecursive(XmlDocument* ownerDocument) {
    ownerDocument_ = ownerDocument;

    if (auto* element = dynamic_cast<XmlElement*>(this)) {
        for (const auto& attribute : element->attributes_) {
            attribute->SetOwnerDocumentRecursive(ownerDocument);
        }
    }

    if (auto* documentType = dynamic_cast<XmlDocumentType*>(this)) {
        for (const auto& entity : documentType->entities_) {
            entity->SetOwnerDocumentRecursive(ownerDocument);
        }
        for (const auto& notation : documentType->notations_) {
            notation->SetOwnerDocumentRecursive(ownerDocument);
        }
    }

    for (const auto& child : childNodes_) {
        child->SetOwnerDocumentRecursive(ownerDocument);
    }
}

void XmlNode::SetParent(XmlNode* parent) noexcept {
    parent_ = parent;
}

XmlAttribute::XmlAttribute(std::string name, std::string value)
    : XmlNode(XmlNodeType::Attribute, std::move(name), std::move(value)) {
}

XmlElement* XmlAttribute::OwnerElement() noexcept {
    return ParentNode() != nullptr && ParentNode()->NodeType() == XmlNodeType::Element
        ? static_cast<XmlElement*>(ParentNode())
        : nullptr;
}

const XmlElement* XmlAttribute::OwnerElement() const noexcept {
    return ParentNode() != nullptr && ParentNode()->NodeType() == XmlNodeType::Element
        ? static_cast<const XmlElement*>(ParentNode())
        : nullptr;
}

// XmlNode::Normalize — merge adjacent Text nodes recursively
void XmlNode::Normalize() {
    auto& children = MutableChildNodes();
    std::size_t i = 0;
    while (i < children.size()) {
        if (children[i]->NodeType() == XmlNodeType::Text) {
            // Accumulate consecutive text nodes
            std::string combined = children[i]->Value();
            std::size_t j = i + 1;
            while (j < children.size() && children[j]->NodeType() == XmlNodeType::Text) {
                combined += children[j]->Value();
                children[j]->SetParent(nullptr);
                ++j;
            }
            if (j > i + 1) {
                children[i]->SetValue(combined);
                children.erase(children.begin() + static_cast<std::ptrdiff_t>(i + 1),
                               children.begin() + static_cast<std::ptrdiff_t>(j));
            } else if (combined.empty()) {
                // Remove empty text node
                children[i]->SetParent(nullptr);
                children.erase(children.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
        } else {
            children[i]->Normalize();
        }
        ++i;
    }
}

// XmlCharacterData
XmlCharacterData::XmlCharacterData(XmlNodeType nodeType, std::string name, std::string value)
    : XmlNode(nodeType, std::move(name), std::move(value)) {
}

const std::string& XmlCharacterData::Data() const noexcept {
    return Value();
}

void XmlCharacterData::SetData(const std::string& data) {
    SetValue(data);
}

std::size_t XmlCharacterData::Length() const noexcept {
    return Value().size();
}

void XmlCharacterData::AppendData(const std::string& strData) {
    SetValue(Value() + strData);
}

void XmlCharacterData::DeleteData(std::size_t offset, std::size_t count) {
    const std::string& v = Value();
    if (offset > v.size()) {
        throw XmlException("DeleteData offset out of range");
    }
    std::string result = v.substr(0, offset);
    if (offset + count < v.size()) {
        result += v.substr(offset + count);
    }
    SetValue(result);
}

void XmlCharacterData::InsertData(std::size_t offset, const std::string& strData) {
    const std::string& v = Value();
    if (offset > v.size()) {
        throw XmlException("InsertData offset out of range");
    }
    SetValue(v.substr(0, offset) + strData + v.substr(offset));
}

void XmlCharacterData::ReplaceData(std::size_t offset, std::size_t count, const std::string& strData) {
    const std::string& v = Value();
    if (offset > v.size()) {
        throw XmlException("ReplaceData offset out of range");
    }
    std::string result = v.substr(0, offset) + strData;
    if (offset + count < v.size()) {
        result += v.substr(offset + count);
    }
    SetValue(result);
}

std::string XmlCharacterData::Substring(std::size_t offset, std::size_t count) const {
    const std::string& v = Value();
    if (offset > v.size()) {
        throw XmlException("Substring offset out of range");
    }
    return v.substr(offset, count);
}

XmlText::XmlText(std::string text) : XmlCharacterData(XmlNodeType::Text, "#text", std::move(text)) {
}

std::shared_ptr<XmlText> XmlText::SplitText(std::size_t offset) {
    const std::string& current = Value();
    if (offset > current.size()) {
        throw XmlException("SplitText offset exceeds text length");
    }
    std::string tail = current.substr(offset);
    SetValue(current.substr(0, offset));

    auto newNode = std::make_shared<XmlText>(std::move(tail));
    newNode->SetOwnerDocumentRecursive(OwnerDocument());
    if (ParentNode() != nullptr) {
        auto next = NextSibling();
        if (next != nullptr) {
            ParentNode()->InsertBefore(newNode, next);
        } else {
            ParentNode()->AppendChild(newNode);
        }
    }
    return newNode;
}

XmlEntityReference::XmlEntityReference(std::string name, std::string resolvedValue)
    : XmlNode(XmlNodeType::EntityReference, std::move(name), std::move(resolvedValue)) {
}

XmlEntity::XmlEntity(
    std::string name,
    std::string replacementText,
    std::string publicId,
    std::string systemId,
    std::string notationName)
    : XmlNode(XmlNodeType::Entity, std::move(name), std::move(replacementText)),
      publicId_(std::move(publicId)),
      systemId_(std::move(systemId)),
      notationName_(std::move(notationName)) {
}

const std::string& XmlEntity::PublicId() const noexcept {
    return publicId_;
}

const std::string& XmlEntity::SystemId() const noexcept {
    return systemId_;
}

const std::string& XmlEntity::NotationName() const noexcept {
    return notationName_;
}

XmlNotation::XmlNotation(std::string name, std::string publicId, std::string systemId)
    : XmlNode(XmlNodeType::Notation, std::move(name)),
      publicId_(std::move(publicId)),
      systemId_(std::move(systemId)) {
}

const std::string& XmlNotation::PublicId() const noexcept {
    return publicId_;
}

const std::string& XmlNotation::SystemId() const noexcept {
    return systemId_;
}

XmlWhitespace::XmlWhitespace(std::string whitespace)
    : XmlCharacterData(XmlNodeType::Whitespace, "#whitespace", std::move(whitespace)) {
    ValidateWhitespaceValue(Value(), "Whitespace");
}

void XmlWhitespace::SetValue(const std::string& value) {
    ValidateWhitespaceValue(value, "Whitespace");
    XmlNode::SetValue(value);
}

XmlSignificantWhitespace::XmlSignificantWhitespace(std::string whitespace)
    : XmlCharacterData(XmlNodeType::SignificantWhitespace, "#significant-whitespace", std::move(whitespace)) {
    ValidateWhitespaceValue(Value(), "SignificantWhitespace");
}

void XmlSignificantWhitespace::SetValue(const std::string& value) {
    ValidateWhitespaceValue(value, "SignificantWhitespace");
    XmlNode::SetValue(value);
}

XmlCDataSection::XmlCDataSection(std::string data) : XmlCharacterData(XmlNodeType::CDATA, "#cdata-section", std::move(data)) {
}

XmlComment::XmlComment(std::string comment) : XmlCharacterData(XmlNodeType::Comment, "#comment", std::move(comment)) {
}

XmlProcessingInstruction::XmlProcessingInstruction(std::string target, std::string data)
    : XmlNode(XmlNodeType::ProcessingInstruction, std::move(target), std::move(data)) {
}

const std::string& XmlProcessingInstruction::Target() const noexcept {
    return Name();
}

const std::string& XmlProcessingInstruction::Data() const noexcept {
    return Value();
}

void XmlProcessingInstruction::SetData(const std::string& data) {
    SetValue(data);
}

XmlDeclaration::XmlDeclaration(std::string version, std::string encoding, std::string standalone)
        : XmlNode(XmlNodeType::XmlDeclaration, "xml", BuildDeclarationValue(version, encoding, standalone)),
      version_(std::move(version)),
      encoding_(std::move(encoding)),
            standalone_(std::move(standalone)) {
}

const std::string& XmlDeclaration::Version() const noexcept {
    return version_;
}

const std::string& XmlDeclaration::Encoding() const noexcept {
    return encoding_;
}

const std::string& XmlDeclaration::Standalone() const noexcept {
    return standalone_;
}

XmlDocumentType::XmlDocumentType(
    std::string name,
    std::string publicId,
    std::string systemId,
    std::string internalSubset)
    : XmlNode(XmlNodeType::DocumentType, std::move(name)),
      publicId_(std::move(publicId)),
      systemId_(std::move(systemId)),
      internalSubset_(std::move(internalSubset)) {
    ParseDocumentTypeInternalSubset(internalSubset_, entities_, notations_);
}

const std::string& XmlDocumentType::PublicId() const noexcept {
    return publicId_;
}

const std::string& XmlDocumentType::SystemId() const noexcept {
    return systemId_;
}

const std::string& XmlDocumentType::InternalSubset() const noexcept {
    return internalSubset_;
}

XmlNamedNodeMap XmlDocumentType::Entities() const {
    return XmlNamedNodeMap(entities_);
}

XmlNamedNodeMap XmlDocumentType::Notations() const {
    return XmlNamedNodeMap(notations_);
}

XmlDocumentFragment::XmlDocumentFragment() : XmlNode(XmlNodeType::DocumentFragment, "#document-fragment") {
}

void XmlDocumentFragment::SetInnerXml(const std::string& xml) {
    auto* ownerDocument = OwnerDocument();
    if (ownerDocument == nullptr) {
        throw XmlException("Document fragment must belong to an owner document before parsing XML");
    }

    XmlDocument scratch;
    scratch.SetPreserveWhitespace(ownerDocument->PreserveWhitespace());
    scratch.LoadXml("<__fragment_root__>" + xml + "</__fragment_root__>");

    RemoveAllChildren();

    const auto wrapper = scratch.DocumentElement();
    while (wrapper != nullptr && wrapper->FirstChild() != nullptr) {
        auto child = wrapper->RemoveChild(wrapper->FirstChild());
        AppendChild(ownerDocument->ImportNode(*child, true));
    }
}

XmlElement::XmlElement(std::string name) : XmlNode(XmlNodeType::Element, std::move(name)) {
}

std::string_view XmlElement::PendingLoadAttributeNameView(const PendingLoadAttribute& attribute) const noexcept {
    return std::string_view(pendingLoadAttributeStorage_).substr(attribute.nameOffset, attribute.nameLength);
}

std::string_view XmlElement::PendingLoadAttributeValueView(const PendingLoadAttribute& attribute) const noexcept {
    return std::string_view(pendingLoadAttributeStorage_).substr(attribute.valueOffset, attribute.valueLength);
}

bool XmlElement::TryFindNamespaceDeclarationValueView(std::string_view prefix, std::string_view& value) const noexcept {
    for (const auto& attribute : attributes_) {
        const std::string_view attributeName(attribute->Name());
        if (!IsNamespaceDeclarationName(attributeName)) {
            continue;
        }
        if (NamespaceDeclarationPrefixView(attributeName) == prefix) {
            value = attribute->Value();
            return true;
        }
    }

    for (const auto& pendingAttribute : pendingLoadAttributes_) {
        const std::string_view pendingName = PendingLoadAttributeNameView(pendingAttribute);
        if (!IsNamespaceDeclarationName(pendingName)) {
            continue;
        }
        if (NamespaceDeclarationPrefixView(pendingName) == prefix) {
            value = PendingLoadAttributeValueView(pendingAttribute);
            return true;
        }
    }

    return false;
}

bool TryGetAttributeValueViewInternal(const XmlElement& element, std::string_view name, std::string_view& value) noexcept {
    for (const auto& attribute : element.attributes_) {
        if (attribute->Name() == name) {
            value = attribute->Value();
            return true;
        }
    }

    for (const auto& pendingAttribute : element.pendingLoadAttributes_) {
        if (element.PendingLoadAttributeNameView(pendingAttribute) == name) {
            value = element.PendingLoadAttributeValueView(pendingAttribute);
            return true;
        }
    }

    value = {};
    return false;
}

bool AttributeValueEqualsInternal(const XmlElement& element, std::string_view name, std::string_view expectedValue) noexcept {
    std::string_view value;
    return TryGetAttributeValueViewInternal(element, name, value) && value == expectedValue;
}

std::shared_ptr<XmlAttribute> XmlElement::MaterializePendingLoadAttribute(std::size_t index) const {
    if (index >= pendingLoadAttributes_.size()) {
        return nullptr;
    }

    const auto pendingAttribute = pendingLoadAttributes_[index];
    auto attribute = std::make_shared<XmlAttribute>(
        std::string(PendingLoadAttributeNameView(pendingAttribute)),
        std::string(PendingLoadAttributeValueView(pendingAttribute)));
    attribute->SetParent(const_cast<XmlElement*>(this));
    attribute->SetOwnerDocument(const_cast<XmlDocument*>(OwnerDocument()));
    attributes_.push_back(attribute);
    pendingLoadAttributes_.erase(pendingLoadAttributes_.begin() + static_cast<std::ptrdiff_t>(index));
    return attribute;
}

std::size_t XmlElement::FindPendingLoadAttributeIndex(const std::string& name) const noexcept {
    for (std::size_t index = 0; index < pendingLoadAttributes_.size(); ++index) {
        if (PendingLoadAttributeNameView(pendingLoadAttributes_[index]) == name) {
            return index;
        }
    }
    return std::string::npos;
}

std::size_t XmlElement::FindPendingLoadAttributeIndex(const std::string& localName, const std::string& namespaceUri) const {
    for (std::size_t index = 0; index < pendingLoadAttributes_.size(); ++index) {
        const auto& pendingAttribute = pendingLoadAttributes_[index];
        const auto [prefix, pendingLocalName] = SplitQualifiedNameView(PendingLoadAttributeNameView(pendingAttribute));
        if (pendingLocalName != localName) {
            continue;
        }
        if (prefix.empty()) {
            if (namespaceUri.empty()) {
                return index;
            }
            continue;
        }
        if (LookupNamespaceUriOnElement(this, prefix) == namespaceUri) {
            return index;
        }
    }
    return std::string::npos;
}

std::string XmlElement::FindNamespaceDeclarationValue(const std::string& prefix) const {
    std::string_view value;
    return TryFindNamespaceDeclarationValueView(prefix, value) ? std::string(value) : std::string{};
}

bool XmlElement::HasNamespaceDeclaration(const std::string& prefix) const {
    std::string_view value;
    return TryFindNamespaceDeclarationValueView(prefix, value);
}

std::string XmlElement::FindNamespaceDeclarationPrefix(const std::string& namespaceUri) const {
    for (const auto& attribute : attributes_) {
        const std::string_view attributeName(attribute->Name());
        if (!IsNamespaceDeclarationName(attributeName)) {
            continue;
        }
        if (attribute->Value() == namespaceUri) {
            return std::string(NamespaceDeclarationPrefixView(attributeName));
        }
    }

    for (const auto& pendingAttribute : pendingLoadAttributes_) {
        const std::string_view pendingName = PendingLoadAttributeNameView(pendingAttribute);
        if (!IsNamespaceDeclarationName(pendingName)) {
            continue;
        }
        if (PendingLoadAttributeValueView(pendingAttribute) == namespaceUri) {
            return std::string(NamespaceDeclarationPrefixView(pendingName));
        }
    }

    return {};
}

void XmlElement::EnsureAttributesMaterialized() const {
    if (pendingLoadAttributes_.empty()) {
        return;
    }

    attributes_.reserve(attributes_.size() + pendingLoadAttributes_.size());
    while (!pendingLoadAttributes_.empty()) {
        MaterializePendingLoadAttribute(pendingLoadAttributes_.size() - 1);
    }
}

const std::vector<std::shared_ptr<XmlAttribute>>& XmlElement::Attributes() const {
    EnsureAttributesMaterialized();
    return attributes_;
}

XmlAttributeCollection XmlElement::AttributeNodes() const {
    EnsureAttributesMaterialized();
    return XmlAttributeCollection(attributes_);
}

bool XmlElement::HasAttributes() const noexcept {
    return !attributes_.empty() || !pendingLoadAttributes_.empty();
}

bool XmlElement::HasAttribute(const std::string& name) const {
    std::string_view value;
    return TryGetAttributeValueViewInternal(*this, name, value);
}

bool XmlElement::HasAttribute(const std::string& localName, const std::string& namespaceUri) const {
    const auto found = std::find_if(attributes_.begin(), attributes_.end(), [&](const auto& attribute) {
        const auto [prefix, attributeLocalName] = SplitQualifiedNameView(attribute->Name());
        if (attributeLocalName != localName) {
            return false;
        }
        if (prefix.empty()) {
            return namespaceUri.empty();
        }
        return LookupNamespaceUriOnElement(this, prefix) == namespaceUri;
    });
    return found != attributes_.end() || FindPendingLoadAttributeIndex(localName, namespaceUri) != std::string::npos;
}

std::string XmlElement::GetAttribute(const std::string& name) const {
    std::string_view value;
    return TryGetAttributeValueViewInternal(*this, name, value) ? std::string(value) : std::string{};
}

std::string XmlElement::GetAttribute(const std::string& localName, const std::string& namespaceUri) const {
    const auto found = std::find_if(attributes_.begin(), attributes_.end(), [&](const auto& attribute) {
        const auto [prefix, attributeLocalName] = SplitQualifiedNameView(attribute->Name());
        if (attributeLocalName != localName) {
            return false;
        }
        if (prefix.empty()) {
            return namespaceUri.empty();
        }
        return LookupNamespaceUriOnElement(this, prefix) == namespaceUri;
    });
    if (found != attributes_.end()) {
        return (*found)->Value();
    }

    const auto pendingIndex = FindPendingLoadAttributeIndex(localName, namespaceUri);
    return pendingIndex == std::string::npos
        ? std::string{}
        : std::string(PendingLoadAttributeValueView(pendingLoadAttributes_[pendingIndex]));
}

std::shared_ptr<XmlAttribute> XmlElement::GetAttributeNode(const std::string& name) const {
    const auto found = std::find_if(attributes_.begin(), attributes_.end(), [&name](const auto& attribute) {
        return attribute->Name() == name;
    });

    if (found != attributes_.end()) {
        return *found;
    }

    const auto pendingIndex = FindPendingLoadAttributeIndex(name);
    if (pendingIndex != std::string::npos) {
        return MaterializePendingLoadAttribute(pendingIndex);
    }

    return nullptr;
}

std::shared_ptr<XmlAttribute> XmlElement::GetAttributeNode(const std::string& localName, const std::string& namespaceUri) const {
    const auto found = std::find_if(attributes_.begin(), attributes_.end(), [&](const auto& attribute) {
        const auto [prefix, attributeLocalName] = SplitQualifiedNameView(attribute->Name());
        if (attributeLocalName != localName) {
            return false;
        }
        if (prefix.empty()) {
            return namespaceUri.empty();
        }
        return LookupNamespaceUriOnElement(this, prefix) == namespaceUri;
    });

    if (found != attributes_.end()) {
        return *found;
    }

    const auto pendingIndex = FindPendingLoadAttributeIndex(localName, namespaceUri);
    if (pendingIndex != std::string::npos) {
        return MaterializePendingLoadAttribute(pendingIndex);
    }

    return nullptr;
}

std::shared_ptr<XmlAttribute> XmlElement::SetAttribute(const std::string& name, const std::string& value) {
    if (auto attribute = GetAttributeNode(name)) {
        attribute->SetValue(value);
        return attribute;
    }

    auto attribute = std::make_shared<XmlAttribute>(name, value);
    attribute->SetParent(this);
    attribute->SetOwnerDocumentRecursive(OwnerDocument());
    attributes_.push_back(attribute);
    return attribute;
}

void XmlElement::ReserveAttributesForLoad(std::size_t count, std::size_t totalStorageBytes) {
    pendingLoadAttributes_.reserve(count);
    if (totalStorageBytes != 0) {
        pendingLoadAttributeStorage_.reserve(pendingLoadAttributeStorage_.size() + totalStorageBytes);
    }
}

void XmlElement::AppendAttributeForLoad(std::string name, std::string value) {
    const std::size_t nameOffset = pendingLoadAttributeStorage_.size();
    pendingLoadAttributeStorage_ += name;
    const std::size_t valueOffset = pendingLoadAttributeStorage_.size();
    pendingLoadAttributeStorage_ += value;
    pendingLoadAttributes_.push_back(PendingLoadAttribute{
        nameOffset,
        name.size(),
        valueOffset,
        value.size()});
}

void XmlElement::SetAttribute(const std::string& localName, const std::string& namespaceUri, const std::string& value) {
    // Find existing attribute with matching localName+ns, or create prefix:localName
    if (auto existing = GetAttributeNode(localName, namespaceUri)) {
        existing->SetValue(value);
        return;
    }
    if (namespaceUri.empty()) {
        SetAttribute(localName, value);
        return;
    }
    // Search for existing prefix bound to namespaceUri on this element/ancestors.
    std::string existingPrefix = FindNamespaceDeclarationPrefix(namespaceUri);
    if (existingPrefix.empty()) {
        const XmlNode* p = ParentNode();
        while (p != nullptr && p->NodeType() == XmlNodeType::Element && existingPrefix.empty()) {
            existingPrefix = static_cast<const XmlElement*>(p)->FindNamespaceDeclarationPrefix(namespaceUri);
            p = p->ParentNode();
        }
    }
    if (existingPrefix.empty()) {
        // Generate a prefix
        std::size_t prefixIndex = attributes_.size() + pendingLoadAttributes_.size();
        do {
            existingPrefix = "ns" + std::to_string(prefixIndex++);
        } while (HasNamespaceDeclaration(existingPrefix));
        SetAttribute("xmlns:" + existingPrefix, namespaceUri);
    }
    SetAttribute(existingPrefix + ":" + localName, value);
}

std::shared_ptr<XmlAttribute> XmlElement::SetAttributeNode(const std::shared_ptr<XmlAttribute>& attribute) {
    EnsureAttributesMaterialized();
    if (!attribute) {
        throw XmlException("Cannot attach a null attribute node");
    }

    if (attribute->OwnerDocument() != nullptr && OwnerDocument() != nullptr && attribute->OwnerDocument() != OwnerDocument()) {
        throw XmlException("The attribute belongs to a different document");
    }

    if (attribute->OwnerElement() != nullptr && attribute->OwnerElement() != this) {
        throw XmlException("The attribute already belongs to another element");
    }

    const auto found = std::find_if(attributes_.begin(), attributes_.end(), [&attribute](const auto& current) {
        return current->Name() == attribute->Name();
    });

    if (found != attributes_.end() && *found == attribute) {
        return attribute;
    }

    std::shared_ptr<XmlAttribute> replaced;
    if (found != attributes_.end()) {
        replaced = *found;
        replaced->SetParent(nullptr);
        *found = attribute;
    } else {
        attributes_.push_back(attribute);
    }

    attribute->SetParent(this);
    attribute->SetOwnerDocumentRecursive(OwnerDocument());
    return replaced;
}

void XmlElement::SetInnerXml(const std::string& xml) {
    XmlDocument scratch;
    if (OwnerDocument() != nullptr) {
        scratch.SetPreserveWhitespace(OwnerDocument()->PreserveWhitespace());
    }
    scratch.LoadXml("<__element_inner_xml__>" + xml + "</__element_inner_xml__>");

    RemoveAllChildren();

    const auto wrapper = scratch.DocumentElement();
    while (wrapper != nullptr && wrapper->FirstChild() != nullptr) {
        auto child = wrapper->RemoveChild(wrapper->FirstChild());
        if (OwnerDocument() != nullptr) {
            AppendChild(OwnerDocument()->ImportNode(*child, true));
        } else {
            AppendChild(child);
        }
    }
}

bool XmlElement::RemoveAttribute(const std::string& name) {
    if (const auto pendingIndex = FindPendingLoadAttributeIndex(name); pendingIndex != std::string::npos) {
        pendingLoadAttributes_.erase(pendingLoadAttributes_.begin() + static_cast<std::ptrdiff_t>(pendingIndex));
        return true;
    }

    const auto found = std::find_if(attributes_.begin(), attributes_.end(), [&name](const auto& attribute) {
        return attribute->Name() == name;
    });
    if (found == attributes_.end()) {
        return false;
    }

    (*found)->SetParent(nullptr);
    attributes_.erase(found);
    return true;
}

bool XmlElement::RemoveAttribute(const std::string& localName, const std::string& namespaceUri) {
    if (const auto pendingIndex = FindPendingLoadAttributeIndex(localName, namespaceUri); pendingIndex != std::string::npos) {
        pendingLoadAttributes_.erase(pendingLoadAttributes_.begin() + static_cast<std::ptrdiff_t>(pendingIndex));
        return true;
    }

    const auto found = std::find_if(attributes_.begin(), attributes_.end(), [&](const auto& attribute) {
        const auto [prefix, attributeLocalName] = SplitQualifiedNameView(attribute->Name());
        if (attributeLocalName != localName) {
            return false;
        }
        if (prefix.empty()) {
            return namespaceUri.empty();
        }
        return LookupNamespaceUriOnElement(this, prefix) == namespaceUri;
    });
    if (found == attributes_.end()) {
        return false;
    }

    (*found)->SetParent(nullptr);
    attributes_.erase(found);
    return true;
}

std::shared_ptr<XmlAttribute> XmlElement::RemoveAttributeNode(const std::shared_ptr<XmlAttribute>& attribute) {
    EnsureAttributesMaterialized();
    if (!attribute) {
        return nullptr;
    }

    const auto found = std::find(attributes_.begin(), attributes_.end(), attribute);
    if (found == attributes_.end()) {
        return nullptr;
    }

    auto removed = *found;
    removed->SetParent(nullptr);
    attributes_.erase(found);
    return removed;
}

void XmlElement::RemoveAllAttributes() {
    pendingLoadAttributes_.clear();
    for (const auto& attribute : attributes_) {
        attribute->SetParent(nullptr);
    }
    attributes_.clear();
}

bool XmlElement::IsEmpty() const noexcept {
    return !HasChildNodes() && !writeFullEndElement_;
}

std::vector<std::shared_ptr<XmlElement>> XmlElement::GetElementsByTagName(const std::string& name) const {
    std::vector<std::shared_ptr<XmlElement>> results;
    CollectElementsByName(*this, name, results);
    return results;
}

XmlNodeList XmlElement::GetElementsByTagNameList(const std::string& name) const {
    std::vector<std::shared_ptr<XmlNode>> nodes;
    for (const auto& element : GetElementsByTagName(name)) {
        nodes.push_back(element);
    }
    return XmlNodeList(std::move(nodes));
}

std::vector<std::shared_ptr<XmlElement>> XmlElement::GetElementsByTagName(const std::string& localName, const std::string& namespaceUri) const {
    std::vector<std::shared_ptr<XmlElement>> results;
    CollectElementsByNameNS(*this, localName, namespaceUri, results);
    return results;
}

XmlNodeList XmlElement::GetElementsByTagNameList(const std::string& localName, const std::string& namespaceUri) const {
    std::vector<std::shared_ptr<XmlNode>> nodes;
    for (const auto& element : GetElementsByTagName(localName, namespaceUri)) {
        nodes.push_back(element);
    }
    return XmlNodeList(std::move(nodes));
}

bool XmlElement::WritesFullEndElement() const noexcept {
    return writeFullEndElement_;
}

XmlNodeReader XmlElement::CreateReader() const {
    return XmlNodeReader(*this);
}

XmlDocument::XmlDocument() : XmlNode(XmlNodeType::Document, "#document") {
    SetOwnerDocumentRecursive(this);
}

std::shared_ptr<XmlDocument> XmlDocument::Parse(const std::string& xml) {
    auto document = std::make_shared<XmlDocument>();
    document->LoadXml(xml);
    return document;
}

void XmlDocument::LoadXml(const std::string& xml) {
    XmlParser(xml).ParseInto(*this);
}

void XmlDocument::Load(const std::string& path) {
    auto sourceText = std::make_shared<std::string>(ReadFileFully(path));
    ValidateXmlReaderInputAgainstSchemas(*sourceText, XmlReaderSettings{});
    XmlReader reader = XmlReader::CreateFromValidatedString(sourceText, XmlReaderSettings{});
    reader.baseUri_ = std::filesystem::absolute(std::filesystem::path(path)).string();
    LoadXmlDocumentFromReader(reader, *this);
}

void XmlDocument::Load(std::istream& stream) {
    auto reader = XmlReader::Create(stream);
    LoadXmlDocumentFromReader(reader, *this);
}

void XmlDocument::Save(const std::string& path, const XmlWriterSettings& settings) const {
    XmlWriter::WriteToFile(*this, path, settings);
}

void XmlDocument::Save(std::ostream& stream, const XmlWriterSettings& settings) const {
    XmlWriter::WriteToStream(*this, stream, settings);
}

std::string NormalizeSchemaSimpleTypeValue(const XmlSchemaSet::SimpleTypeRule& type, const std::string& lexicalValue) {
    if (!type.whiteSpace.has_value() || *type.whiteSpace == "preserve") {
        return lexicalValue;
    }

    std::string normalized;
    normalized.reserve(lexicalValue.size());
    if (*type.whiteSpace == "replace") {
        for (const char ch : lexicalValue) {
            if (IsWhitespace(ch)) {
                normalized.push_back(' ');
            } else {
                normalized.push_back(ch);
            }
        }
        return normalized;
    }

    if (*type.whiteSpace == "collapse") {
        bool inWhitespace = true;
        for (const char ch : lexicalValue) {
            if (IsWhitespace(ch)) {
                inWhitespace = true;
                continue;
            }
            if (!normalized.empty() && inWhitespace) {
                normalized.push_back(' ');
            }
            normalized.push_back(ch);
            inWhitespace = false;
        }
        return normalized;
    }

    throw XmlException("Schema validation failed: unsupported whiteSpace facet '" + *type.whiteSpace + "'");
}

std::string LookupNamespaceUriInBindings(
    const std::vector<std::pair<std::string, std::string>>& namespaceBindings,
    const std::string& prefix) {
    for (auto it = namespaceBindings.rbegin(); it != namespaceBindings.rend(); ++it) {
        if (it->first == prefix) {
            return it->second;
        }
    }
    return {};
}

void ValidateSchemaSimpleValueWithQNameResolver(
    const XmlSchemaSet::SimpleTypeRule& type,
    const std::string& value,
    const std::string& label,
    const std::function<std::string(const std::string&)>& resolveNamespaceUri,
    const std::function<bool(const std::string&)>& hasNotationDeclaration,
    const std::function<bool(const std::string&)>& hasUnparsedEntityDeclaration) {
    const auto isPracticalLanguageValue = [](const std::string& lexicalValue) {
        if (lexicalValue.empty()) {
            return false;
        }

        std::size_t segmentLength = 0;
        bool expectLetterOnly = true;
        for (const char ch : lexicalValue) {
            if (ch == '-') {
                if (segmentLength == 0) {
                    return false;
                }
                segmentLength = 0;
                expectLetterOnly = false;
                continue;
            }

            const unsigned char current = static_cast<unsigned char>(ch);
            if (segmentLength >= 8) {
                return false;
            }
            if (expectLetterOnly) {
                if (!std::isalpha(current)) {
                    return false;
                }
            } else if (!std::isalnum(current)) {
                return false;
            }
            ++segmentLength;
        }

        return segmentLength > 0;
    };

    const auto isPracticalAnyUriValue = [&](const std::string& lexicalValue) {
        for (const char ch : lexicalValue) {
            const unsigned char current = static_cast<unsigned char>(ch);
            if (IsWhitespace(ch) || current < 0x20) {
                return false;
            }
        }
        return true;
    };

    const auto decodePracticalBinaryValue = [&](const std::string& baseType, const std::string& lexicalValue) {
        std::vector<unsigned char> decoded;
        bool success = false;
        if (baseType == "xs:hexBinary") {
            success = TryDecodeHexBinary(lexicalValue, decoded);
        } else if (baseType == "xs:base64Binary") {
            success = TryDecodeBase64Binary(lexicalValue, decoded);
        }
        if (!success) {
            throw XmlException("invalid binary lexical form");
        }
        return decoded;
    };

    struct PracticalTemporalValue {
        long long primary = 0;
        int secondary = 0;
        int fractionalNanoseconds = 0;
    };

    const auto isLeapYear = [](const int year) {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    };

    const auto daysInMonth = [&](const int year, const int month) {
        static const int kDaysInMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        if (month < 1 || month > 12) {
            return 0;
        }
        if (month == 2 && isLeapYear(year)) {
            return 29;
        }
        return kDaysInMonth[month - 1];
    };

    const auto daysFromCivil = [](int year, const unsigned month, const unsigned day) -> long long {
        year -= month <= 2 ? 1 : 0;
        const long long era = static_cast<long long>(year >= 0 ? year : year - 399) / 400LL;
        const unsigned yoe = static_cast<unsigned>(year - static_cast<int>(era * 400LL));
        const unsigned shiftedMonth = month > 2 ? month - 3 : month + 9;
        const unsigned doy = (153 * shiftedMonth + 2) / 5 + day - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097LL + static_cast<long long>(doe) - 719468LL;
    };

    const auto compareTemporalValues = [](const PracticalTemporalValue& left, const PracticalTemporalValue& right) {
        if (left.primary != right.primary) {
            return left.primary < right.primary ? -1 : 1;
        }
        if (left.secondary != right.secondary) {
            return left.secondary < right.secondary ? -1 : 1;
        }
        if (left.fractionalNanoseconds != right.fractionalNanoseconds) {
            return left.fractionalNanoseconds < right.fractionalNanoseconds ? -1 : 1;
        }
        return 0;
    };

    const auto normalizeTemporalValue = [&](const std::string& baseType, const std::string& lexicalValue) -> PracticalTemporalValue {
        const auto parseFixedWidthNumber = [&](const std::string& text, const std::size_t start, const std::size_t length) {
            if (start + length > text.size()) {
                throw XmlException("invalid temporal lexical form");
            }
            int value = 0;
            for (std::size_t index = 0; index < length; ++index) {
                const char ch = text[start + index];
                if (!std::isdigit(static_cast<unsigned char>(ch))) {
                    throw XmlException("invalid temporal lexical form");
                }
                value = value * 10 + (ch - '0');
            }
            return value;
        };

        const auto parseTimezoneOffsetMinutes = [&](const std::string& text, const std::size_t start) {
            if (start >= text.size()) {
                return 0;
            }
            if (text[start] == 'Z') {
                if (start + 1 != text.size()) {
                    throw XmlException("invalid temporal lexical form");
                }
                return 0;
            }
            if ((text[start] != '+' && text[start] != '-') || start + 6 != text.size() || text[start + 3] != ':') {
                throw XmlException("invalid temporal lexical form");
            }
            const int hours = parseFixedWidthNumber(text, start + 1, 2);
            const int minutes = parseFixedWidthNumber(text, start + 4, 2);
            if (hours > 14 || minutes > 59 || (hours == 14 && minutes != 0)) {
                throw XmlException("invalid temporal lexical form");
            }
            const int totalMinutes = hours * 60 + minutes;
            return text[start] == '-' ? -totalMinutes : totalMinutes;
        };

        const auto parseFractionalNanoseconds = [&](const std::string& digits) {
            if (digits.empty()) {
                throw XmlException("invalid temporal lexical form");
            }
            int value = 0;
            for (std::size_t index = 0; index < 9; ++index) {
                value *= 10;
                if (index < digits.size()) {
                    const char ch = digits[index];
                    if (!std::isdigit(static_cast<unsigned char>(ch))) {
                        throw XmlException("invalid temporal lexical form");
                    }
                    value += ch - '0';
                }
            }
            for (std::size_t index = 9; index < digits.size(); ++index) {
                if (!std::isdigit(static_cast<unsigned char>(digits[index]))) {
                    throw XmlException("invalid temporal lexical form");
                }
            }
            return value;
        };

        auto normalizeDateTimeComponents = [](long long& dayValue, int& secondsOfDay, const int offsetMinutes) {
            secondsOfDay -= offsetMinutes * 60;
            while (secondsOfDay < 0) {
                secondsOfDay += 24 * 60 * 60;
                --dayValue;
            }
            while (secondsOfDay >= 24 * 60 * 60) {
                secondsOfDay -= 24 * 60 * 60;
                ++dayValue;
            }
        };

        if (baseType == "xs:date") {
            if (lexicalValue.size() < 10 || lexicalValue[4] != '-' || lexicalValue[7] != '-') {
                throw XmlException("invalid temporal lexical form");
            }
            const int year = parseFixedWidthNumber(lexicalValue, 0, 4);
            const int month = parseFixedWidthNumber(lexicalValue, 5, 2);
            const int day = parseFixedWidthNumber(lexicalValue, 8, 2);
            if (day < 1 || day > daysInMonth(year, month)) {
                throw XmlException("invalid temporal lexical form");
            }

            const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, 10);
            long long absoluteDay = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
            int secondsOfDay = 0;
            normalizeDateTimeComponents(absoluteDay, secondsOfDay, timezoneOffsetMinutes);
            return PracticalTemporalValue{ absoluteDay, secondsOfDay, 0 };
        }

        if (baseType == "xs:gYear") {
            if (lexicalValue.size() < 4) {
                throw XmlException("invalid temporal lexical form");
            }
            const int year = parseFixedWidthNumber(lexicalValue, 0, 4);
            const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, 4);
            long long absoluteDay = daysFromCivil(year, 1, 1);
            int secondsOfDay = 0;
            normalizeDateTimeComponents(absoluteDay, secondsOfDay, timezoneOffsetMinutes);
            return PracticalTemporalValue{ absoluteDay, secondsOfDay, 0 };
        }

        if (baseType == "xs:gYearMonth") {
            if (lexicalValue.size() < 7 || lexicalValue[4] != '-') {
                throw XmlException("invalid temporal lexical form");
            }
            const int year = parseFixedWidthNumber(lexicalValue, 0, 4);
            const int month = parseFixedWidthNumber(lexicalValue, 5, 2);
            if (month < 1 || month > 12) {
                throw XmlException("invalid temporal lexical form");
            }

            const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, 7);
            long long absoluteDay = daysFromCivil(year, static_cast<unsigned>(month), 1);
            int secondsOfDay = 0;
            normalizeDateTimeComponents(absoluteDay, secondsOfDay, timezoneOffsetMinutes);
            return PracticalTemporalValue{ absoluteDay, secondsOfDay, 0 };
        }

        if (baseType == "xs:gMonth") {
            if (lexicalValue.size() < 4 || lexicalValue[0] != '-' || lexicalValue[1] != '-') {
                throw XmlException("invalid temporal lexical form");
            }
            const int month = parseFixedWidthNumber(lexicalValue, 2, 2);
            if (month < 1 || month > 12) {
                throw XmlException("invalid temporal lexical form");
            }
            const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, 4);
            long long absoluteDay = daysFromCivil(2000, static_cast<unsigned>(month), 1);
            int secondsOfDay = 0;
            normalizeDateTimeComponents(absoluteDay, secondsOfDay, timezoneOffsetMinutes);
            return PracticalTemporalValue{ absoluteDay, secondsOfDay, 0 };
        }

        if (baseType == "xs:gDay") {
            if (lexicalValue.size() < 5 || lexicalValue[0] != '-' || lexicalValue[1] != '-' || lexicalValue[2] != '-') {
                throw XmlException("invalid temporal lexical form");
            }
            const int day = parseFixedWidthNumber(lexicalValue, 3, 2);
            if (day < 1 || day > 31) {
                throw XmlException("invalid temporal lexical form");
            }
            const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, 5);
            long long absoluteDay = daysFromCivil(2000, 1, static_cast<unsigned>(day));
            int secondsOfDay = 0;
            normalizeDateTimeComponents(absoluteDay, secondsOfDay, timezoneOffsetMinutes);
            return PracticalTemporalValue{ absoluteDay, secondsOfDay, 0 };
        }

        if (baseType == "xs:gMonthDay") {
            if (lexicalValue.size() < 7 || lexicalValue[0] != '-' || lexicalValue[1] != '-' || lexicalValue[4] != '-') {
                throw XmlException("invalid temporal lexical form");
            }
            const int month = parseFixedWidthNumber(lexicalValue, 2, 2);
            const int day = parseFixedWidthNumber(lexicalValue, 5, 2);
            if (day < 1 || day > daysInMonth(2000, month)) {
                throw XmlException("invalid temporal lexical form");
            }
            const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, 7);
            long long absoluteDay = daysFromCivil(2000, static_cast<unsigned>(month), static_cast<unsigned>(day));
            int secondsOfDay = 0;
            normalizeDateTimeComponents(absoluteDay, secondsOfDay, timezoneOffsetMinutes);
            return PracticalTemporalValue{ absoluteDay, secondsOfDay, 0 };
        }

        if (baseType == "xs:time") {
            if (lexicalValue.size() < 8 || lexicalValue[2] != ':' || lexicalValue[5] != ':') {
                throw XmlException("invalid temporal lexical form");
            }
            const int hour = parseFixedWidthNumber(lexicalValue, 0, 2);
            const int minute = parseFixedWidthNumber(lexicalValue, 3, 2);
            const int second = parseFixedWidthNumber(lexicalValue, 6, 2);
            if (hour > 23 || minute > 59 || second > 59) {
                throw XmlException("invalid temporal lexical form");
            }

            std::size_t timezoneStart = 8;
            int fractionalNanoseconds = 0;
            if (timezoneStart < lexicalValue.size() && lexicalValue[timezoneStart] == '.') {
                const std::size_t fractionStart = timezoneStart + 1;
                std::size_t fractionEnd = fractionStart;
                while (fractionEnd < lexicalValue.size() && std::isdigit(static_cast<unsigned char>(lexicalValue[fractionEnd]))) {
                    ++fractionEnd;
                }
                fractionalNanoseconds = parseFractionalNanoseconds(lexicalValue.substr(fractionStart, fractionEnd - fractionStart));
                timezoneStart = fractionEnd;
            }

            const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, timezoneStart);
            int secondsOfDay = hour * 3600 + minute * 60 + second - timezoneOffsetMinutes * 60;
            secondsOfDay %= 24 * 60 * 60;
            if (secondsOfDay < 0) {
                secondsOfDay += 24 * 60 * 60;
            }
            return PracticalTemporalValue{ 0, secondsOfDay, fractionalNanoseconds };
        }

        if (baseType == "xs:dateTime") {
            if (lexicalValue.size() < 19 || lexicalValue[4] != '-' || lexicalValue[7] != '-' || lexicalValue[10] != 'T'
                || lexicalValue[13] != ':' || lexicalValue[16] != ':') {
                throw XmlException("invalid temporal lexical form");
            }
            const int year = parseFixedWidthNumber(lexicalValue, 0, 4);
            const int month = parseFixedWidthNumber(lexicalValue, 5, 2);
            const int day = parseFixedWidthNumber(lexicalValue, 8, 2);
            const int hour = parseFixedWidthNumber(lexicalValue, 11, 2);
            const int minute = parseFixedWidthNumber(lexicalValue, 14, 2);
            const int second = parseFixedWidthNumber(lexicalValue, 17, 2);
            if (day < 1 || day > daysInMonth(year, month) || hour > 23 || minute > 59 || second > 59) {
                throw XmlException("invalid temporal lexical form");
            }

            std::size_t timezoneStart = 19;
            int fractionalNanoseconds = 0;
            if (timezoneStart < lexicalValue.size() && lexicalValue[timezoneStart] == '.') {
                const std::size_t fractionStart = timezoneStart + 1;
                std::size_t fractionEnd = fractionStart;
                while (fractionEnd < lexicalValue.size() && std::isdigit(static_cast<unsigned char>(lexicalValue[fractionEnd]))) {
                    ++fractionEnd;
                }
                fractionalNanoseconds = parseFractionalNanoseconds(lexicalValue.substr(fractionStart, fractionEnd - fractionStart));
                timezoneStart = fractionEnd;
            }

            const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, timezoneStart);
            long long absoluteDay = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
            int secondsOfDay = hour * 3600 + minute * 60 + second;
            normalizeDateTimeComponents(absoluteDay, secondsOfDay, timezoneOffsetMinutes);
            return PracticalTemporalValue{ absoluteDay, secondsOfDay, fractionalNanoseconds };
        }

        throw XmlException("unsupported temporal base type");
    };

    const auto extractDigitInfo = [&](const std::string& lexicalValue) {
        struct DigitInfo {
            std::size_t totalDigits = 0;
            std::size_t fractionDigits = 0;
        };

        DigitInfo info;
        bool seenDecimalPoint = false;
        bool hasDigit = false;
        std::size_t index = 0;
        if (!lexicalValue.empty() && (lexicalValue[0] == '+' || lexicalValue[0] == '-')) {
            index = 1;
        }
        for (; index < lexicalValue.size(); ++index) {
            const char ch = lexicalValue[index];
            if (ch == '.') {
                if (seenDecimalPoint) {
                    throw XmlException("Schema validation failed: " + label + " must use a plain decimal lexical form for digit facets");
                }
                seenDecimalPoint = true;
                continue;
            }
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                throw XmlException("Schema validation failed: " + label + " must use a plain decimal lexical form for digit facets");
            }
            hasDigit = true;
            ++info.totalDigits;
            if (seenDecimalPoint) {
                ++info.fractionDigits;
            }
        }
        if (!hasDigit) {
            throw XmlException("Schema validation failed: " + label + " must use a plain decimal lexical form for digit facets");
        }
        return info;
    };

    const std::string normalizedValue = NormalizeSchemaSimpleTypeValue(type, value);
    const bool listVariety = type.variety == XmlSchemaSet::SimpleTypeRule::Variety::List;
    const bool unionVariety = type.variety == XmlSchemaSet::SimpleTypeRule::Variety::Union;

    if (listVariety) {
        if (type.itemType == nullptr) {
            throw XmlException("Schema validation failed: list simpleType is missing an itemType");
        }

        std::vector<std::string> items;
        std::string current;
        std::istringstream stream(normalizedValue);
        while (stream >> current) {
            items.push_back(current);
        }

        if (type.length.has_value() && items.size() != *type.length) {
            throw XmlException("Schema validation failed: " + label + " must contain exactly " + std::to_string(*type.length) + " list items");
        }
        if (type.minLength.has_value() && items.size() < *type.minLength) {
            throw XmlException("Schema validation failed: " + label + " must contain at least " + std::to_string(*type.minLength) + " list items");
        }
        if (type.maxLength.has_value() && items.size() > *type.maxLength) {
            throw XmlException("Schema validation failed: " + label + " must contain at most " + std::to_string(*type.maxLength) + " list items");
        }

        for (std::size_t index = 0; index < items.size(); ++index) {
            ValidateSchemaSimpleValueWithQNameResolver(
                *type.itemType,
                items[index],
                label + " list item " + std::to_string(index + 1),
                resolveNamespaceUri,
                hasNotationDeclaration,
                hasUnparsedEntityDeclaration);
        }
    }

    if (unionVariety) {
        if (type.memberTypes.empty()) {
            throw XmlException("Schema validation failed: union simpleType is missing memberTypes");
        }

        bool matchedMemberType = false;
        for (const auto& memberType : type.memberTypes) {
            try {
                ValidateSchemaSimpleValueWithQNameResolver(
                    memberType,
                    normalizedValue,
                    label,
                    resolveNamespaceUri,
                    hasNotationDeclaration,
                    hasUnparsedEntityDeclaration);
                matchedMemberType = true;
                break;
            } catch (const XmlException&) {
            }
        }
        if (!matchedMemberType) {
            throw XmlException("Schema validation failed: " + label + " does not match any declared union member type");
        }
    }

    if (!type.enumerationValues.empty()
        && std::find(type.enumerationValues.begin(), type.enumerationValues.end(), normalizedValue) == type.enumerationValues.end()) {
        throw XmlException("Schema validation failed: " + label + " must be one of the declared enumeration values");
    }

    const bool binaryLengthFamily = type.baseType == "xs:hexBinary" || type.baseType == "xs:base64Binary";
    const std::size_t valueLength = normalizedValue.size();
    if (!listVariety && !binaryLengthFamily && type.length.has_value() && valueLength != *type.length) {
        throw XmlException("Schema validation failed: " + label + " must have length " + std::to_string(*type.length));
    }
    if (!listVariety && !binaryLengthFamily && type.minLength.has_value() && valueLength < *type.minLength) {
        throw XmlException("Schema validation failed: " + label + " must have length >= " + std::to_string(*type.minLength));
    }
    if (!listVariety && !binaryLengthFamily && type.maxLength.has_value() && valueLength > *type.maxLength) {
        throw XmlException("Schema validation failed: " + label + " must have length <= " + std::to_string(*type.maxLength));
    }

    if (type.pattern.has_value()) {
        try {
            if (!std::regex_match(normalizedValue, std::regex(*type.pattern))) {
                throw XmlException("Schema validation failed: " + label + " does not match the declared pattern");
            }
        } catch (const std::regex_error&) {
            throw XmlException("Schema validation failed: unsupported pattern facet '" + *type.pattern + "'");
        }
    }

    if (type.totalDigits.has_value() || type.fractionDigits.has_value()) {
        const auto digitInfo = extractDigitInfo(normalizedValue);
        if (type.totalDigits.has_value() && digitInfo.totalDigits > *type.totalDigits) {
            throw XmlException("Schema validation failed: " + label + " must have at most " + std::to_string(*type.totalDigits) + " total digits");
        }
        if (type.fractionDigits.has_value() && digitInfo.fractionDigits > *type.fractionDigits) {
            throw XmlException("Schema validation failed: " + label + " must have at most " + std::to_string(*type.fractionDigits) + " fraction digits");
        }
    }

    try {
        if (type.baseType == "xs:anySimpleType" || type.baseType == "xs:string" || type.baseType.empty()) {
            return;
        }
        if (type.baseType == "xs:normalizedString" || type.baseType == "xs:token") {
            return;
        }
        if (IsPracticalIntegerBuiltinType(type.baseType)) {
            const PracticalIntegerValue numericValue = ParsePracticalIntegerOrThrow(normalizedValue);
            if (!PracticalIntegerFitsBuiltinType(type.baseType, numericValue)) {
                throw XmlException("integer value out of range");
            }
            if (type.minInclusive.has_value()
                && ComparePracticalIntegerValues(numericValue, ParsePracticalIntegerOrThrow(*type.minInclusive)) < 0) {
                throw XmlException("Schema validation failed: " + label + " must be >= " + *type.minInclusive);
            }
            if (type.maxInclusive.has_value()
                && ComparePracticalIntegerValues(numericValue, ParsePracticalIntegerOrThrow(*type.maxInclusive)) > 0) {
                throw XmlException("Schema validation failed: " + label + " must be <= " + *type.maxInclusive);
            }
            if (type.minExclusive.has_value()
                && ComparePracticalIntegerValues(numericValue, ParsePracticalIntegerOrThrow(*type.minExclusive)) <= 0) {
                throw XmlException("Schema validation failed: " + label + " must be > " + *type.minExclusive);
            }
            if (type.maxExclusive.has_value()
                && ComparePracticalIntegerValues(numericValue, ParsePracticalIntegerOrThrow(*type.maxExclusive)) >= 0) {
                throw XmlException("Schema validation failed: " + label + " must be < " + *type.maxExclusive);
            }
            return;
        }
        if (type.baseType == "xs:boolean") {
            (void)XmlConvert::ToBoolean(normalizedValue);
            return;
        }
        if (type.baseType == "xs:Name") {
            (void)XmlConvert::VerifyName(normalizedValue);
            return;
        }
        if (type.baseType == "xs:NCName") {
            (void)XmlConvert::VerifyNCName(normalizedValue);
            return;
        }
        if (type.baseType == "xs:NMTOKEN") {
            (void)XmlConvert::VerifyNmToken(normalizedValue);
            return;
        }
        if (type.baseType == "xs:ID" || type.baseType == "xs:IDREF") {
            (void)XmlConvert::VerifyNCName(normalizedValue);
            return;
        }
        if (type.baseType == "xs:language") {
            if (!isPracticalLanguageValue(normalizedValue)) {
                throw XmlException("invalid language tag");
            }
            return;
        }
        if (type.baseType == "xs:anyURI") {
            if (!isPracticalAnyUriValue(normalizedValue)) {
                throw XmlException("invalid URI lexical form");
            }
            return;
        }
        if (type.baseType == "xs:duration") {
            const PracticalDurationValue durationValue = ParsePracticalDurationOrThrow(normalizedValue);
            if (type.minInclusive.has_value()
                && ComparePracticalDurationValues(durationValue, ParsePracticalDurationOrThrow(*type.minInclusive)) < 0) {
                throw XmlException("Schema validation failed: " + label + " must be >= " + *type.minInclusive);
            }
            if (type.maxInclusive.has_value()
                && ComparePracticalDurationValues(durationValue, ParsePracticalDurationOrThrow(*type.maxInclusive)) > 0) {
                throw XmlException("Schema validation failed: " + label + " must be <= " + *type.maxInclusive);
            }
            if (type.minExclusive.has_value()
                && ComparePracticalDurationValues(durationValue, ParsePracticalDurationOrThrow(*type.minExclusive)) <= 0) {
                throw XmlException("Schema validation failed: " + label + " must be > " + *type.minExclusive);
            }
            if (type.maxExclusive.has_value()
                && ComparePracticalDurationValues(durationValue, ParsePracticalDurationOrThrow(*type.maxExclusive)) >= 0) {
                throw XmlException("Schema validation failed: " + label + " must be < " + *type.maxExclusive);
            }
            return;
        }
        if (type.baseType == "xs:hexBinary" || type.baseType == "xs:base64Binary") {
            const std::size_t binaryValueLength = decodePracticalBinaryValue(type.baseType, normalizedValue).size();
            if (type.length.has_value() && binaryValueLength != *type.length) {
                throw XmlException("Schema validation failed: " + label + " must have length " + std::to_string(*type.length));
            }
            if (type.minLength.has_value() && binaryValueLength < *type.minLength) {
                throw XmlException("Schema validation failed: " + label + " must have length >= " + std::to_string(*type.minLength));
            }
            if (type.maxLength.has_value() && binaryValueLength > *type.maxLength) {
                throw XmlException("Schema validation failed: " + label + " must have length <= " + std::to_string(*type.maxLength));
            }
            return;
        }
        if (type.baseType == "xs:date" || type.baseType == "xs:time" || type.baseType == "xs:dateTime"
            || type.baseType == "xs:gYear" || type.baseType == "xs:gYearMonth"
            || type.baseType == "xs:gMonth" || type.baseType == "xs:gDay" || type.baseType == "xs:gMonthDay") {
            const PracticalTemporalValue normalizedTemporalValue = normalizeTemporalValue(type.baseType, normalizedValue);
            if (type.minInclusive.has_value()) {
                const PracticalTemporalValue minInclusiveValue = normalizeTemporalValue(type.baseType, *type.minInclusive);
                if (compareTemporalValues(normalizedTemporalValue, minInclusiveValue) < 0) {
                    throw XmlException("Schema validation failed: " + label + " must be >= " + *type.minInclusive);
                }
            }
            if (type.maxInclusive.has_value()) {
                const PracticalTemporalValue maxInclusiveValue = normalizeTemporalValue(type.baseType, *type.maxInclusive);
                if (compareTemporalValues(normalizedTemporalValue, maxInclusiveValue) > 0) {
                    throw XmlException("Schema validation failed: " + label + " must be <= " + *type.maxInclusive);
                }
            }
            if (type.minExclusive.has_value()) {
                const PracticalTemporalValue minExclusiveValue = normalizeTemporalValue(type.baseType, *type.minExclusive);
                if (compareTemporalValues(normalizedTemporalValue, minExclusiveValue) <= 0) {
                    throw XmlException("Schema validation failed: " + label + " must be > " + *type.minExclusive);
                }
            }
            if (type.maxExclusive.has_value()) {
                const PracticalTemporalValue maxExclusiveValue = normalizeTemporalValue(type.baseType, *type.maxExclusive);
                if (compareTemporalValues(normalizedTemporalValue, maxExclusiveValue) >= 0) {
                    throw XmlException("Schema validation failed: " + label + " must be < " + *type.maxExclusive);
                }
            }
            return;
        }
        if (type.baseType == "xs:QName") {
            const auto [prefix, localName] = SplitQualifiedName(normalizedValue);
            if (prefix.empty()) {
                (void)XmlConvert::VerifyNCName(localName);
                return;
            }

            (void)XmlConvert::VerifyNCName(prefix);
            (void)XmlConvert::VerifyNCName(localName);
            const std::string namespaceUri = resolveNamespaceUri(prefix);
            if (namespaceUri.empty()) {
                throw XmlException("Schema validation failed: " + label + " must reference a QName prefix declared in scope");
            }
            return;
        }
        if (type.baseType == "xs:NOTATION") {
            try {
                (void)XmlConvert::VerifyNCName(normalizedValue);
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }

            if (!hasNotationDeclaration(normalizedValue)) {
                throw XmlException("Schema validation failed: " + label + " must reference a declared xs:NOTATION value");
            }
            return;
        }
        if (type.baseType == "xs:ENTITY") {
            try {
                (void)XmlConvert::VerifyNCName(normalizedValue);
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }

            if (!hasUnparsedEntityDeclaration(normalizedValue)) {
                throw XmlException("Schema validation failed: " + label + " must reference a declared unparsed xs:ENTITY value");
            }
            return;
        }
        if (type.baseType == "xs:float") {
            const float numericValue = XmlConvert::ToSingle(normalizedValue);
            if (type.minInclusive.has_value() && numericValue < XmlConvert::ToSingle(*type.minInclusive)) {
                throw XmlException("Schema validation failed: " + label + " must be >= " + *type.minInclusive);
            }
            if (type.maxInclusive.has_value() && numericValue > XmlConvert::ToSingle(*type.maxInclusive)) {
                throw XmlException("Schema validation failed: " + label + " must be <= " + *type.maxInclusive);
            }
            if (type.minExclusive.has_value() && numericValue <= XmlConvert::ToSingle(*type.minExclusive)) {
                throw XmlException("Schema validation failed: " + label + " must be > " + *type.minExclusive);
            }
            if (type.maxExclusive.has_value() && numericValue >= XmlConvert::ToSingle(*type.maxExclusive)) {
                throw XmlException("Schema validation failed: " + label + " must be < " + *type.maxExclusive);
            }
            return;
        }
        if (type.baseType == "xs:decimal") {
            const PracticalDecimalValue numericValue = ParsePracticalDecimalOrThrow(normalizedValue);
            if (type.minInclusive.has_value()
                && ComparePracticalDecimalValues(numericValue, ParsePracticalDecimalOrThrow(*type.minInclusive)) < 0) {
                throw XmlException("Schema validation failed: " + label + " must be >= " + *type.minInclusive);
            }
            if (type.maxInclusive.has_value()
                && ComparePracticalDecimalValues(numericValue, ParsePracticalDecimalOrThrow(*type.maxInclusive)) > 0) {
                throw XmlException("Schema validation failed: " + label + " must be <= " + *type.maxInclusive);
            }
            if (type.minExclusive.has_value()
                && ComparePracticalDecimalValues(numericValue, ParsePracticalDecimalOrThrow(*type.minExclusive)) <= 0) {
                throw XmlException("Schema validation failed: " + label + " must be > " + *type.minExclusive);
            }
            if (type.maxExclusive.has_value()
                && ComparePracticalDecimalValues(numericValue, ParsePracticalDecimalOrThrow(*type.maxExclusive)) >= 0) {
                throw XmlException("Schema validation failed: " + label + " must be < " + *type.maxExclusive);
            }
            return;
        }
        if (type.baseType == "xs:double") {
            const double numericValue = XmlConvert::ToDouble(normalizedValue);
            if (type.minInclusive.has_value() && numericValue < XmlConvert::ToDouble(*type.minInclusive)) {
                throw XmlException("Schema validation failed: " + label + " must be >= " + *type.minInclusive);
            }
            if (type.maxInclusive.has_value() && numericValue > XmlConvert::ToDouble(*type.maxInclusive)) {
                throw XmlException("Schema validation failed: " + label + " must be <= " + *type.maxInclusive);
            }
            if (type.minExclusive.has_value() && numericValue <= XmlConvert::ToDouble(*type.minExclusive)) {
                throw XmlException("Schema validation failed: " + label + " must be > " + *type.minExclusive);
            }
            if (type.maxExclusive.has_value() && numericValue >= XmlConvert::ToDouble(*type.maxExclusive)) {
                throw XmlException("Schema validation failed: " + label + " must be < " + *type.maxExclusive);
            }
            return;
        }
    } catch (const XmlException&) {
        if (IsPracticalIntegerBuiltinType(type.baseType)) {
            try {
                const PracticalIntegerValue numericValue = ParsePracticalIntegerOrThrow(normalizedValue);
                if (!PracticalIntegerFitsBuiltinType(type.baseType, numericValue)) {
                    throw XmlException("integer value out of range");
                }
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }
            throw;
        }
        if (type.baseType == "xs:float") {
            try {
                (void)XmlConvert::ToSingle(normalizedValue);
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }
            throw;
        }
        if (type.baseType == "xs:decimal") {
            try {
                (void)ParsePracticalDecimalOrThrow(normalizedValue);
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }
            throw;
        }
        if (type.baseType == "xs:double") {
            try {
                (void)XmlConvert::ToDouble(normalizedValue);
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }
            throw;
        }
        if (type.baseType == "xs:boolean") {
            try {
                (void)XmlConvert::ToBoolean(normalizedValue);
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }
            throw;
        }
        if (type.baseType == "xs:Name") {
            try {
                (void)XmlConvert::VerifyName(normalizedValue);
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }
            throw;
        }
        if (type.baseType == "xs:NCName") {
            try {
                (void)XmlConvert::VerifyNCName(normalizedValue);
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }
            throw;
        }
        if (type.baseType == "xs:NOTATION") {
            try {
                (void)XmlConvert::VerifyNCName(normalizedValue);
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }
            throw;
        }
        if (type.baseType == "xs:NMTOKEN") {
            try {
                (void)XmlConvert::VerifyNmToken(normalizedValue);
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }
            throw;
        }
        if (type.baseType == "xs:ID" || type.baseType == "xs:IDREF") {
            try {
                (void)XmlConvert::VerifyNCName(normalizedValue);
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }
            throw;
        }
        if (type.baseType == "xs:language" || type.baseType == "xs:anyURI") {
            throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
        }
        if (type.baseType == "xs:duration") {
            try {
                (void)ParsePracticalDurationOrThrow(normalizedValue);
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }
            throw;
        }
        if (type.baseType == "xs:hexBinary" || type.baseType == "xs:base64Binary") {
            try {
                (void)decodePracticalBinaryValue(type.baseType, normalizedValue);
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }
            throw;
        }
        if (type.baseType == "xs:date" || type.baseType == "xs:time" || type.baseType == "xs:dateTime"
            || type.baseType == "xs:gYear" || type.baseType == "xs:gYearMonth"
            || type.baseType == "xs:gMonth" || type.baseType == "xs:gDay" || type.baseType == "xs:gMonthDay") {
            try {
                (void)normalizeTemporalValue(type.baseType, normalizedValue);
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }
            throw;
        }
        if (type.baseType == "xs:QName") {
            try {
                const auto [prefix, localName] = SplitQualifiedName(normalizedValue);
                if (prefix.empty()) {
                    (void)XmlConvert::VerifyNCName(localName);
                } else {
                    (void)XmlConvert::VerifyNCName(prefix);
                    (void)XmlConvert::VerifyNCName(localName);
                    const std::string namespaceUri = resolveNamespaceUri(prefix);
                    if (namespaceUri.empty()) {
                        throw XmlException("undefined QName prefix");
                    }
                }
            } catch (const XmlException&) {
                throw XmlException("Schema validation failed: " + label + " must be a valid " + type.baseType + " value");
            }
            throw;
        }
        throw;
    }
    throw XmlException("Schema validation failed: unsupported simpleType base '" + type.baseType + "'");
}

void ValidateSchemaSimpleValue(
    const XmlSchemaSet::SimpleTypeRule& type,
    const std::string& value,
    const std::string& label,
    const XmlElement* contextElement) {
    ValidateSchemaSimpleValueWithQNameResolver(
        type,
        value,
        label,
        [&](const std::string& prefix) {
            return LookupNamespaceUriOnElement(contextElement, prefix);
        },
        [&](const std::string& name) {
            const XmlDocument* ownerDocument = contextElement == nullptr ? nullptr : contextElement->OwnerDocument();
            return HasDocumentNotationDeclaration(ownerDocument, name);
        },
        [&](const std::string& name) {
            const XmlDocument* ownerDocument = contextElement == nullptr ? nullptr : contextElement->OwnerDocument();
            return HasDocumentUnparsedEntityDeclaration(ownerDocument, name);
        });
}

void ValidateSchemaSimpleValue(
    const XmlSchemaSet::SimpleTypeRule& type,
    const std::string& value,
    const std::string& label,
    const std::vector<std::pair<std::string, std::string>>& namespaceBindings,
    const std::unordered_set<std::string>& notationDeclarationNames,
    const std::unordered_set<std::string>& unparsedEntityDeclarationNames) {
    ValidateSchemaSimpleValueWithQNameResolver(
        type,
        value,
        label,
        [&](const std::string& prefix) {
            return LookupNamespaceUriInBindings(namespaceBindings, prefix);
        },
        [&](const std::string& name) {
            return notationDeclarationNames.find(name) != notationDeclarationNames.end();
        },
        [&](const std::string& name) {
            return unparsedEntityDeclarationNames.find(name) != unparsedEntityDeclarationNames.end();
        });
}

struct PendingSchemaIdReference {
    std::string value;
    std::string label;
};

struct SchemaIdentityValidationState {
    std::unordered_set<std::string> declaredIds;
    std::vector<PendingSchemaIdReference> pendingIdReferences;
};

struct SchemaObservedChildElement {
    std::string name;
    std::string localName;
    std::string namespaceUri;
    const XmlElement* element = nullptr;
};

struct SchemaElementObservedContent {
    std::vector<SchemaObservedChildElement> elementChildren;
    bool hasSignificantText = false;
    std::string textValue;
};

struct SchemaObservedAttribute {
    std::string name;
    std::string localName;
    std::string prefix;
    std::string namespaceUri;
    std::string value;
};

struct SchemaIdentityNodeSnapshot {
    std::string name;
    std::string localName;
    std::string namespaceUri;
    std::vector<SchemaObservedAttribute> attributes;
    std::string textValue;
    std::vector<std::shared_ptr<SchemaIdentityNodeSnapshot>> children;
};

struct SchemaValidationFrame {
    std::string name;
    std::string localName;
    std::string namespaceUri;
    int depth = 0;
    bool isEmptyElement = false;
    const XmlElement* element = nullptr;
    const XmlDocument* ownerDocument = nullptr;
    std::vector<std::pair<std::string, std::string>> namespaceBindings;
    std::vector<SchemaObservedAttribute> observedAttributes;
    SchemaElementObservedContent observedContent;
    std::shared_ptr<SchemaIdentityNodeSnapshot> identitySnapshot;
};

std::vector<SchemaObservedAttribute> ObserveSchemaElementAttributes(const XmlElement& element) {
    std::vector<SchemaObservedAttribute> observed;
    observed.reserve(element.Attributes().size());
    for (const auto& attribute : element.Attributes()) {
        if (attribute == nullptr) {
            continue;
        }
        observed.push_back(SchemaObservedAttribute{
            attribute->Name(),
            attribute->LocalName(),
            attribute->Prefix(),
            attribute->NamespaceURI(),
            attribute->Value()});
    }
    return observed;
}

const SchemaObservedAttribute* FindObservedSchemaAttribute(
    const std::vector<SchemaObservedAttribute>& observedAttributes,
    const std::string& localName,
    const std::string& namespaceUri) {
    const auto found = std::find_if(observedAttributes.begin(), observedAttributes.end(), [&](const auto& attribute) {
        return attribute.localName == localName && attribute.namespaceUri == namespaceUri;
    });
    return found == observedAttributes.end() ? nullptr : std::addressof(*found);
}

std::string LookupNamespaceUriOnSchemaFrame(const SchemaValidationFrame& frame, const std::string& prefix) {
    if (frame.namespaceBindings.empty() && frame.element != nullptr) {
        return LookupNamespaceUriOnElement(frame.element, prefix);
    }

    for (auto it = frame.namespaceBindings.rbegin(); it != frame.namespaceBindings.rend(); ++it) {
        if (it->first == prefix) {
            return it->second;
        }
    }
    return {};
}

std::vector<SchemaObservedAttribute> ObserveSchemaReaderAttributes(XmlReader& reader) {
    std::vector<SchemaObservedAttribute> observed;
    if (reader.NodeType() != XmlNodeType::Element || !reader.HasAttributes()) {
        return observed;
    }

    if (reader.MoveToFirstAttribute()) {
        do {
            observed.push_back(SchemaObservedAttribute{
                reader.Name(),
                reader.LocalName(),
                reader.Prefix(),
                reader.NamespaceURI(),
                reader.Value()});
        } while (reader.MoveToNextAttribute());
        reader.MoveToElement();
    }

    return observed;
}

std::vector<std::pair<std::string, std::string>> ExtendSchemaNamespaceBindings(
    const std::vector<std::pair<std::string, std::string>>& parentBindings,
    const std::vector<SchemaObservedAttribute>& observedAttributes) {
    std::vector<std::pair<std::string, std::string>> bindings = parentBindings;
    if (bindings.empty()) {
        bindings.push_back({"xml", "http://www.w3.org/XML/1998/namespace"});
        bindings.push_back({"xmlns", "http://www.w3.org/2000/xmlns/"});
    }

    for (const auto& attribute : observedAttributes) {
        if (attribute.name == "xmlns") {
            bindings.push_back({std::string{}, attribute.value});
            continue;
        }
        if (attribute.prefix == "xmlns") {
            bindings.push_back({attribute.localName, attribute.value});
        }
    }

    return bindings;
}

SchemaValidationFrame BuildSchemaValidationFrame(
    XmlReader& reader,
    const std::vector<std::pair<std::string, std::string>>& parentBindings = {},
    const bool captureIdentitySnapshot = false) {
    SchemaValidationFrame frame;
    frame.name = reader.Name();
    frame.localName = reader.LocalName();
    frame.namespaceUri = reader.NamespaceURI();
    frame.depth = reader.Depth();
    frame.isEmptyElement = reader.IsEmptyElement();
    frame.observedAttributes = ObserveSchemaReaderAttributes(reader);
    frame.namespaceBindings = ExtendSchemaNamespaceBindings(parentBindings, frame.observedAttributes);
    if (captureIdentitySnapshot) {
        frame.identitySnapshot = std::make_shared<SchemaIdentityNodeSnapshot>();
        frame.identitySnapshot->name = frame.name;
        frame.identitySnapshot->localName = frame.localName;
        frame.identitySnapshot->namespaceUri = frame.namespaceUri;
        frame.identitySnapshot->attributes = frame.observedAttributes;
    }
    return frame;
}

void AppendObservedSchemaChildFrame(SchemaValidationFrame& parentFrame, const SchemaValidationFrame& childFrame) {
    parentFrame.observedContent.elementChildren.push_back(SchemaObservedChildElement{
        childFrame.name,
        childFrame.localName,
        childFrame.namespaceUri,
        childFrame.element});
    if (parentFrame.identitySnapshot != nullptr && childFrame.identitySnapshot != nullptr) {
        parentFrame.identitySnapshot->children.push_back(childFrame.identitySnapshot);
    }
}

void AppendSchemaTextToFrame(SchemaValidationFrame& frame, const std::string& text) {
    if (IsWhitespaceOnly(text)) {
        return;
    }
    frame.observedContent.hasSignificantText = true;
    frame.observedContent.textValue += text;
    if (frame.identitySnapshot != nullptr) {
        frame.identitySnapshot->textValue += text;
    }
}

template <typename AttributeRuleT>
std::optional<std::string> ResolveSchemaAttributeEffectiveValue(
    const AttributeRuleT& attributeRule,
    const XmlElement& element,
    const std::vector<SchemaObservedAttribute>& observedAttributes) {
    const auto* observedAttribute = FindObservedSchemaAttribute(observedAttributes, attributeRule.name, attributeRule.namespaceUri);
    const bool present = observedAttribute != nullptr;
    if (attributeRule.required && !present) {
        throw XmlException(
            "Schema validation failed: required attribute '" + attributeRule.name
            + "' is missing on element '" + element.Name() + "'");
    }

    if (present) {
        if (attributeRule.fixedValue.has_value() && observedAttribute->value != *attributeRule.fixedValue) {
            throw XmlException(
                "Schema validation failed: attribute '" + attributeRule.name
                + "' on element '" + element.Name() + "' must match the fixed value '" + *attributeRule.fixedValue + "'");
        }
        return observedAttribute->value;
    }

    if (!attributeRule.required) {
        if (attributeRule.fixedValue.has_value()) {
            return *attributeRule.fixedValue;
        }
        if (attributeRule.defaultValue.has_value()) {
            return *attributeRule.defaultValue;
        }
    }

    return std::nullopt;
}

template <typename AttributeRuleT>
std::optional<std::string> ResolveSchemaAttributeEffectiveValue(
    const AttributeRuleT& attributeRule,
    const std::string& elementName,
    const std::vector<SchemaObservedAttribute>& observedAttributes) {
    const auto* observedAttribute = FindObservedSchemaAttribute(observedAttributes, attributeRule.name, attributeRule.namespaceUri);
    const bool present = observedAttribute != nullptr;
    if (attributeRule.required && !present) {
        throw XmlException(
            "Schema validation failed: required attribute '" + attributeRule.name
            + "' is missing on element '" + elementName + "'");
    }

    if (present) {
        if (attributeRule.fixedValue.has_value() && observedAttribute->value != *attributeRule.fixedValue) {
            throw XmlException(
                "Schema validation failed: attribute '" + attributeRule.name
                + "' on element '" + elementName + "' must match the fixed value '" + *attributeRule.fixedValue + "'");
        }
        return observedAttribute->value;
    }

    if (!attributeRule.required) {
        if (attributeRule.fixedValue.has_value()) {
            return *attributeRule.fixedValue;
        }
        if (attributeRule.defaultValue.has_value()) {
            return *attributeRule.defaultValue;
        }
    }

    return std::nullopt;
}

SchemaElementObservedContent ObserveSchemaElementContent(const XmlElement& element) {
    SchemaElementObservedContent observed;
    for (const auto& child : element.ChildNodes()) {
        if (child == nullptr) {
            continue;
        }
        if (child->NodeType() == XmlNodeType::Element) {
            const auto* childElement = static_cast<const XmlElement*>(child.get());
            observed.elementChildren.push_back(SchemaObservedChildElement{
                childElement->Name(),
                childElement->LocalName(),
                childElement->NamespaceURI(),
                childElement});
            continue;
        }
        if ((child->NodeType() == XmlNodeType::Text || child->NodeType() == XmlNodeType::CDATA)
            && !IsWhitespaceOnly(child->Value())) {
            observed.hasSignificantText = true;
            observed.textValue += child->Value();
        }
    }
    return observed;
}

SchemaValidationFrame BuildSchemaValidationFrame(const XmlElement& element) {
    SchemaValidationFrame frame;
    frame.name = element.Name();
    frame.localName = element.LocalName();
    frame.namespaceUri = element.NamespaceURI();
    frame.isEmptyElement = !element.HasChildNodes() && !element.WritesFullEndElement();
    frame.element = std::addressof(element);
    frame.ownerDocument = element.OwnerDocument();
    frame.observedAttributes = ObserveSchemaElementAttributes(element);
    frame.observedContent = ObserveSchemaElementContent(element);
    return frame;
}

template <typename ElementRuleT>
std::optional<std::string> ResolveSchemaElementEffectiveValue(
    const ElementRuleT& rule,
    const XmlElement& element,
    const SchemaElementObservedContent& observed,
    const bool isNilled) {
    if (isNilled || !observed.elementChildren.empty()) {
        return std::nullopt;
    }

    if (observed.hasSignificantText) {
        if (rule.fixedValue.has_value() && observed.textValue != *rule.fixedValue) {
            throw XmlException(
                "Schema validation failed: element '" + element.Name() + "' must match the fixed value '" + *rule.fixedValue + "'");
        }
        return observed.textValue;
    }

    if (rule.fixedValue.has_value()) {
        return *rule.fixedValue;
    }
    if (rule.defaultValue.has_value()) {
        return *rule.defaultValue;
    }
    return std::nullopt;
}

template <typename ElementRuleT>
std::optional<std::string> ResolveSchemaElementEffectiveValue(
    const ElementRuleT& rule,
    const std::string& elementName,
    const SchemaElementObservedContent& observed,
    const bool isNilled) {
    if (isNilled || !observed.elementChildren.empty()) {
        return std::nullopt;
    }

    if (observed.hasSignificantText) {
        if (rule.fixedValue.has_value() && observed.textValue != *rule.fixedValue) {
            throw XmlException(
                "Schema validation failed: element '" + elementName + "' must match the fixed value '" + *rule.fixedValue + "'");
        }
        return observed.textValue;
    }

    if (rule.fixedValue.has_value()) {
        return *rule.fixedValue;
    }
    if (rule.defaultValue.has_value()) {
        return *rule.defaultValue;
    }
    return std::nullopt;
}

template <typename ParticleT, typename ChildT, typename FindDeclaredRuleFn, typename ElementMatchesFn,
    typename WildcardMatchesFn, typename WildcardAllowsUndeclaredFn, typename IsElementParticleFn,
    typename IsAnyParticleFn, typename IsSequenceParticleFn, typename IsAllParticleFn,
    typename GetParticleChildrenFn, typename GetParticleNameFn, typename GetParticleNamespaceFn,
    typename GetParticleProcessContentsFn, typename GetParticleMinOccursFn, typename GetParticleMaxOccursFn,
    typename GetChildLocalNameFn, typename GetChildNamespaceFn>
std::vector<std::size_t> CollectObservedParticleMatches(
    const ParticleT& particle,
    const std::vector<ChildT>& elementChildren,
    FindDeclaredRuleFn&& findDeclaredRule,
    ElementMatchesFn&& elementMatchesDeclaration,
    WildcardMatchesFn&& matchesWildcardNamespace,
    WildcardAllowsUndeclaredFn&& wildcardAllowsUndeclared,
    IsElementParticleFn&& isElementParticle,
    IsAnyParticleFn&& isAnyParticle,
    IsSequenceParticleFn&& isSequenceParticle,
    IsAllParticleFn&& isAllParticle,
    GetParticleChildrenFn&& getParticleChildren,
    GetParticleNameFn&& getParticleName,
    GetParticleNamespaceFn&& getParticleNamespace,
    GetParticleProcessContentsFn&& getParticleProcessContents,
    GetParticleMinOccursFn&& getParticleMinOccurs,
    GetParticleMaxOccursFn&& getParticleMaxOccurs,
    GetChildLocalNameFn&& getChildLocalName,
    GetChildNamespaceFn&& getChildNamespace) {
    const auto dedupeIndices = [](std::vector<std::size_t>& indices) {
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    };

    std::function<std::vector<std::size_t>(const ParticleT&, std::size_t)> collectParticleMatches;
    std::function<std::vector<std::size_t>(const ParticleT&, std::size_t)> collectSingleOccurrenceMatches;

    collectSingleOccurrenceMatches = [&](const ParticleT& currentParticle, std::size_t startIndex) -> std::vector<std::size_t> {
        if (isElementParticle(currentParticle)) {
            const auto* declaredRule = findDeclaredRule(getParticleName(currentParticle), getParticleNamespace(currentParticle));
            if (declaredRule != nullptr
                && startIndex < elementChildren.size()
                && elementMatchesDeclaration(elementChildren[startIndex], *declaredRule)) {
                return {startIndex + 1};
            }
            return {};
        }

        if (isAnyParticle(currentParticle)) {
            if (startIndex < elementChildren.size()
                && matchesWildcardNamespace(getParticleNamespace(currentParticle), getChildNamespace(elementChildren[startIndex]))) {
                const auto* childRule = findDeclaredRule(
                    getChildLocalName(elementChildren[startIndex]), getChildNamespace(elementChildren[startIndex]));
                if (childRule == nullptr && !wildcardAllowsUndeclared(getParticleProcessContents(currentParticle))) {
                    return {};
                }
                return {startIndex + 1};
            }
            return {};
        }

        if (isSequenceParticle(currentParticle)) {
            std::vector<std::size_t> indices{startIndex};
            for (const auto& childParticle : getParticleChildren(currentParticle)) {
                std::vector<std::size_t> nextIndices;
                for (const auto index : indices) {
                    auto childMatches = collectParticleMatches(childParticle, index);
                    nextIndices.insert(nextIndices.end(), childMatches.begin(), childMatches.end());
                }
                dedupeIndices(nextIndices);
                indices = std::move(nextIndices);
                if (indices.empty()) {
                    break;
                }
            }
            return indices;
        }

        if (isAllParticle(currentParticle)) {
            std::vector<std::size_t> indices;
            std::function<void(std::size_t, std::vector<bool>&)> collectAllMatches;

            collectAllMatches = [&](std::size_t currentIndex, std::vector<bool>& usedChildren) {
                bool canStop = true;
                for (std::size_t childIndex = 0; childIndex < getParticleChildren(currentParticle).size(); ++childIndex) {
                    if (!usedChildren[childIndex] && getParticleMinOccurs(getParticleChildren(currentParticle)[childIndex]) > 0) {
                        canStop = false;
                        break;
                    }
                }
                if (canStop) {
                    indices.push_back(currentIndex);
                }

                for (std::size_t childIndex = 0; childIndex < getParticleChildren(currentParticle).size(); ++childIndex) {
                    if (usedChildren[childIndex]) {
                        continue;
                    }

                    auto childMatches = collectSingleOccurrenceMatches(getParticleChildren(currentParticle)[childIndex], currentIndex);
                    for (const auto matchIndex : childMatches) {
                        if (matchIndex == currentIndex) {
                            continue;
                        }

                        std::vector<bool> nextUsed = usedChildren;
                        nextUsed[childIndex] = true;
                        collectAllMatches(matchIndex, nextUsed);
                    }
                }
            };

            std::vector<bool> usedChildren(getParticleChildren(currentParticle).size(), false);
            collectAllMatches(startIndex, usedChildren);
            dedupeIndices(indices);
            return indices;
        }

        std::vector<std::size_t> indices;
        for (const auto& childParticle : getParticleChildren(currentParticle)) {
            auto childMatches = collectParticleMatches(childParticle, startIndex);
            indices.insert(indices.end(), childMatches.begin(), childMatches.end());
        }
        dedupeIndices(indices);
        return indices;
    };

    collectParticleMatches = [&](const ParticleT& currentParticle, std::size_t startIndex) -> std::vector<std::size_t> {
        std::vector<std::size_t> matches;
        if (getParticleMinOccurs(currentParticle) == 0) {
            matches.push_back(startIndex);
        }

        std::vector<std::size_t> currentIndices{startIndex};
        const std::size_t maxRepeats = getParticleMaxOccurs(currentParticle) == std::numeric_limits<std::size_t>::max()
            ? elementChildren.size() - startIndex + 1
            : getParticleMaxOccurs(currentParticle);

        for (std::size_t repeat = 1; repeat <= maxRepeats; ++repeat) {
            std::vector<std::size_t> nextIndices;
            bool progressed = false;
            for (const auto index : currentIndices) {
                auto occurrenceMatches = collectSingleOccurrenceMatches(currentParticle, index);
                for (const auto matchIndex : occurrenceMatches) {
                    nextIndices.push_back(matchIndex);
                    if (matchIndex != index) {
                        progressed = true;
                    }
                }
            }
            dedupeIndices(nextIndices);
            if (nextIndices.empty()) {
                break;
            }
            if (repeat >= getParticleMinOccurs(currentParticle)) {
                matches.insert(matches.end(), nextIndices.begin(), nextIndices.end());
            }
            if (!progressed) {
                break;
            }
            currentIndices = std::move(nextIndices);
        }

        dedupeIndices(matches);
        return matches;
    };

    return collectParticleMatches(particle, 0);
}

void ValidatePendingSchemaIdReferences(const SchemaIdentityValidationState& state) {
    for (const auto& reference : state.pendingIdReferences) {
        if (state.declaredIds.find(reference.value) == state.declaredIds.end()) {
            throw XmlException(
                "Schema validation failed: " + reference.label + " references unknown xs:ID value '" + reference.value + "'");
        }
    }
}

void XmlDocument::Validate(const XmlSchemaSet& schemas) const {
    static const std::string kSchemaInstanceNamespace = "http://www.w3.org/2001/XMLSchema-instance";
    SchemaIdentityValidationState identityState;

    std::function<void(const XmlSchemaSet::SimpleTypeRule&, const std::string&, const std::string&, const XmlElement*)> recordIdentityTypedValue;
    recordIdentityTypedValue = [&](const XmlSchemaSet::SimpleTypeRule& type, const std::string& value, const std::string& label, const XmlElement* contextElement) {
        if (type.variety == XmlSchemaSet::SimpleTypeRule::Variety::List) {
            if (type.itemType == nullptr) {
                return;
            }

            std::istringstream stream(NormalizeSchemaSimpleTypeValue(type, value));
            std::string item;
            std::size_t index = 0;
            while (stream >> item) {
                ++index;
                recordIdentityTypedValue(*type.itemType, item, label + " list item " + std::to_string(index), contextElement);
            }
            return;
        }

        if (type.variety == XmlSchemaSet::SimpleTypeRule::Variety::Union) {
            for (const auto& memberType : type.memberTypes) {
                try {
                    ValidateSchemaSimpleValue(memberType, value, label, contextElement);
                    recordIdentityTypedValue(memberType, value, label, contextElement);
                    return;
                } catch (const XmlException&) {
                }
            }
            return;
        }

        const std::string normalizedValue = NormalizeSchemaSimpleTypeValue(type, value);
        if (type.baseType == "xs:ID") {
            if (!identityState.declaredIds.insert(normalizedValue).second) {
                throw XmlException("Schema validation failed: " + label + " duplicates xs:ID value '" + normalizedValue + "'");
            }
            return;
        }
        if (type.baseType == "xs:IDREF") {
            identityState.pendingIdReferences.push_back(PendingSchemaIdReference{normalizedValue, label});
        }
    };

    const auto root = DocumentElement();
    if (root == nullptr) {
        throw XmlException("Cannot validate an XML document without a root element");
    }

    const auto runtimeSimpleTypeDerivesFrom = [&](const XmlSchemaSet::SimpleTypeRule& derivedType,
        const XmlSchemaSet::SimpleTypeRule& baseType,
        bool& usesRestriction) {
        if (!derivedType.namedTypeName.empty()
            && derivedType.namedTypeName == baseType.namedTypeName
            && derivedType.namedTypeNamespaceUri == baseType.namedTypeNamespaceUri) {
            return true;
        }
        if (derivedType.namedTypeName.empty()
            && baseType.namedTypeName.empty()
            && derivedType.derivationMethod == XmlSchemaSet::SimpleTypeRule::DerivationMethod::None
            && baseType.derivationMethod == XmlSchemaSet::SimpleTypeRule::DerivationMethod::None
            && derivedType.variety == baseType.variety) {
            bool builtinUsesRestriction = false;
            if (BuiltinSimpleTypeDerivesFrom(derivedType.baseType, baseType.baseType, builtinUsesRestriction)) {
                usesRestriction = usesRestriction || builtinUsesRestriction;
                return true;
            }
        }

        const XmlSchemaSet::SimpleTypeRule* current = &derivedType;
        std::unordered_set<std::string> visited;
        while (true) {
            if (current->derivationMethod == XmlSchemaSet::SimpleTypeRule::DerivationMethod::Restriction) {
                usesRestriction = true;
            }
            if (!baseType.namedTypeName.empty()
                && current->derivationBaseName == baseType.namedTypeName
                && current->derivationBaseNamespaceUri == baseType.namedTypeNamespaceUri) {
                return true;
            }
            if (baseType.namedTypeName.empty()
                && current->derivationMethod == XmlSchemaSet::SimpleTypeRule::DerivationMethod::Restriction) {
                bool builtinUsesRestriction = false;
                if (BuiltinSimpleTypeDerivesFrom(current->baseType, baseType.baseType, builtinUsesRestriction)) {
                    usesRestriction = usesRestriction || builtinUsesRestriction;
                    return true;
                }
            }
            if (current->derivationBaseName.empty()) {
                return false;
            }

            const std::string currentKey = current->derivationBaseNamespaceUri + "\n" + current->derivationBaseName;
            if (!visited.insert(currentKey).second) {
                return false;
            }

            current = schemas.FindSimpleTypeRule(current->derivationBaseName, current->derivationBaseNamespaceUri);
            if (current == nullptr) {
                return false;
            }
            if (!baseType.namedTypeName.empty()
                && current->namedTypeName == baseType.namedTypeName
                && current->namedTypeNamespaceUri == baseType.namedTypeNamespaceUri) {
                return true;
            }
        }
    };

    const auto runtimeComplexTypeDerivesFrom = [&](const XmlSchemaSet::ComplexTypeRule& derivedType,
        const XmlSchemaSet::ComplexTypeRule& baseType,
        bool& usesRestriction,
        bool& usesExtension) {
        if (baseType.namedTypeName == "anyType"
            && baseType.namedTypeNamespaceUri == "http://www.w3.org/2001/XMLSchema") {
            return true;
        }
        if (!derivedType.namedTypeName.empty()
            && derivedType.namedTypeName == baseType.namedTypeName
            && derivedType.namedTypeNamespaceUri == baseType.namedTypeNamespaceUri) {
            return true;
        }

        const XmlSchemaSet::ComplexTypeRule* current = &derivedType;
        std::unordered_set<std::string> visited;
        while (true) {
            if (current->derivationMethod == XmlSchemaSet::ComplexTypeRule::DerivationMethod::Restriction) {
                usesRestriction = true;
            } else if (current->derivationMethod == XmlSchemaSet::ComplexTypeRule::DerivationMethod::Extension) {
                usesExtension = true;
            }
            if (current->derivationBaseName == baseType.namedTypeName
                && current->derivationBaseNamespaceUri == baseType.namedTypeNamespaceUri) {
                return true;
            }
            if (current->derivationBaseName.empty()) {
                return false;
            }

            const std::string currentKey = current->derivationBaseNamespaceUri + "\n" + current->derivationBaseName;
            if (!visited.insert(currentKey).second) {
                return false;
            }

            current = schemas.FindComplexTypeRule(current->derivationBaseName, current->derivationBaseNamespaceUri);
            if (current == nullptr) {
                return false;
            }
            if (!baseType.namedTypeName.empty()
                && current->namedTypeName == baseType.namedTypeName
                && current->namedTypeNamespaceUri == baseType.namedTypeNamespaceUri) {
                return true;
            }
        }
    };

    const auto runtimeElementCanSubstituteForHead = [&](const XmlSchemaSet::ElementRule& actualRule,
        const XmlSchemaSet::ElementRule& declaration) {
        bool usesRestriction = false;
        bool usesExtension = false;
        if (declaration.declaredComplexType.has_value()) {
            if (!actualRule.declaredComplexType.has_value()) {
                return false;
            }
            if (!runtimeComplexTypeDerivesFrom(*actualRule.declaredComplexType, *declaration.declaredComplexType, usesRestriction, usesExtension)) {
                return false;
            }
        } else if (declaration.declaredSimpleType.has_value()) {
            if (!actualRule.declaredSimpleType.has_value()) {
                return false;
            }
            if (!runtimeSimpleTypeDerivesFrom(*actualRule.declaredSimpleType, *declaration.declaredSimpleType, usesRestriction)) {
                return false;
            }
        }

        if ((usesRestriction && declaration.blockRestriction)
            || (usesExtension && declaration.blockExtension)) {
            return false;
        }
        return true;
    };

    const auto elementMatchesDeclaration = [&](const SchemaObservedChildElement& element, const XmlSchemaSet::ElementRule& declaration) {
        const auto* actualRule = schemas.FindElementRule(element.localName, element.namespaceUri);
        if (actualRule == nullptr) {
            return false;
        }
        if (actualRule->name == declaration.name && actualRule->namespaceUri == declaration.namespaceUri) {
            return true;
        }
        if (declaration.blockSubstitution) {
            return false;
        }

        std::string currentHeadName = actualRule->substitutionGroupHeadName;
        std::string currentHeadNamespaceUri = actualRule->substitutionGroupHeadNamespaceUri;
        std::unordered_set<std::string> visitedHeads;
        while (!currentHeadName.empty()) {
            const std::string headKey = currentHeadNamespaceUri + "\n" + currentHeadName;
            if (!visitedHeads.insert(headKey).second) {
                break;
            }
            if (currentHeadName == declaration.name && currentHeadNamespaceUri == declaration.namespaceUri) {
                return runtimeElementCanSubstituteForHead(*actualRule, declaration);
            }
            const auto* headRule = schemas.FindElementRule(currentHeadName, currentHeadNamespaceUri);
            if (headRule == nullptr) {
                break;
            }
            currentHeadName = headRule->substitutionGroupHeadName;
            currentHeadNamespaceUri = headRule->substitutionGroupHeadNamespaceUri;
        }
        return false;
    };

    const auto* rootRule = schemas.FindElementRule(root->LocalName(), root->NamespaceURI());
    if (rootRule == nullptr) {
        throw XmlException("Schema validation failed: no schema declaration found for root element '" + root->Name() + "'");
    }
    if (rootRule->isAbstract) {
        throw XmlException("Schema validation failed: element '" + root->Name() + "' is abstract and cannot appear in the document");
    }

    const auto wildcardNamespaceTokenMatches = [&](const std::string& token, const std::string& namespaceUri) {
        if (token.empty() || token == "##any") {
            return true;
        }
        if (token == "##other") {
            return namespaceUri != root->NamespaceURI();
        }
        if (token == "##targetNamespace") {
            return namespaceUri == root->NamespaceURI();
        }
        if (token == "##local") {
            return namespaceUri.empty();
        }
        return namespaceUri == token;
    };

    const auto matchesWildcardNamespace = [&](const std::string& namespaceConstraint, const std::string& namespaceUri) {
        if (namespaceConstraint.empty() || namespaceConstraint == "##any") {
            return true;
        }
        std::istringstream tokens(namespaceConstraint);
        std::string token;
        while (tokens >> token) {
            if (wildcardNamespaceTokenMatches(token, namespaceUri)) {
                return true;
            }
        }
        return false;
    };

    const auto wildcardAllowsValidationSkip = [](const std::string& processContents) {
        return processContents == "skip";
    };

    const auto wildcardAllowsUndeclared = [](const std::string& processContents) {
        return processContents == "lax" || processContents == "skip";
    };

    const auto resolveBuiltinSimpleType = [](const std::string& qualifiedName) -> std::optional<XmlSchemaSet::SimpleTypeRule> {
        const auto descriptor = ResolveBuiltinSimpleTypeDescriptor(qualifiedName);
        if (!descriptor.has_value()) {
            return std::nullopt;
        }

        XmlSchemaSet::SimpleTypeRule rule;
        rule.baseType = descriptor->baseType;
        rule.whiteSpace = descriptor->whiteSpace;
        if (descriptor->variety == BuiltinSimpleTypeDescriptor::Variety::List) {
            rule.variety = XmlSchemaSet::SimpleTypeRule::Variety::List;

            XmlSchemaSet::SimpleTypeRule itemType;
            itemType.baseType = descriptor->itemType;
            itemType.whiteSpace = descriptor->itemWhiteSpace;
            rule.itemType = std::make_shared<XmlSchemaSet::SimpleTypeRule>(itemType);
        }
        return rule;
    };

    const auto resolveBuiltinComplexType = [](const std::string& qualifiedName) -> std::optional<XmlSchemaSet::ComplexTypeRule> {
        if (qualifiedName != "xs:anyType") {
            return std::nullopt;
        }

        XmlSchemaSet::ComplexTypeRule rule;
        rule.namedTypeName = "anyType";
        rule.namedTypeNamespaceUri = "http://www.w3.org/2001/XMLSchema";
        rule.allowsText = true;
        rule.anyAttributeAllowed = true;
        rule.anyAttributeNamespaceConstraint = "##any";
        rule.anyAttributeProcessContents = "lax";

        XmlSchemaSet::Particle particle;
        particle.kind = XmlSchemaSet::Particle::Kind::Any;
        particle.namespaceUri = "##any";
        particle.processContents = "lax";
        particle.minOccurs = 0;
        particle.maxOccurs = std::numeric_limits<std::size_t>::max();
        rule.particle = particle;
        return rule;
    };

    const auto applyComplexTypeToElementRule = [](XmlSchemaSet::ElementRule& elementRule, const XmlSchemaSet::ComplexTypeRule& complexTypeRule) {
        elementRule.allowsText = complexTypeRule.allowsText;
        elementRule.textType = complexTypeRule.textType;
        elementRule.contentModel = complexTypeRule.contentModel;
        elementRule.attributes = complexTypeRule.attributes;
        elementRule.children = complexTypeRule.children;
        elementRule.particle = complexTypeRule.particle;
        elementRule.anyAttributeAllowed = complexTypeRule.anyAttributeAllowed;
        elementRule.anyAttributeNamespaceConstraint = complexTypeRule.anyAttributeNamespaceConstraint;
        elementRule.anyAttributeProcessContents = complexTypeRule.anyAttributeProcessContents;
    };

    const auto applySimpleTypeToElementRule = [](XmlSchemaSet::ElementRule& elementRule, const XmlSchemaSet::SimpleTypeRule& simpleTypeRule) {
        elementRule.declaredSimpleType = simpleTypeRule;
        elementRule.declaredComplexType.reset();
        elementRule.allowsText = true;
        elementRule.textType = simpleTypeRule;
        elementRule.contentModel = XmlSchemaSet::ContentModel::Empty;
        elementRule.attributes.clear();
        elementRule.children.clear();
        elementRule.particle.reset();
        elementRule.anyAttributeAllowed = false;
        elementRule.anyAttributeNamespaceConstraint = "##any";
        elementRule.anyAttributeProcessContents = "strict";
    };

    const auto parseSchemaInstanceTypeName = [&](const SchemaValidationFrame& frame, const std::string& lexicalValue) {
        const auto [prefix, localName] = SplitQualifiedName(lexicalValue);
        try {
            if (prefix.empty()) {
                (void)XmlConvert::VerifyNCName(localName);
                return std::pair<std::string, std::string>{localName, std::string{}};
            }

            (void)XmlConvert::VerifyNCName(prefix);
            (void)XmlConvert::VerifyNCName(localName);
            const std::string namespaceUri = LookupNamespaceUriOnSchemaFrame(frame, prefix);
            if (namespaceUri.empty()) {
                throw XmlException("undefined QName prefix");
            }
            return std::pair<std::string, std::string>{localName, namespaceUri};
        } catch (const XmlException&) {
            throw XmlException(
            "Schema validation failed: xsi:type on element '" + frame.name + "' must be a valid xs:QName value");
        }
    };

    const auto resolveEffectiveElementRule = [&](const SchemaValidationFrame& frame, const XmlSchemaSet::ElementRule& declaration) {
        XmlSchemaSet::ElementRule effectiveRule = declaration;
        const auto* typeAttribute = FindObservedSchemaAttribute(frame.observedAttributes, "type", kSchemaInstanceNamespace);
        if (typeAttribute == nullptr) {
            return effectiveRule;
        }

        const auto [typeLocalName, typeNamespaceUri] = parseSchemaInstanceTypeName(frame, typeAttribute->value);
        const std::string typeDisplayName = typeAttribute->value;

        if (const auto builtinType = resolveBuiltinSimpleType(typeNamespaceUri == "http://www.w3.org/2001/XMLSchema"
                ? "xs:" + typeLocalName
                : std::string{}); builtinType.has_value()) {
            if (declaration.declaredComplexType.has_value()) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }

            bool usesRestriction = false;
            if (declaration.declaredSimpleType.has_value()
                && !runtimeSimpleTypeDerivesFrom(*builtinType, *declaration.declaredSimpleType, usesRestriction)) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }
            if (usesRestriction && declaration.blockRestriction) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' uses blocked restriction derivation");
            }

            applySimpleTypeToElementRule(effectiveRule, *builtinType);
            return effectiveRule;
        }

        if (const auto builtinComplexType = resolveBuiltinComplexType(typeNamespaceUri == "http://www.w3.org/2001/XMLSchema"
                ? "xs:" + typeLocalName
                : std::string{}); builtinComplexType.has_value()) {
            if (declaration.declaredSimpleType.has_value()) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }

            bool usesRestriction = false;
            bool usesExtension = false;
            if (declaration.declaredComplexType.has_value()
                && !runtimeComplexTypeDerivesFrom(*builtinComplexType, *declaration.declaredComplexType, usesRestriction, usesExtension)) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }

            effectiveRule.declaredSimpleType.reset();
            effectiveRule.declaredComplexType = *builtinComplexType;
            applyComplexTypeToElementRule(effectiveRule, *builtinComplexType);
            return effectiveRule;
        }

        if (const auto* namedSimpleType = schemas.FindSimpleTypeRule(typeLocalName, typeNamespaceUri); namedSimpleType != nullptr) {
            if (declaration.declaredComplexType.has_value()) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }

            bool usesRestriction = false;
            if (declaration.declaredSimpleType.has_value()
                && !runtimeSimpleTypeDerivesFrom(*namedSimpleType, *declaration.declaredSimpleType, usesRestriction)) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }
            if (usesRestriction && declaration.blockRestriction) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' uses blocked restriction derivation");
            }

            applySimpleTypeToElementRule(effectiveRule, *namedSimpleType);
            return effectiveRule;
        }

        if (const auto* namedComplexType = schemas.FindComplexTypeRule(typeLocalName, typeNamespaceUri); namedComplexType != nullptr) {
            if (declaration.declaredSimpleType.has_value()) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }

            bool usesRestriction = false;
            bool usesExtension = false;
            if (declaration.declaredComplexType.has_value()
                && !runtimeComplexTypeDerivesFrom(*namedComplexType, *declaration.declaredComplexType, usesRestriction, usesExtension)) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }
            const bool blockedByBaseType = declaration.declaredComplexType.has_value()
                && ((usesRestriction && declaration.declaredComplexType->blockRestriction)
                    || (usesExtension && declaration.declaredComplexType->blockExtension));
            if ((usesRestriction && declaration.blockRestriction)
                || (usesExtension && declaration.blockExtension)
                || blockedByBaseType) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' uses blocked type derivation");
            }

            effectiveRule.declaredSimpleType.reset();
            effectiveRule.declaredComplexType = *namedComplexType;
            applyComplexTypeToElementRule(effectiveRule, *namedComplexType);
            return effectiveRule;
        }

        throw XmlException(
            "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
            + "' is not a supported schema type");
    };

    const auto validateElement = [&](const auto& self, const XmlElement& element) -> void {
        const SchemaValidationFrame frame = BuildSchemaValidationFrame(element);
        const auto* rule = schemas.FindElementRule(frame.localName, frame.namespaceUri);
        if (rule == nullptr) {
            throw XmlException("Schema validation failed: no schema declaration found for element '" + frame.name + "'");
        }
        if (rule->isAbstract) {
            throw XmlException("Schema validation failed: element '" + frame.name + "' is abstract and cannot appear in the document");
        }

        const XmlSchemaSet::ElementRule effectiveRule = resolveEffectiveElementRule(frame, *rule);
        rule = &effectiveRule;

        const auto* nilAttribute = FindObservedSchemaAttribute(frame.observedAttributes, "nil", kSchemaInstanceNamespace);
        bool isNilled = false;
        if (nilAttribute != nullptr) {
            if (!TryParseXmlSchemaBoolean(nilAttribute->value, isNilled)) {
                throw XmlException(
                    "Schema validation failed: xsi:nil on element '" + frame.name + "' must be a valid xs:boolean value");
            }
            if (!rule->isNillable) {
                throw XmlException(
                    "Schema validation failed: element '" + frame.name + "' is not declared nillable and cannot use xsi:nil");
            }
        }

        for (const auto& attributeRule : rule->attributes) {
            const std::optional<std::string> effectiveAttributeValue =
                ResolveSchemaAttributeEffectiveValue(attributeRule, element, frame.observedAttributes);

            if (effectiveAttributeValue.has_value() && attributeRule.type.has_value()) {
                ValidateSchemaSimpleValue(*attributeRule.type, *effectiveAttributeValue,
                    "attribute '" + attributeRule.name + "' on element '" + element.Name() + "'", &element);
                recordIdentityTypedValue(*attributeRule.type, *effectiveAttributeValue,
                    "attribute '" + attributeRule.name + "' on element '" + element.Name() + "'", &element);
            }
        }

        for (const auto& attribute : frame.observedAttributes) {
            if (attribute.name == "xmlns" || attribute.prefix == "xmlns" || attribute.prefix == "xml") {
                continue;
            }
            if (attribute.localName == "nil" && attribute.namespaceUri == kSchemaInstanceNamespace) {
                continue;
            }
            if (attribute.localName == "type" && attribute.namespaceUri == kSchemaInstanceNamespace) {
                continue;
            }
            const bool declared = std::any_of(rule->attributes.begin(), rule->attributes.end(), [&](const auto& attributeRule) {
                return attribute.localName == attributeRule.name && attribute.namespaceUri == attributeRule.namespaceUri;
            });
            const bool wildcardMatches = rule->anyAttributeAllowed
                && matchesWildcardNamespace(rule->anyAttributeNamespaceConstraint, attribute.namespaceUri);
            const bool allowedByWildcard = wildcardMatches
                && wildcardAllowsUndeclared(rule->anyAttributeProcessContents);
            if (!declared && !allowedByWildcard) {
                throw XmlException(
                    "Schema validation failed: attribute '" + attribute.name
                    + "' is not declared for element '" + element.Name() + "'");
            }
        }

        const SchemaElementObservedContent& observedContent = frame.observedContent;
        const auto& elementChildren = observedContent.elementChildren;
        const bool hasSignificantText = observedContent.hasSignificantText;

        if (isNilled && (!elementChildren.empty() || hasSignificantText)) {
            throw XmlException(
                "Schema validation failed: element '" + element.Name() + "' is nilled and must not contain text or child elements");
        }

        const std::optional<std::string> effectiveElementValue =
            ResolveSchemaElementEffectiveValue(*rule, element, observedContent, isNilled);

        if (!isNilled) {
            if (rule->particle.has_value()) {
                const auto matches = CollectObservedParticleMatches(
                    *rule->particle,
                    elementChildren,
                    [&](const std::string& localName, const std::string& namespaceUri) {
                        return schemas.FindElementRule(localName, namespaceUri);
                    },
                    elementMatchesDeclaration,
                    matchesWildcardNamespace,
                    wildcardAllowsUndeclared,
                    [](const auto& particle) { return particle.kind == std::decay_t<decltype(particle)>::Kind::Element; },
                    [](const auto& particle) { return particle.kind == std::decay_t<decltype(particle)>::Kind::Any; },
                    [](const auto& particle) { return particle.kind == std::decay_t<decltype(particle)>::Kind::Sequence; },
                    [](const auto& particle) { return particle.kind == std::decay_t<decltype(particle)>::Kind::All; },
                    [](const auto& particle) -> const auto& { return particle.children; },
                    [](const auto& particle) -> const auto& { return particle.name; },
                    [](const auto& particle) -> const auto& { return particle.namespaceUri; },
                    [](const auto& particle) -> const auto& { return particle.processContents; },
                    [](const auto& particle) { return particle.minOccurs; },
                    [](const auto& particle) { return particle.maxOccurs; },
                    [](const auto& childElement) -> const auto& { return childElement.localName; },
                    [](const auto& childElement) -> const auto& { return childElement.namespaceUri; });
                if (std::find(matches.begin(), matches.end(), elementChildren.size()) == matches.end()) {
                    throw XmlException(
                        "Schema validation failed: child element structure under element '" + element.Name() + "' does not satisfy the declared content model");
                }
            } else if (!rule->children.empty()) {
                if (rule->contentModel == XmlSchemaSet::ContentModel::Choice) {
                    std::optional<std::size_t> selectedChoiceIndex;
                    std::vector<std::size_t> counts(rule->children.size(), 0);
                    for (const auto& childElement : elementChildren) {
                        const auto match = std::find_if(rule->children.begin(), rule->children.end(), [&](const auto& childRule) {
                            const auto* declaredRule = schemas.FindElementRule(childRule.name, childRule.namespaceUri);
                            return declaredRule != nullptr && elementMatchesDeclaration(childElement, *declaredRule);
                        });
                        if (match == rule->children.end()) {
                            throw XmlException(
                                "Schema validation failed: unexpected child element '" + childElement.name
                                + "' under element '" + element.Name() + "'");
                        }

                        const std::size_t choiceIndex = static_cast<std::size_t>(std::distance(rule->children.begin(), match));
                        if (selectedChoiceIndex.has_value() && selectedChoiceIndex.value() != choiceIndex) {
                            throw XmlException(
                                "Schema validation failed: element '" + element.Name() + "' allows only one choice branch at a time");
                        }
                        selectedChoiceIndex = choiceIndex;

                        const auto& choiceRule = rule->children[choiceIndex];
                        ++counts[choiceIndex];
                        if (choiceRule.maxOccurs != std::numeric_limits<std::size_t>::max()
                            && counts[choiceIndex] > choiceRule.maxOccurs) {
                            throw XmlException(
                                "Schema validation failed: child element '" + choiceRule.name
                                + "' exceeded maxOccurs under element '" + element.Name() + "'");
                        }
                    }

                    const auto requiredChoice = std::find_if(rule->children.begin(), rule->children.end(), [](const auto& childRule) {
                        return childRule.minOccurs > 0;
                    });
                    if (!selectedChoiceIndex.has_value()) {
                        if (requiredChoice != rule->children.end()) {
                            throw XmlException(
                                "Schema validation failed: element '" + element.Name() + "' requires one of the declared choice elements");
                        }
                    } else {
                        const auto& selectedRule = rule->children[selectedChoiceIndex.value()];
                        if (counts[selectedChoiceIndex.value()] < selectedRule.minOccurs) {
                            throw XmlException(
                                "Schema validation failed: child element '" + selectedRule.name
                                + "' does not satisfy minOccurs under element '" + element.Name() + "'");
                        }
                    }
                } else {
                    std::size_t childIndex = 0;
                    for (const auto& childRule : rule->children) {
                        std::size_t count = 0;
                        const auto* declaredRule = schemas.FindElementRule(childRule.name, childRule.namespaceUri);
                        if (declaredRule == nullptr) {
                            throw XmlException("Schema validation failed: no schema declaration found for element '" + childRule.name + "'");
                        }
                        while (childIndex < elementChildren.size()) {
                            const auto& childElement = elementChildren[childIndex];
                            if (!elementMatchesDeclaration(childElement, *declaredRule)) {
                                break;
                            }
                            ++count;
                            ++childIndex;
                            if (childRule.maxOccurs != std::numeric_limits<std::size_t>::max() && count == childRule.maxOccurs) {
                                break;
                            }
                        }
                        if (count < childRule.minOccurs) {
                            throw XmlException(
                                "Schema validation failed: expected child element '" + childRule.name
                                + "' under element '" + element.Name() + "'");
                        }
                    }
                    if (childIndex != elementChildren.size()) {
                        throw XmlException(
                            "Schema validation failed: unexpected child element '" + elementChildren[childIndex].name
                            + "' under element '" + element.Name() + "'");
                    }
                }
            } else if (!elementChildren.empty()) {
                throw XmlException(
                    "Schema validation failed: element '" + element.Name() + "' does not allow child elements");
            }

            if (!rule->allowsText && hasSignificantText) {
                throw XmlException(
                    "Schema validation failed: element '" + element.Name() + "' does not allow text content");
            }
            if (rule->textType.has_value() && effectiveElementValue.has_value()) {
                ValidateSchemaSimpleValue(*rule->textType, *effectiveElementValue,
                    "text content of element '" + element.Name() + "'", &element);
                recordIdentityTypedValue(*rule->textType, *effectiveElementValue,
                    "text content of element '" + element.Name() + "'", &element);
            }
        }

        std::function<bool(const XmlSchemaSet::Particle&, const SchemaObservedChildElement&)> explicitParticleMatchesObservedChildElement;
        explicitParticleMatchesObservedChildElement = [&](const XmlSchemaSet::Particle& particle, const SchemaObservedChildElement& childElement) -> bool {
            if (particle.kind == XmlSchemaSet::Particle::Kind::Element) {
                const auto* declaredRule = schemas.FindElementRule(particle.name, particle.namespaceUri);
                return declaredRule != nullptr && elementMatchesDeclaration(childElement, *declaredRule);
            }
            if (particle.kind == XmlSchemaSet::Particle::Kind::Any) {
                return false;
            }
            for (const auto& childParticle : particle.children) {
                if (explicitParticleMatchesObservedChildElement(childParticle, childElement)) {
                    return true;
                }
            }
            return false;
        };

        std::function<const XmlSchemaSet::Particle*(const XmlSchemaSet::Particle&, const SchemaObservedChildElement&)> wildcardParticleForObservedChildElement;
        wildcardParticleForObservedChildElement = [&](const XmlSchemaSet::Particle& particle, const SchemaObservedChildElement& childElement) -> const XmlSchemaSet::Particle* {
            if (particle.kind == XmlSchemaSet::Particle::Kind::Any
                && matchesWildcardNamespace(particle.namespaceUri, childElement.namespaceUri)) {
                return &particle;
            }
            if (particle.kind == XmlSchemaSet::Particle::Kind::Element) {
                return nullptr;
            }
            for (const auto& childParticle : particle.children) {
                if (const auto* found = wildcardParticleForObservedChildElement(childParticle, childElement); found != nullptr) {
                    return found;
                }
            }
            return nullptr;
        };

        const auto shouldValidateObservedChildElement = [&](const SchemaObservedChildElement& childElement) {
            const auto* childRule = schemas.FindElementRule(childElement.localName, childElement.namespaceUri);
            if (childRule == nullptr) {
                return false;
            }

            if (!rule->particle.has_value()) {
                return true;
            }

            if (explicitParticleMatchesObservedChildElement(*rule->particle, childElement)) {
                return true;
            }

            const auto* wildcardParticle = wildcardParticleForObservedChildElement(*rule->particle, childElement);
            if (wildcardParticle == nullptr) {
                return true;
            }

            return !wildcardAllowsValidationSkip(wildcardParticle->processContents);
        };

        for (const auto& childElement : elementChildren) {
            if (shouldValidateObservedChildElement(childElement) && childElement.element != nullptr) {
                self(self, *childElement.element);
            }
        }
    };

    validateElement(validateElement, *root);

    ValidatePendingSchemaIdReferences(identityState);

    if (schemas.HasIdentityConstraints()) {
        ValidateIdentityConstraints(schemas);
    }
}

void XmlDocument::ValidateIdentityConstraints(const XmlSchemaSet& schemas) const {
    const auto root = DocumentElement();
    if (root == nullptr) {
        return;
    }

    const auto evaluateCompiledIdentityConstraintPath = [](const XmlNode& contextNode,
                                                           const auto& path) {
        const auto matchesCompiledIdentityStep = [&](const auto& step,
                                                     const std::string& localName,
                                                     const std::string& namespaceUri) {
            const bool localNameMatches = step.localName == "*" || localName == step.localName;
            const bool namespaceMatches = step.localName == "*" && step.namespaceUri.empty()
                ? true
                : namespaceUri == step.namespaceUri;
            return localNameMatches && namespaceMatches;
        };

        const auto matchesCompiledIdentityPredicate = [&](const auto& step, const XmlNode& node) {
            if (!step.predicateAttributeValue.has_value()) {
                return true;
            }
            if (node.NodeType() != XmlNodeType::Element) {
                return false;
            }

            const auto* element = static_cast<const XmlElement*>(&node);
            const auto attribute = element->GetAttributeNode(step.predicateAttributeLocalName, step.predicateAttributeNamespaceUri);
            return attribute != nullptr && attribute->Value() == *step.predicateAttributeValue;
        };

        const auto appendMatchingDescendants = [&](const auto& self,
                                                   const XmlNode& node,
                                                   const auto& step,
                                                   std::vector<const XmlNode*>& matches) -> void {
            for (const auto& child : node.ChildNodes()) {
                if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                    continue;
                }
                if (matchesCompiledIdentityStep(step, child->LocalName(), child->NamespaceURI())
                    && matchesCompiledIdentityPredicate(step, *child)) {
                    matches.push_back(child.get());
                }
                self(self, *child, step, matches);
            }
        };

        std::vector<const XmlNode*> currentNodes{std::addressof(contextNode)};
        for (const auto& step : path.steps) {
            std::vector<const XmlNode*> nextNodes;
            for (const XmlNode* node : currentNodes) {
                if (node == nullptr) {
                    continue;
                }

                if (step.kind == std::decay_t<decltype(step)>::Kind::Self) {
                    nextNodes.push_back(node);
                    continue;
                }

                if (step.kind == std::decay_t<decltype(step)>::Kind::Element) {
                    for (const auto& child : node->ChildNodes()) {
                        if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                            continue;
                        }
                        if (matchesCompiledIdentityStep(step, child->LocalName(), child->NamespaceURI())
                            && matchesCompiledIdentityPredicate(step, *child)) {
                            nextNodes.push_back(child.get());
                        }
                    }
                    continue;
                }

                if (step.kind == std::decay_t<decltype(step)>::Kind::DescendantElement) {
                    appendMatchingDescendants(appendMatchingDescendants, *node, step, nextNodes);
                    continue;
                }

                if (step.kind == std::decay_t<decltype(step)>::Kind::Attribute && node->NodeType() == XmlNodeType::Element) {
                    const auto* element = static_cast<const XmlElement*>(node);
                    for (const auto& attribute : element->Attributes()) {
                        if (attribute == nullptr) {
                            continue;
                        }
                        if (matchesCompiledIdentityStep(step, attribute->LocalName(), attribute->NamespaceURI())) {
                            nextNodes.push_back(attribute.get());
                        }
                    }
                }
            }
            currentNodes = std::move(nextNodes);
            if (currentNodes.empty()) {
                break;
            }
        }

        return currentNodes;
    };

    const auto evaluateCompiledIdentityConstraintFieldValue = [](const XmlNode& contextNode,
                                                                 const auto& path) {
        IdentityConstraintFieldEvaluationResult result;

        const auto matchesCompiledIdentityStep = [&](const auto& step,
                                                     const std::string& localName,
                                                     const std::string& namespaceUri) {
            const bool localNameMatches = step.localName == "*" || localName == step.localName;
            const bool namespaceMatches = step.localName == "*" && step.namespaceUri.empty()
                ? true
                : namespaceUri == step.namespaceUri;
            return localNameMatches && namespaceMatches;
        };

        const auto matchesCompiledIdentityPredicate = [&](const auto& step, const XmlNode& node) {
            if (!step.predicateAttributeValue.has_value()) {
                return true;
            }
            if (node.NodeType() != XmlNodeType::Element) {
                return false;
            }

            const auto* element = static_cast<const XmlElement*>(&node);
            const auto attribute = element->GetAttributeNode(step.predicateAttributeLocalName, step.predicateAttributeNamespaceUri);
            return attribute != nullptr && attribute->Value() == *step.predicateAttributeValue;
        };

        const auto appendMatchingDescendants = [&](const auto& self,
                                                   const XmlNode& node,
                                                   const auto& step,
                                                   std::vector<const XmlNode*>& matches) -> void {
            for (const auto& child : node.ChildNodes()) {
                if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                    continue;
                }
                if (matchesCompiledIdentityStep(step, child->LocalName(), child->NamespaceURI())
                    && matchesCompiledIdentityPredicate(step, *child)) {
                    matches.push_back(child.get());
                }
                self(self, *child, step, matches);
            }
        };

        std::vector<const XmlNode*> currentNodes{std::addressof(contextNode)};
        for (std::size_t stepIndex = 0; stepIndex < path.steps.size(); ++stepIndex) {
            const auto& step = path.steps[stepIndex];
            const bool isLastStep = stepIndex + 1 == path.steps.size();

            if (isLastStep && step.kind == std::decay_t<decltype(step)>::Kind::Attribute) {
                for (const XmlNode* node : currentNodes) {
                    if (node == nullptr || node->NodeType() != XmlNodeType::Element) {
                        continue;
                    }

                    const auto* element = static_cast<const XmlElement*>(node);
                    for (const auto& attribute : element->Attributes()) {
                        if (attribute == nullptr) {
                            continue;
                        }
                        if (!matchesCompiledIdentityStep(step, attribute->LocalName(), attribute->NamespaceURI())) {
                            continue;
                        }

                        if (result.found) {
                            result.multiple = true;
                            return result;
                        }

                        result.found = true;
                        result.value = attribute->Value();
                    }
                }

                return result;
            }

            std::vector<const XmlNode*> nextNodes;
            for (const XmlNode* node : currentNodes) {
                if (node == nullptr) {
                    continue;
                }

                if (step.kind == std::decay_t<decltype(step)>::Kind::Self) {
                    nextNodes.push_back(node);
                    continue;
                }

                if (step.kind == std::decay_t<decltype(step)>::Kind::Element) {
                    for (const auto& child : node->ChildNodes()) {
                        if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                            continue;
                        }
                        if (matchesCompiledIdentityStep(step, child->LocalName(), child->NamespaceURI())
                            && matchesCompiledIdentityPredicate(step, *child)) {
                            nextNodes.push_back(child.get());
                        }
                    }
                    continue;
                }

                if (step.kind == std::decay_t<decltype(step)>::Kind::DescendantElement) {
                    appendMatchingDescendants(appendMatchingDescendants, *node, step, nextNodes);
                }
            }

            currentNodes = std::move(nextNodes);
            if (currentNodes.empty()) {
                return result;
            }
        }

        for (const XmlNode* node : currentNodes) {
            if (node == nullptr) {
                continue;
            }
            if (result.found) {
                result.multiple = true;
                return result;
            }

            result.found = true;
            result.value = IdentityConstraintFieldStringValue(*node);
        }

        return result;
    };

    struct IdentityConstraintTable {
        const XmlSchemaSet::ElementRule::IdentityConstraint* constraint = nullptr;
        std::unordered_set<std::string> tuples;
    };

    struct IdentityConstraintScope {
        const XmlElement* element = nullptr;
        std::vector<IdentityConstraintTable> tables;
        std::unordered_map<std::string, std::size_t> tableIndexByConstraintKey;
    };

    const auto buildConstraintNamespaces = [](const XmlSchemaSet::ElementRule::IdentityConstraint& constraint) {
        XmlNamespaceManager namespaces;
        for (const auto& [prefix, uri] : constraint.namespaceBindings) {
            namespaces.AddNamespace(prefix, uri);
        }
        return namespaces;
    };

    const auto forEachConstraintTuple = [&](const XmlElement& scopeElement,
                                           const XmlSchemaSet::ElementRule::IdentityConstraint& constraint,
                                           const auto& onTuple) {
        const XmlNamespaceManager namespaces = buildConstraintNamespaces(constraint);
        std::vector<const XmlNode*> selectedNodes;
        if (constraint.compiledSelectorPath.has_value()) {
            selectedNodes = evaluateCompiledIdentityConstraintPath(scopeElement, *constraint.compiledSelectorPath);
        } else {
            const XmlNodeList selectedNodeList = scopeElement.SelectNodes(constraint.selectorXPath, namespaces);
            selectedNodes.reserve(selectedNodeList.Count());
            for (std::size_t selectedIndex = 0; selectedIndex < selectedNodeList.Count(); ++selectedIndex) {
                const auto selectedNode = selectedNodeList.Item(selectedIndex);
                if (selectedNode != nullptr) {
                    selectedNodes.push_back(selectedNode.get());
                }
            }
        }

        for (const XmlNode* selectedNode : selectedNodes) {
            if (selectedNode == nullptr) {
                continue;
            }

            bool skipTuple = false;
            if (constraint.fieldXPaths.size() == 1) {
                const std::string& fieldXPath = constraint.fieldXPaths.front();
                const bool hasCompiledFieldPath = !constraint.compiledFieldPaths.empty()
                    && constraint.compiledFieldPaths.front().has_value();
                std::string fieldValue;
                if (hasCompiledFieldPath) {
                    const auto fieldEvaluation = evaluateCompiledIdentityConstraintFieldValue(
                        *selectedNode,
                        *constraint.compiledFieldPaths.front());
                    if (fieldEvaluation.multiple) {
                        throw XmlException(
                            "Schema validation failed: identity constraint '" + constraint.name
                            + "' field XPath '" + fieldXPath + "' must select at most one node per selector match");
                    }
                    if (!fieldEvaluation.found) {
                        if (constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::Key) {
                            throw XmlException(
                                "Schema validation failed: identity constraint '" + constraint.name
                                + "' requires every selector match to provide field '" + fieldXPath + "'");
                        }
                        continue;
                    }
                    fieldValue = fieldEvaluation.value;
                } else {
                    const XmlNodeList fieldNodeList = selectedNode->SelectNodes(fieldXPath, namespaces);
                    if (fieldNodeList.Count() > 1) {
                        throw XmlException(
                            "Schema validation failed: identity constraint '" + constraint.name
                            + "' field XPath '" + fieldXPath + "' must select at most one node per selector match");
                    }
                    if (fieldNodeList.Count() == 0) {
                        if (constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::Key) {
                            throw XmlException(
                                "Schema validation failed: identity constraint '" + constraint.name
                                + "' requires every selector match to provide field '" + fieldXPath + "'");
                        }
                        continue;
                    }

                    const auto fieldNode = fieldNodeList.Item(0);
                    fieldValue = fieldNode == nullptr ? std::string{} : IdentityConstraintFieldStringValue(*fieldNode);
                }

                if (fieldValue.empty()) {
                    if (constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::Key) {
                        throw XmlException(
                            "Schema validation failed: identity constraint '" + constraint.name
                            + "' requires every selector match to provide a non-empty value for field '" + fieldXPath + "'");
                    }
                    continue;
                }

                onTuple(fieldValue);
                continue;
            }

            std::vector<std::string> fieldValues;
            fieldValues.reserve(constraint.fieldXPaths.size());
            for (std::size_t fieldIndex = 0; fieldIndex < constraint.fieldXPaths.size(); ++fieldIndex) {
                const std::string& fieldXPath = constraint.fieldXPaths[fieldIndex];
                const bool hasCompiledFieldPath = fieldIndex < constraint.compiledFieldPaths.size()
                    && constraint.compiledFieldPaths[fieldIndex].has_value();
                if (hasCompiledFieldPath) {
                    const auto fieldEvaluation = evaluateCompiledIdentityConstraintFieldValue(
                        *selectedNode,
                        *constraint.compiledFieldPaths[fieldIndex]);
                    if (fieldEvaluation.multiple) {
                        throw XmlException(
                            "Schema validation failed: identity constraint '" + constraint.name
                            + "' field XPath '" + fieldXPath + "' must select at most one node per selector match");
                    }
                    if (!fieldEvaluation.found) {
                        if (constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::Key) {
                            throw XmlException(
                                "Schema validation failed: identity constraint '" + constraint.name
                                + "' requires every selector match to provide field '" + fieldXPath + "'");
                        }
                        skipTuple = true;
                        break;
                    }

                    if (fieldEvaluation.value.empty()) {
                        if (constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::Key) {
                            throw XmlException(
                                "Schema validation failed: identity constraint '" + constraint.name
                                + "' requires every selector match to provide a non-empty value for field '" + fieldXPath + "'");
                        }
                        skipTuple = true;
                        break;
                    }

                    fieldValues.push_back(fieldEvaluation.value);
                } else {
                    std::vector<const XmlNode*> fieldNodes;
                    const XmlNodeList fieldNodeList = selectedNode->SelectNodes(fieldXPath, namespaces);
                    fieldNodes.reserve(fieldNodeList.Count());
                    for (std::size_t nodeIndex = 0; nodeIndex < fieldNodeList.Count(); ++nodeIndex) {
                        const auto fieldNode = fieldNodeList.Item(nodeIndex);
                        if (fieldNode != nullptr) {
                            fieldNodes.push_back(fieldNode.get());
                        }
                    }

                    if (fieldNodes.size() > 1) {
                        throw XmlException(
                            "Schema validation failed: identity constraint '" + constraint.name
                            + "' field XPath '" + fieldXPath + "' must select at most one node per selector match");
                    }
                    if (fieldNodes.empty()) {
                        if (constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::Key) {
                            throw XmlException(
                                "Schema validation failed: identity constraint '" + constraint.name
                                + "' requires every selector match to provide field '" + fieldXPath + "'");
                        }
                        skipTuple = true;
                        break;
                    }

                    const std::string fieldValue = IdentityConstraintFieldStringValue(*fieldNodes.front());
                    if (fieldValue.empty()) {
                        if (constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::Key) {
                            throw XmlException(
                                "Schema validation failed: identity constraint '" + constraint.name
                                + "' requires every selector match to provide a non-empty value for field '" + fieldXPath + "'");
                        }
                        skipTuple = true;
                        break;
                    }
                    fieldValues.push_back(fieldValue);
                }
            }

            if (!skipTuple) {
                onTuple(SerializeIdentityConstraintTuple(fieldValues));
            }
        }
    };

    const auto validateIdentityConstraints = [&](const auto& self,
                                                 const XmlElement& element,
                                                 std::vector<IdentityConstraintScope>& scopes) -> void {
        const auto* rule = schemas.FindElementRule(element.LocalName(), element.NamespaceURI());
        if (rule == nullptr) {
            return;
        }

        IdentityConstraintScope currentScope;
        currentScope.element = &element;

        for (const auto& constraint : rule->identityConstraints) {
            if (constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::KeyRef) {
                continue;
            }

            IdentityConstraintTable table;
            table.constraint = &constraint;
            forEachConstraintTuple(element, constraint, [&](const std::string& tuple) {
                if (!table.tuples.insert(tuple).second) {
                    const char* kindLabel = constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::Key
                        ? "key"
                        : "unique";
                    throw XmlException(
                        std::string("Schema validation failed: identity constraint '") + constraint.name
                        + "' violates " + kindLabel + " uniqueness under element '" + element.Name() + "'");
                }
            });
            currentScope.tables.push_back(std::move(table));
            currentScope.tableIndexByConstraintKey.emplace(
                BuildIdentityConstraintLookupKey(constraint.name, constraint.namespaceUri),
                currentScope.tables.size() - 1);
        }

        scopes.push_back(std::move(currentScope));

        for (const auto& constraint : rule->identityConstraints) {
            if (constraint.kind != XmlSchemaSet::ElementRule::IdentityConstraint::Kind::KeyRef) {
                continue;
            }

            const IdentityConstraintTable* referencedTable = nullptr;
            const std::string referencedConstraintKey = BuildIdentityConstraintLookupKey(constraint.referName, constraint.referNamespaceUri);
            for (auto scopeIt = scopes.rbegin(); scopeIt != scopes.rend() && referencedTable == nullptr; ++scopeIt) {
                const auto found = scopeIt->tableIndexByConstraintKey.find(referencedConstraintKey);
                if (found != scopeIt->tableIndexByConstraintKey.end()) {
                    referencedTable = std::addressof(scopeIt->tables[found->second]);
                }
            }

            if (referencedTable == nullptr) {
                throw XmlException(
                    "Schema validation failed: keyref '" + constraint.name
                    + "' could not find an in-scope referenced key/unique constraint");
            }

            forEachConstraintTuple(element, constraint, [&](const std::string& tuple) {
                if (referencedTable->tuples.find(tuple) == referencedTable->tuples.end()) {
                    throw XmlException(
                        "Schema validation failed: keyref '" + constraint.name
                        + "' references a value with no matching key/unique tuple");
                }
            });
        }

        for (const auto& child : element.ChildNodes()) {
            if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                continue;
            }
            self(self, *static_cast<const XmlElement*>(child.get()), scopes);
        }

        scopes.pop_back();
    };

    std::vector<IdentityConstraintScope> identityScopes;
    validateIdentityConstraints(validateIdentityConstraints, *root, identityScopes);
}

bool XmlDocument::PreserveWhitespace() const noexcept {
    return preserveWhitespace_;
}

void XmlDocument::SetPreserveWhitespace(bool value) noexcept {
    preserveWhitespace_ = value;
}

std::string XmlDocument::ToString(const XmlWriterSettings& settings) const {
    return XmlWriter::WriteToString(*this, settings);
}

void XmlDocument::RemoveAll() {
    RemoveAllChildren();
}

std::shared_ptr<XmlDeclaration> XmlDocument::Declaration() const {
    for (const auto& child : ChildNodes()) {
        if (child->NodeType() == XmlNodeType::XmlDeclaration) {
            return std::static_pointer_cast<XmlDeclaration>(child);
        }
    }

    return nullptr;
}

std::shared_ptr<XmlDocumentType> XmlDocument::DocumentType() const {
    for (const auto& child : ChildNodes()) {
        if (child->NodeType() == XmlNodeType::DocumentType) {
            return std::static_pointer_cast<XmlDocumentType>(child);
        }
    }

    return nullptr;
}

std::shared_ptr<XmlElement> XmlDocument::DocumentElement() const {
    for (const auto& child : ChildNodes()) {
        if (child->NodeType() == XmlNodeType::Element) {
            return std::static_pointer_cast<XmlElement>(child);
        }
    }

    return nullptr;
}

std::vector<std::shared_ptr<XmlElement>> XmlDocument::GetElementsByTagName(const std::string& name) const {
    std::vector<std::shared_ptr<XmlElement>> results;
    CollectElementsByName(*this, name, results);
    return results;
}

XmlNodeList XmlDocument::GetElementsByTagNameList(const std::string& name) const {
    std::vector<std::shared_ptr<XmlNode>> nodes;
    for (const auto& element : GetElementsByTagName(name)) {
        nodes.push_back(element);
    }
    return XmlNodeList(std::move(nodes));
}

std::vector<std::shared_ptr<XmlElement>> XmlDocument::GetElementsByTagName(const std::string& localName, const std::string& namespaceUri) const {
    std::vector<std::shared_ptr<XmlElement>> results;
    CollectElementsByNameNS(*this, localName, namespaceUri, results);
    return results;
}

XmlNodeList XmlDocument::GetElementsByTagNameList(const std::string& localName, const std::string& namespaceUri) const {
    std::vector<std::shared_ptr<XmlNode>> nodes;
    for (const auto& element : GetElementsByTagName(localName, namespaceUri)) {
        nodes.push_back(element);
    }
    return XmlNodeList(std::move(nodes));
}

std::shared_ptr<XmlDocumentFragment> XmlDocument::CreateDocumentFragment() const {
    auto node = std::make_shared<XmlDocumentFragment>();
    node->SetOwnerDocument(const_cast<XmlDocument*>(this));
    return node;
}

std::shared_ptr<XmlElement> XmlDocument::CreateElement(const std::string& name) const {
    auto node = std::make_shared<XmlElement>(name);
    node->SetOwnerDocument(const_cast<XmlDocument*>(this));
    return node;
}

std::shared_ptr<XmlElement> XmlDocument::CreateElement(const std::string& prefix, const std::string& localName, const std::string& namespaceUri) const {
    std::string qualifiedName = prefix.empty() ? localName : (prefix + ":" + localName);
    auto node = std::make_shared<XmlElement>(qualifiedName);
    node->SetOwnerDocument(const_cast<XmlDocument*>(this));
    if (!namespaceUri.empty()) {
        std::string nsAttrName = prefix.empty() ? "xmlns" : ("xmlns:" + prefix);
        node->SetAttribute(nsAttrName, namespaceUri);
    }
    return node;
}

std::shared_ptr<XmlAttribute> XmlDocument::CreateAttribute(const std::string& name, const std::string& value) const {
    auto node = std::make_shared<XmlAttribute>(name, value);
    node->SetOwnerDocument(const_cast<XmlDocument*>(this));
    return node;
}

std::shared_ptr<XmlAttribute> XmlDocument::CreateAttribute(const std::string& prefix, const std::string& localName, const std::string& namespaceUri, const std::string& value) const {
    (void)namespaceUri;
    std::string qualifiedName = prefix.empty() ? localName : (prefix + ":" + localName);
    auto node = std::make_shared<XmlAttribute>(qualifiedName, value);
    node->SetOwnerDocument(const_cast<XmlDocument*>(this));
    return node;
}

std::shared_ptr<XmlText> XmlDocument::CreateTextNode(const std::string& value) const {
    auto node = std::make_shared<XmlText>(value);
    node->SetOwnerDocument(const_cast<XmlDocument*>(this));
    return node;
}

std::shared_ptr<XmlEntityReference> XmlDocument::CreateEntityReference(const std::string& name) const {
    if (name.empty()) {
        throw XmlException("CreateEntityReference requires an entity name");
    }

    std::string resolvedValue = ResolvePredefinedEntityReferenceValue(name);
    if (resolvedValue.empty()) {
        if (const auto declared = LookupDocumentInternalEntityDeclaration(this, name); declared.has_value()) {
            std::vector<std::string> resolutionStack{name};
            resolvedValue = DecodeEntityText(
                *declared,
                [this](const std::string& entity) {
                    return LookupDocumentInternalEntityDeclaration(this, entity);
                },
                [](const std::string& message) {
                    throw XmlException(message);
                },
                0,
                &resolutionStack);
        }
    }

    auto node = std::make_shared<XmlEntityReference>(name, resolvedValue);
    node->SetOwnerDocument(const_cast<XmlDocument*>(this));
    return node;
}

std::shared_ptr<XmlWhitespace> XmlDocument::CreateWhitespace(const std::string& value) const {
    auto node = std::make_shared<XmlWhitespace>(value);
    node->SetOwnerDocument(const_cast<XmlDocument*>(this));
    return node;
}

std::shared_ptr<XmlSignificantWhitespace> XmlDocument::CreateSignificantWhitespace(const std::string& value) const {
    auto node = std::make_shared<XmlSignificantWhitespace>(value);
    node->SetOwnerDocument(const_cast<XmlDocument*>(this));
    return node;
}

std::shared_ptr<XmlCDataSection> XmlDocument::CreateCDataSection(const std::string& value) const {
    auto node = std::make_shared<XmlCDataSection>(value);
    node->SetOwnerDocument(const_cast<XmlDocument*>(this));
    return node;
}

std::shared_ptr<XmlComment> XmlDocument::CreateComment(const std::string& value) const {
    auto node = std::make_shared<XmlComment>(value);
    node->SetOwnerDocument(const_cast<XmlDocument*>(this));
    return node;
}

std::shared_ptr<XmlProcessingInstruction> XmlDocument::CreateProcessingInstruction(const std::string& target, const std::string& data) const {
    auto node = std::make_shared<XmlProcessingInstruction>(target, data);
    node->SetOwnerDocument(const_cast<XmlDocument*>(this));
    return node;
}

std::shared_ptr<XmlDeclaration> XmlDocument::CreateXmlDeclaration(
    const std::string& version,
    const std::string& encoding,
    const std::string& standalone) const {
    auto node = std::make_shared<XmlDeclaration>(version, encoding, standalone);
    node->SetOwnerDocument(const_cast<XmlDocument*>(this));
    return node;
}

std::shared_ptr<XmlDocumentType> XmlDocument::CreateDocumentType(
    const std::string& name,
    const std::string& publicId,
    const std::string& systemId,
    const std::string& internalSubset) const {
    auto node = std::make_shared<XmlDocumentType>(name, publicId, systemId, internalSubset);
    node->SetOwnerDocument(const_cast<XmlDocument*>(this));
    return node;
}

std::shared_ptr<XmlNode> XmlDocument::CreateNode(
    XmlNodeType nodeType,
    const std::string& name,
    const std::string& value) const {
    switch (nodeType) {
    case XmlNodeType::Element:
        if (name.empty()) {
            throw XmlException("CreateNode requires a qualified name for element nodes");
        }
        return CreateElement(name);
    case XmlNodeType::Attribute:
        if (name.empty()) {
            throw XmlException("CreateNode requires a qualified name for attribute nodes");
        }
        return CreateAttribute(name, value);
    case XmlNodeType::Text:
        return CreateTextNode(value);
    case XmlNodeType::EntityReference:
        return CreateEntityReference(name);
    case XmlNodeType::CDATA:
        return CreateCDataSection(value);
    case XmlNodeType::Comment:
        return CreateComment(value);
    case XmlNodeType::Whitespace:
        return CreateWhitespace(value);
    case XmlNodeType::SignificantWhitespace:
        return CreateSignificantWhitespace(value);
    case XmlNodeType::ProcessingInstruction:
        if (name.empty()) {
            throw XmlException("CreateNode requires a target name for processing instruction nodes");
        }
        return CreateProcessingInstruction(name, value);
    case XmlNodeType::DocumentFragment:
        return CreateDocumentFragment();
    default:
        throw XmlException("CreateNode does not support the specified node type");
    }
}

std::shared_ptr<XmlNode> XmlDocument::ImportNode(const XmlNode& node, bool deep) const {
    if (node.NodeType() == XmlNodeType::Document) {
        throw XmlException("ImportNode does not support document nodes");
    }

    auto imported = node.CloneNode(deep);
    imported->SetOwnerDocumentRecursive(const_cast<XmlDocument*>(this));
    return imported;
}

std::shared_ptr<XmlNode> XmlDocument::AppendChild(const std::shared_ptr<XmlNode>& child) {
    ValidateChildInsertion(child);
    return XmlNode::AppendChild(child);
}

void XmlDocument::SetNodeInserting(XmlNodeChangedEventHandler handler) {
    onNodeInserting_ = std::move(handler);
}

void XmlDocument::SetNodeInserted(XmlNodeChangedEventHandler handler) {
    onNodeInserted_ = std::move(handler);
}

void XmlDocument::SetNodeRemoving(XmlNodeChangedEventHandler handler) {
    onNodeRemoving_ = std::move(handler);
}

void XmlDocument::SetNodeRemoved(XmlNodeChangedEventHandler handler) {
    onNodeRemoved_ = std::move(handler);
}

void XmlDocument::SetNodeChanging(XmlNodeChangedEventHandler handler) {
    onNodeChanging_ = std::move(handler);
}

void XmlDocument::SetNodeChanged(XmlNodeChangedEventHandler handler) {
    onNodeChanged_ = std::move(handler);
}

bool XmlDocument::HasNodeChangeHandlers() const noexcept {
    return static_cast<bool>(onNodeInserting_)
        || static_cast<bool>(onNodeInserted_)
        || static_cast<bool>(onNodeRemoving_)
        || static_cast<bool>(onNodeRemoved_)
        || static_cast<bool>(onNodeChanging_)
        || static_cast<bool>(onNodeChanged_);
}

void XmlDocument::FireNodeInserting(XmlNode* node, XmlNode* newParent) const {
    if (!onNodeInserting_) {
        return;
    }
    onNodeInserting_({XmlNodeChangedAction::Insert, node, node != nullptr ? node->ParentNode() : nullptr, newParent, {}, {}});
}

void XmlDocument::FireNodeInserted(XmlNode* node, XmlNode* newParent) const {
    if (!onNodeInserted_) {
        return;
    }
    onNodeInserted_({XmlNodeChangedAction::Insert, node, nullptr, newParent, {}, {}});
}

void XmlDocument::FireNodeRemoving(XmlNode* node, XmlNode* oldParent) const {
    if (!onNodeRemoving_) {
        return;
    }
    onNodeRemoving_({XmlNodeChangedAction::Remove, node, oldParent, nullptr, {}, {}});
}

void XmlDocument::FireNodeRemoved(XmlNode* node, XmlNode* oldParent) const {
    if (!onNodeRemoved_) {
        return;
    }
    onNodeRemoved_({XmlNodeChangedAction::Remove, node, oldParent, nullptr, {}, {}});
}

void XmlDocument::FireNodeChanging(XmlNode* node, const std::string& oldValue, const std::string& newValue) const {
    if (!onNodeChanging_) {
        return;
    }
    onNodeChanging_({XmlNodeChangedAction::Change, node, node != nullptr ? node->ParentNode() : nullptr, node != nullptr ? node->ParentNode() : nullptr, oldValue, newValue});
}

void XmlDocument::FireNodeChanged(XmlNode* node, const std::string& oldValue, const std::string& newValue) const {
    if (!onNodeChanged_) {
        return;
    }
    onNodeChanged_({XmlNodeChangedAction::Change, node, node != nullptr ? node->ParentNode() : nullptr, node != nullptr ? node->ParentNode() : nullptr, oldValue, newValue});
}

void XmlDocument::ValidateChildInsertion(const std::shared_ptr<XmlNode>& child, const XmlNode* replacingChild) const {
    XmlNode::ValidateChildInsertion(child, replacingChild);

    if (!child) {
        return;
    }

    auto countNodeType = [this, replacingChild](XmlNodeType nodeType) {
        std::size_t count = 0;
        for (const auto& existingChild : ChildNodes()) {
            if (existingChild.get() == replacingChild) {
                continue;
            }
            if (existingChild->NodeType() == nodeType) {
                ++count;
            }
        }
        return count;
    };

    if (child->NodeType() == XmlNodeType::DocumentFragment) {
        std::size_t elementCount = countNodeType(XmlNodeType::Element);
        std::size_t declarationCount = countNodeType(XmlNodeType::XmlDeclaration);
        std::size_t documentTypeCount = countNodeType(XmlNodeType::DocumentType);

        for (const auto& fragmentChild : child->ChildNodes()) {
            if (fragmentChild->NodeType() == XmlNodeType::Element && ++elementCount > 1) {
                throw XmlException("XML document can only contain a single root element");
            }
            if (fragmentChild->NodeType() == XmlNodeType::XmlDeclaration && ++declarationCount > 1) {
                throw XmlException("XML document can only contain a single XML declaration");
            }
            if (fragmentChild->NodeType() == XmlNodeType::DocumentType && ++documentTypeCount > 1) {
                throw XmlException("XML document can only contain a single DOCTYPE declaration");
            }
        }
        return;
    }

    if (child->NodeType() == XmlNodeType::Element && countNodeType(XmlNodeType::Element) > 0) {
        throw XmlException("XML document can only contain a single root element");
    }
    if (child->NodeType() == XmlNodeType::XmlDeclaration && countNodeType(XmlNodeType::XmlDeclaration) > 0) {
        throw XmlException("XML document can only contain a single XML declaration");
    }
    if (child->NodeType() == XmlNodeType::DocumentType && countNodeType(XmlNodeType::DocumentType) > 0) {
        throw XmlException("XML document can only contain a single DOCTYPE declaration");
    }
}

XmlNamespaceManager::XmlNamespaceManager() {
    scopes_.push_back({});
    scopes_.back().emplace("xml", "http://www.w3.org/XML/1998/namespace");
    scopes_.back().emplace("xmlns", "http://www.w3.org/2000/xmlns/");
}

void XmlNamespaceManager::PushScope() {
    scopes_.push_back({});
}

bool XmlNamespaceManager::PopScope() {
    if (scopes_.size() <= 1) {
        return false;
    }

    scopes_.pop_back();
    return true;
}

void XmlNamespaceManager::AddNamespace(const std::string& prefix, const std::string& uri) {
    scopes_.back()[prefix] = uri;
}

std::string XmlNamespaceManager::LookupNamespace(const std::string& prefix) const {
    if (prefix == "xml") {
        return "http://www.w3.org/XML/1998/namespace";
    }
    if (prefix == "xmlns") {
        return "http://www.w3.org/2000/xmlns/";
    }

    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->find(prefix);
        if (found != it->end()) {
            return found->second;
        }
    }

    return {};
}

std::string XmlNamespaceManager::LookupPrefix(const std::string& uri) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = std::find_if(it->begin(), it->end(), [&uri](const auto& pair) {
            return pair.second == uri;
        });
        if (found != it->end()) {
            return found->first;
        }
    }

    return {};
}

bool XmlNamespaceManager::HasNamespace(const std::string& prefix) const {
    return !LookupNamespace(prefix).empty();
}

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

class StreamXmlReaderInputSource final : public XmlReaderInputSource {
public:
    ~StreamXmlReaderInputSource() override {
        CloseReplayFile();
        RemoveReplayFile();
    }

    explicit StreamXmlReaderInputSource(std::istream& stream)
        : stream_(&stream) {
    }

    StreamXmlReaderInputSource(std::istream& stream, std::string initialBuffer)
        : stream_(&stream),
          buffer_(std::move(initialBuffer)),
          eof_(stream_ == nullptr || !stream_->good()) {
    }

    explicit StreamXmlReaderInputSource(std::shared_ptr<std::istream> stream)
        : ownedStream_(std::move(stream)),
          stream_(ownedStream_.get()) {
    }

    StreamXmlReaderInputSource(std::shared_ptr<std::istream> stream, std::string initialBuffer)
        : ownedStream_(std::move(stream)),
          stream_(ownedStream_.get()),
          buffer_(std::move(initialBuffer)),
          eof_(stream_ == nullptr || !stream_->good()) {
    }

    char CharAt(std::size_t position) const noexcept override {
        EnsureBufferedThrough(position);
        if (position >= bufferStartOffset_) {
            const auto bufferIndex = bufferLogicalStart_ + (position - bufferStartOffset_);
            return bufferIndex < buffer_.size() ? buffer_[bufferIndex] : '\0';
        }
        return ReadReplayCharAt(position);
    }

    const char* PtrAt(std::size_t position, std::size_t& available) const noexcept override {
        available = 0;
        EnsureBufferedThrough(position);
        if (position >= bufferStartOffset_) {
            const auto bufferIndex = bufferLogicalStart_ + (position - bufferStartOffset_);
            if (bufferIndex < buffer_.size()) {
                available = buffer_.size() - bufferIndex;
                return buffer_.data() + static_cast<std::ptrdiff_t>(bufferIndex);
            }
            return nullptr;
        }
        return ReadReplayPtrAt(position, available);
    }

    std::size_t Find(const std::string& token, std::size_t position) const noexcept override {
        if (token.empty()) {
            return position;
        }

        const bool singleToken = token.size() == 1;
        const char singleTokenChar = singleToken ? token.front() : '\0';

        if (position < bufferStartOffset_) {
            const std::size_t replaySearchEnd = (std::min)(bufferStartOffset_, replayLength_);
            const std::size_t replayFound = singleToken
                ? FindFirstOfInReplay(std::string_view(&singleTokenChar, 1), position, replaySearchEnd)
                : FindInReplay(token, position, replaySearchEnd);
            if (replayFound != std::string::npos) {
                return replayFound;
            }

            if (token.size() > 1 && bufferStartOffset_ != 0) {
                const std::size_t boundaryStart = bufferStartOffset_ >= token.size() - 1
                    ? (std::max)(position, bufferStartOffset_ - (token.size() - 1))
                    : position;
                const std::size_t boundaryEnd = BufferedEndOffset();
                for (std::size_t cursor = boundaryStart; cursor < bufferStartOffset_; ++cursor) {
                    if (cursor > boundaryEnd || token.size() > boundaryEnd - cursor) {
                        break;
                    }

                    bool matched = true;
                    for (std::size_t index = 0; index < token.size(); ++index) {
                        if (CharAt(cursor + index) != token[index]) {
                            matched = false;
                            break;
                        }
                    }
                    if (matched) {
                        return cursor;
                    }
                }
            }

            position = bufferStartOffset_;
        }

        EnsureBufferedThrough(position);
        std::size_t searchPosition = position >= bufferStartOffset_
            ? bufferLogicalStart_ + (position - bufferStartOffset_)
            : bufferLogicalStart_;
        while (true) {
            const auto found = singleToken
                ? buffer_.find(singleTokenChar, searchPosition)
                : buffer_.find(token, searchPosition);
            if (found != std::string::npos) {
                return bufferStartOffset_ + (found - bufferLogicalStart_);
            }
            if (eof_) {
                return std::string::npos;
            }

            const auto sizeBefore = buffer_.size();
            ReadMore();
            if (buffer_.size() == sizeBefore) {
                return std::string::npos;
            }

            if (token.size() > 1 && sizeBefore >= token.size() - 1) {
                searchPosition = (std::max)(searchPosition, sizeBefore - (token.size() - 1));
            } else {
                searchPosition = (std::max)(searchPosition, sizeBefore);
            }
        }
    }

    std::size_t FindFirstOf(std::string_view tokens, std::size_t position) const noexcept {
        if (tokens.empty()) {
            return position;
        }

        if (position < bufferStartOffset_) {
            const std::size_t replaySearchEnd = (std::min)(bufferStartOffset_, replayLength_);
            const std::size_t replayFound = FindFirstOfInReplay(tokens, position, replaySearchEnd);
            if (replayFound != std::string::npos) {
                return replayFound;
            }
            position = bufferStartOffset_;
        }

        EnsureBufferedThrough(position);
        std::size_t searchPosition = position >= bufferStartOffset_
            ? bufferLogicalStart_ + (position - bufferStartOffset_)
            : bufferLogicalStart_;
        while (true) {
            const auto found = buffer_.find_first_of(tokens, searchPosition);
            if (found != std::string::npos) {
                return bufferStartOffset_ + (found - bufferLogicalStart_);
            }
            if (eof_) {
                return std::string::npos;
            }

            const auto sizeBefore = buffer_.size();
            ReadMore();
            if (buffer_.size() == sizeBefore) {
                return std::string::npos;
            }

            searchPosition = (std::max)(searchPosition, sizeBefore);
        }
    }

    std::size_t FindNextTextSpecial(std::size_t position) const noexcept {
        if (position < bufferStartOffset_) {
            const std::size_t replaySearchEnd = (std::min)(bufferStartOffset_, replayLength_);
            const std::size_t replayFound = FindFirstOfInReplay("<&", position, replaySearchEnd);
            if (replayFound != std::string::npos) {
                return replayFound;
            }
            position = bufferStartOffset_;
        }

        EnsureBufferedThrough(position);
        std::size_t searchPosition = position >= bufferStartOffset_
            ? bufferLogicalStart_ + (position - bufferStartOffset_)
            : bufferLogicalStart_;
        while (true) {
            const auto found = buffer_.find_first_of("<&", searchPosition);
            if (found != std::string::npos) {
                return bufferStartOffset_ + (found - bufferLogicalStart_);
            }
            if (eof_) {
                return std::string::npos;
            }

            const auto sizeBefore = buffer_.size();
            ReadMore();
            if (buffer_.size() == sizeBefore) {
                return std::string::npos;
            }

            searchPosition = (std::max)(searchPosition, sizeBefore);
        }
    }

    std::size_t ScanQuotedValueEnd(std::size_t quoteStart) const noexcept {
        const char quote = CharAt(quoteStart);
        if (quote != '"' && quote != '\'') {
            return std::string::npos;
        }

        std::size_t scanPosition = quoteStart + 1;
        while (true) {
            EnsureBufferedThrough(scanPosition);
            if (scanPosition < bufferStartOffset_) {
                break;
            }

            const std::size_t bufferIndex = bufferLogicalStart_ + (scanPosition - bufferStartOffset_);
            if (bufferIndex < buffer_.size()) {
                const std::size_t offset = FindQuoteOrNulInBuffer(
                    buffer_.data() + bufferIndex,
                    buffer_.size() - bufferIndex,
                    quote);
                if (offset != std::string::npos) {
                    const std::size_t match = scanPosition + offset;
                    return CharAt(match) == quote ? match : std::string::npos;
                }
            }

            if (eof_) {
                return std::string::npos;
            }

            const std::size_t previousEnd = BufferedEndOffset();
            ReadMore();
            if (BufferedEndOffset() == previousEnd) {
                return std::string::npos;
            }
            scanPosition = previousEnd;
        }

        return ScanQuotedValueEndAt(quoteStart, [this](std::size_t probe) noexcept {
            return CharAt(probe);
        });
    }


    std::string Slice(std::size_t start, std::size_t count) const override {
        std::string result;
        AppendSliceTo(result, start, count);
        return result;
    }

    void AppendSliceTo(std::string& target, std::size_t start, std::size_t count) const override {
        if (count == std::string::npos) {
            EnsureAllBuffered();
        } else if (count != 0 && start <= (std::numeric_limits<std::size_t>::max)() - (count - 1)) {
            EnsureBufferedThrough(start + count - 1);
        } else {
            EnsureBufferedThrough(start);
        }

        const std::size_t bufferedEnd = BufferedEndOffset();
        if (start >= bufferedEnd && !(HasReplayFile() && start < replayLength_)) {
            return;
        }

        const std::size_t requestedEnd = count == std::string::npos
            ? bufferedEnd
            : start + (std::min)(count, bufferedEnd > start ? bufferedEnd - start : 0);
        const std::size_t actualEnd = count == std::string::npos
            ? bufferedEnd
            : (std::min)(requestedEnd, bufferedEnd);

        if (start < bufferStartOffset_) {
            const std::size_t replayEnd = (std::min)(actualEnd, bufferStartOffset_);
            if (replayEnd > start) {
                AppendReplaySliceTo(target, start, replayEnd - start);
            }
        }

        if (actualEnd > bufferStartOffset_) {
            const std::size_t memoryStart = start < bufferStartOffset_ ? bufferStartOffset_ : start;
            const std::size_t memoryCount = actualEnd - memoryStart;
            if (memoryCount != 0) {
                target.append(buffer_, bufferLogicalStart_ + (memoryStart - bufferStartOffset_), memoryCount);
            }
        }
    }

private:
    static constexpr std::size_t ChunkSize = 64 * 1024;
    static constexpr std::size_t BufferCompactionThreshold = 256 * 1024;
    static constexpr std::size_t ReplayIoBufferSize = 256 * 1024;

    std::size_t ActiveBufferSize() const noexcept {
        return buffer_.size() >= bufferLogicalStart_ ? buffer_.size() - bufferLogicalStart_ : 0;
    }

    std::size_t BufferedEndOffset() const noexcept {
        return bufferStartOffset_ + ActiveBufferSize();
    }

    void MaybeCompactBuffer() const {
        if (bufferLogicalStart_ == 0) {
            return;
        }

        const std::size_t activeSize = ActiveBufferSize();
        if (bufferLogicalStart_ < BufferCompactionThreshold && bufferLogicalStart_ < activeSize) {
            return;
        }

        buffer_.erase(0, bufferLogicalStart_);
        bufferLogicalStart_ = 0;
    }

    void CloseOwnedStreamIfPossible() const noexcept {
        if (ownedStream_ == nullptr) {
            return;
        }

        if (auto* fileStream = dynamic_cast<std::ifstream*>(ownedStream_.get()); fileStream != nullptr) {
            fileStream->close();
        }
        stream_ = nullptr;
    }

    void CloseReplayFile() const noexcept {
        if (replayFile_ != nullptr && replayFile_->is_open()) {
            replayFile_->close();
        }
    }

    void RemoveReplayFile() const noexcept {
        if (replayPath_.empty()) {
            return;
        }
        std::error_code error;
        std::filesystem::remove(replayPath_, error);
        replayPath_.clear();
    }

    bool HasReplayFile() const noexcept {
        return replayFile_ != nullptr && replayFile_->is_open();
    }

    void EnableReplay() const override {
        replayEnabled_ = true;
        if (!HasReplayFile()) {
            EnsureReplayFile();
        }
    }

    void DiscardBefore(std::size_t position) const override {
        if (position <= bufferStartOffset_) {
            return;
        }

        const std::size_t bufferEnd = BufferedEndOffset();
        const std::size_t discardEnd = (std::min)(position, bufferEnd);
        if (discardEnd <= bufferStartOffset_) {
            return;
        }

        if (replayEnabled_ && !HasReplayFile()) {
            EnsureReplayFile();
        }

        const std::size_t discardCount = discardEnd - bufferStartOffset_;
        bufferStartOffset_ = discardEnd;
        bufferLogicalStart_ += discardCount;
        MaybeCompactBuffer();
    }

    void EnsureReplayFile() const {
        if (HasReplayFile()) {
            return;
        }

        replayPath_ = CreateTemporaryXmlReplayPath();
        replayFile_ = std::make_unique<std::fstream>(
            replayPath_,
            std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (replayFile_ == nullptr || !replayFile_->is_open()) {
            RemoveReplayFile();
            throw XmlException("Failed to create stream replay file");
        }

        replayIoBuffer_.assign(ReplayIoBufferSize, '\0');
        replayFile_->rdbuf()->pubsetbuf(replayIoBuffer_.data(), static_cast<std::streamsize>(replayIoBuffer_.size()));

        const std::size_t activeSize = ActiveBufferSize();
        if (activeSize != 0) {
            replayFile_->seekp(static_cast<std::streamoff>(bufferStartOffset_), std::ios::beg);
            replayFile_->write(
                buffer_.data() + static_cast<std::ptrdiff_t>(bufferLogicalStart_),
                static_cast<std::streamsize>(activeSize));
            if (!*replayFile_) {
                CloseReplayFile();
                RemoveReplayFile();
                replayFile_.reset();
                throw XmlException("Failed to seed stream replay file");
            }
        }
        replayLength_ = bufferStartOffset_ + activeSize;
        replayFile_->flush();
    }

    char ReadReplayCharAt(std::size_t position) const noexcept {
        std::size_t available = 0;
        if (const char* ptr = ReadReplayPtrAt(position, available); ptr != nullptr) {
            return *ptr;
        }
        return '\0';
    }

    const char* ReadReplayPtrAt(std::size_t position, std::size_t& available) const noexcept {
        available = 0;
        if (!HasReplayFile() || position >= replayLength_) {
            return nullptr;
        }

        if (position < replayWindowStart_ || position >= replayWindowStart_ + replayWindow_.size()) {
            replayWindowStart_ = position;
            const std::size_t remaining = replayLength_ - position;
            const std::size_t windowSize = (std::min)(ChunkSize, remaining);
            replayWindow_.assign(windowSize, '\0');

            replayFile_->clear();
            replayFile_->seekg(static_cast<std::streamoff>(position), std::ios::beg);
            replayFile_->read(replayWindow_.data(), static_cast<std::streamsize>(windowSize));
            const auto bytesRead = replayFile_->gcount();
            if (bytesRead <= 0) {
                replayFile_->clear();
                replayWindow_.clear();
                return nullptr;
            }

            replayWindow_.resize(static_cast<std::size_t>(bytesRead));
            replayFile_->clear();
        }

        const std::size_t windowIndex = position - replayWindowStart_;
        if (windowIndex >= replayWindow_.size()) {
            return nullptr;
        }

        available = replayWindow_.size() - windowIndex;
        return replayWindow_.data() + static_cast<std::ptrdiff_t>(windowIndex);
    }

    void AppendReplaySliceTo(std::string& target, std::size_t start, std::size_t count) const {
        if (!HasReplayFile() || count == 0 || start >= replayLength_) {
            return;
        }

        const std::size_t actualCount = (std::min)(count, replayLength_ - start);
        const std::size_t originalSize = target.size();
        target.resize(originalSize + actualCount);
        replayFile_->clear();
        std::streambuf* replayBuffer = replayFile_->rdbuf();
        replayBuffer->pubseekpos(static_cast<std::streampos>(start), std::ios::in);
        const auto bytesRead = replayBuffer->sgetn(
            target.data() + static_cast<std::ptrdiff_t>(originalSize),
            static_cast<std::streamsize>(actualCount));
        replayFile_->clear();
        if (bytesRead > 0) {
            target.resize(originalSize + static_cast<std::size_t>(bytesRead));
        } else {
            target.resize(originalSize);
        }
    }

    std::size_t FindInReplay(const std::string& token, std::size_t start, std::size_t end) const noexcept {
        if (!HasReplayFile() || start >= end) {
            return std::string::npos;
        }

        const std::size_t overlap = token.size() > 1 ? token.size() - 1 : 0;
        std::size_t cursor = start;
        std::string carry;

        while (cursor < end) {
            const std::size_t remaining = end - cursor;
            const std::size_t chunkSize = (std::min)(ChunkSize, remaining);
            std::string block = carry;
            const std::size_t blockPrefixSize = carry.size();

            replayFile_->clear();
            replayFile_->seekg(static_cast<std::streamoff>(cursor), std::ios::beg);
            const std::size_t blockOriginalSize = block.size();
            block.resize(blockOriginalSize + chunkSize);
            replayFile_->read(block.data() + static_cast<std::ptrdiff_t>(blockOriginalSize), static_cast<std::streamsize>(chunkSize));
            const auto bytesRead = replayFile_->gcount();
            replayFile_->clear();
            if (bytesRead <= 0) {
                return std::string::npos;
            }
            block.resize(blockOriginalSize + static_cast<std::size_t>(bytesRead));

            const auto found = block.find(token);
            if (found != std::string::npos) {
                const std::size_t matchStart = cursor - blockPrefixSize + found;
                if (matchStart + token.size() <= end) {
                    return matchStart;
                }
            }

            if (overlap != 0) {
                const std::size_t carrySize = (std::min)(overlap, block.size());
                carry.assign(block, block.size() - carrySize, carrySize);
            }
            cursor += static_cast<std::size_t>(bytesRead);
        }

        return std::string::npos;
    }

    std::size_t FindFirstOfInReplay(std::string_view tokens, std::size_t start, std::size_t end) const noexcept {
        if (!HasReplayFile() || start >= end || tokens.empty()) {
            return std::string::npos;
        }

        std::size_t cursor = start;
        const bool textSpecialTokens = tokens == "<&";
        const bool textLineSpecialTokens = tokens == "\n<&";
        const bool singleToken = tokens.size() == 1;
        const char singleTokenChar = singleToken ? tokens.front() : '\0';
        while (cursor < end) {
            const std::size_t remaining = end - cursor;
            const std::size_t chunkSize = (std::min)(ChunkSize, remaining);
            std::string block(chunkSize, '\0');

            replayFile_->clear();
            replayFile_->seekg(static_cast<std::streamoff>(cursor), std::ios::beg);
            replayFile_->read(block.data(), static_cast<std::streamsize>(chunkSize));
            const auto bytesRead = replayFile_->gcount();
            replayFile_->clear();
            if (bytesRead <= 0) {
                return std::string::npos;
            }

            if (textSpecialTokens) {
                for (std::size_t index = 0; index < static_cast<std::size_t>(bytesRead); ++index) {
                    const char ch = block[index];
                    if (ch == '<' || ch == '&') {
                        return cursor + index;
                    }
                }
            } else if (textLineSpecialTokens) {
                for (std::size_t index = 0; index < static_cast<std::size_t>(bytesRead); ++index) {
                    const char ch = block[index];
                    if (ch == '\n' || ch == '<' || ch == '&') {
                        return cursor + index;
                    }
                }
            } else if (singleToken) {
                for (std::size_t index = 0; index < static_cast<std::size_t>(bytesRead); ++index) {
                    if (block[index] == singleTokenChar) {
                        return cursor + index;
                    }
                }
            } else {
                for (std::size_t index = 0; index < static_cast<std::size_t>(bytesRead); ++index) {
                    if (tokens.find(block[index]) != std::string_view::npos) {
                        return cursor + index;
                    }
                }
            }

            cursor += static_cast<std::size_t>(bytesRead);
        }

        return std::string::npos;
    }


    void EnsureBufferedThrough(std::size_t position) const noexcept {
        while (!eof_ && BufferedEndOffset() <= position) {
            ReadMore();
        }
    }

    void EnsureAllBuffered() const noexcept {
        while (!eof_) {
            ReadMore();
        }
    }

    void ReadMore() const noexcept {
        if (stream_ == nullptr || eof_) {
            eof_ = true;
            return;
        }

        char chunk[ChunkSize];
        stream_->read(chunk, static_cast<std::streamsize>(ChunkSize));
        const auto bytesRead = stream_->gcount();
        if (bytesRead > 0) {
            const std::size_t byteCount = static_cast<std::size_t>(bytesRead);
            buffer_.append(chunk, byteCount);
            if (replayEnabled_ && !HasReplayFile()) {
                try {
                    EnsureReplayFile();
                } catch (...) {
                    eof_ = true;
                    CloseOwnedStreamIfPossible();
                    return;
                }
            }
            if (replayEnabled_ && HasReplayFile()) {
                replayFile_->clear();
                replayFile_->seekp(static_cast<std::streamoff>(replayLength_), std::ios::beg);
                replayFile_->write(chunk, bytesRead);
                replayFile_->flush();
                if (!*replayFile_) {
                    eof_ = true;
                    CloseOwnedStreamIfPossible();
                    return;
                }
                replayLength_ += byteCount;
            }
        }
        if (bytesRead < static_cast<std::streamsize>(ChunkSize) || !stream_->good()) {
            eof_ = true;
            CloseOwnedStreamIfPossible();
        }
    }

    mutable std::shared_ptr<std::istream> ownedStream_;
    mutable std::istream* stream_ = nullptr;
    mutable std::string buffer_;
    mutable std::size_t bufferStartOffset_ = 0;
    mutable std::size_t bufferLogicalStart_ = 0;
    mutable std::unique_ptr<std::fstream> replayFile_;
    mutable std::filesystem::path replayPath_;
    mutable std::vector<char> replayIoBuffer_;
    mutable std::size_t replayLength_ = 0;
    mutable std::size_t replayWindowStart_ = 0;
    mutable std::string replayWindow_;
    mutable bool replayEnabled_ = false;
    mutable bool eof_ = false;
};

class SubrangeXmlReaderInputSource final : public XmlReaderInputSource {
public:
    SubrangeXmlReaderInputSource(std::shared_ptr<const XmlReaderInputSource> inputSource, std::size_t start, std::size_t length)
        : inputSource_(std::move(inputSource)),
          start_(start),
          length_(length) {
    }

    char CharAt(std::size_t position) const noexcept override {
        if (inputSource_ == nullptr || position >= length_) {
            return '\0';
        }
        return inputSource_->CharAt(start_ + position);
    }

    const char* PtrAt(std::size_t position, std::size_t& available) const noexcept override {
        available = 0;
        if (inputSource_ == nullptr || position >= length_) {
            return nullptr;
        }

        std::size_t sourceAvailable = 0;
        const char* ptr = inputSource_->PtrAt(start_ + position, sourceAvailable);
        if (ptr == nullptr) {
            return nullptr;
        }

        available = (std::min)(sourceAvailable, length_ - position);
        return available == 0 ? nullptr : ptr;
    }

    std::size_t Find(const std::string& token, std::size_t position) const noexcept override {
        if (inputSource_ == nullptr) {
            return std::string::npos;
        }
        if (token.empty()) {
            return position <= length_ ? position : std::string::npos;
        }
        if (position >= length_) {
            return std::string::npos;
        }

        const std::size_t found = inputSource_->Find(token, start_ + position);
        if (found == std::string::npos || found < start_) {
            return std::string::npos;
        }

        const std::size_t end = start_ + length_;
        if (found >= end || token.size() > end - found) {
            return std::string::npos;
        }

        return found - start_;
    }

    std::size_t FindFirstOf(std::string_view tokens, std::size_t position) const noexcept {
        if (tokens.empty()) {
            return position <= length_ ? position : std::string::npos;
        }
        if (inputSource_ == nullptr || position >= length_) {
            return std::string::npos;
        }

        const std::size_t end = start_ + length_;
        if (const auto* streamSource = dynamic_cast<const StreamXmlReaderInputSource*>(inputSource_.get()); streamSource != nullptr) {
            const std::size_t found = tokens == "<&"
                ? streamSource->FindNextTextSpecial(start_ + position)
                : streamSource->FindFirstOf(tokens, start_ + position);
            if (found == std::string::npos || found < start_ || found >= end) {
                return std::string::npos;
            }
            return found - start_;
        }
        if (const auto* stringSource = dynamic_cast<const StringXmlReaderInputSource*>(inputSource_.get()); stringSource != nullptr) {
            const std::size_t found = stringSource->FindFirstOf(tokens, start_ + position);
            if (found == std::string::npos || found < start_ || found >= end) {
                return std::string::npos;
            }
            return found - start_;
        }

        for (std::size_t probe = position; probe < length_; ++probe) {
            const char ch = inputSource_->CharAt(start_ + probe);
            if (ch == '\0') {
                return std::string::npos;
            }
            if (tokens.find(ch) != std::string_view::npos) {
                return probe;
            }
        }
        return std::string::npos;
    }

    std::size_t ScanQuotedValueEnd(std::size_t quoteStart) const noexcept {
        if (inputSource_ == nullptr || quoteStart >= length_) {
            return std::string::npos;
        }

        std::size_t found = std::string::npos;
        if (const auto* stringSource = dynamic_cast<const StringXmlReaderInputSource*>(inputSource_.get()); stringSource != nullptr) {
            found = stringSource->ScanQuotedValueEnd(start_ + quoteStart);
        } else if (const auto* streamSource = dynamic_cast<const StreamXmlReaderInputSource*>(inputSource_.get()); streamSource != nullptr) {
            found = streamSource->ScanQuotedValueEnd(start_ + quoteStart);
        } else {
            found = ScanQuotedValueEndAt(start_ + quoteStart, [this](std::size_t probe) noexcept {
                return inputSource_->CharAt(probe);
            });
        }

        if (found == std::string::npos || found < start_ || found >= start_ + length_) {
            return std::string::npos;
        }
        return found - start_;
    }

    std::string Slice(std::size_t start, std::size_t count) const override {
        if (inputSource_ == nullptr || start >= length_) {
            return {};
        }

        const std::size_t available = length_ - start;
        const std::size_t clampedCount = count == std::string::npos ? available : (std::min)(count, available);
        return inputSource_->Slice(start_ + start, clampedCount);
    }

    void AppendSliceTo(std::string& target, std::size_t start, std::size_t count) const override {
        if (inputSource_ == nullptr || start >= length_) {
            return;
        }

        const std::size_t available = length_ - start;
        const std::size_t clampedCount = count == std::string::npos ? available : (std::min)(count, available);
        inputSource_->AppendSliceTo(target, start_ + start, clampedCount);
    }

    void EnableReplay() const override {
        if (inputSource_ != nullptr) {
            inputSource_->EnableReplay();
        }
    }

private:
    std::shared_ptr<const XmlReaderInputSource> inputSource_;
    std::size_t start_ = 0;
    std::size_t length_ = 0;
};

std::size_t FindFirstOfFromInputSource(const XmlReaderInputSource* inputSource, std::string_view tokens, std::size_t position) noexcept {
    if (inputSource == nullptr) {
        return std::string::npos;
    }
    if (const auto* streamSource = dynamic_cast<const StreamXmlReaderInputSource*>(inputSource); streamSource != nullptr) {
        if (tokens == "<&") {
            return streamSource->FindNextTextSpecial(position);
        }
        return streamSource->FindFirstOf(tokens, position);
    }
    if (const auto* subrangeSource = dynamic_cast<const SubrangeXmlReaderInputSource*>(inputSource); subrangeSource != nullptr) {
        return subrangeSource->FindFirstOf(tokens, position);
    }

    if (tokens == "<&") {
        const std::size_t nextEntity = inputSource->Find("&", position);
        const std::size_t nextMarkup = inputSource->Find("<", position);
        if (nextEntity == std::string::npos) {
            return nextMarkup;
        }
        if (nextMarkup == std::string::npos) {
            return nextEntity;
        }
        return (std::min)(nextEntity, nextMarkup);
    }

    if (tokens.empty()) {
        return position;
    }
    for (std::size_t probe = position; ; ++probe) {
        const char ch = inputSource->CharAt(probe);
        if (ch == '\0') {
            return std::string::npos;
        }
        if (tokens.find(ch) != std::string_view::npos) {
            return probe;
        }
    }
}

void AdvanceLineInfoFromInputSource(
    const XmlReaderInputSource* inputSource,
    std::size_t start,
    std::size_t end,
    std::size_t& line,
    std::size_t& column) noexcept {
    if (end <= start) {
        return;
    }
    if (inputSource == nullptr) {
        column += end - start;
        return;
    }

    for (std::size_t index = start; index < end; ++index) {
        if (inputSource->CharAt(index) == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
}

void SkipXmlWhitespaceAt(const XmlReaderInputSource* inputSource, std::size_t& position) noexcept {
    while (inputSource != nullptr) {
        const char ch = inputSource->CharAt(position);
        if (ch == '\0' || !IsWhitespace(ch)) {
            break;
        }
        ++position;
    }
}

void SkipXmlWhitespaceAt(std::string_view text, std::size_t& position) noexcept {
    const std::size_t size = text.size();
    if (position >= size) return;

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__)
    const __m128i v_space = _mm_set1_epi8(' ');
    const __m128i v_tab = _mm_set1_epi8('\t');
    const __m128i v_lf = _mm_set1_epi8('\n');
    const __m128i v_cr = _mm_set1_epi8('\r');

    while (position + 16 <= size) {
        __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(text.data() + position));

        __m128i m_space = _mm_cmpeq_epi8(chunk, v_space);
        __m128i m_tab = _mm_cmpeq_epi8(chunk, v_tab);
        __m128i m_lf = _mm_cmpeq_epi8(chunk, v_lf);
        __m128i m_cr = _mm_cmpeq_epi8(chunk, v_cr);

        __m128i is_ws = _mm_or_si128(_mm_or_si128(m_space, m_tab),
            _mm_or_si128(m_lf, m_cr));

        unsigned int mask = static_cast<unsigned int>(_mm_movemask_epi8(is_ws));

        if (mask != 0xFFFF) {
            unsigned long first_non_ws_index = 0;
#ifdef _MSC_VER
            _BitScanForward(&first_non_ws_index, ~mask & 0xFFFF);
#else
            first_non_ws_index = __builtin_ctz(~mask | 0x10000);
#endif
            position += first_non_ws_index;
            return;
        }
        position += 16;
    }
#endif
    while (position < size && IsWhitespace(text[position])) {
        ++position;
    }
}

bool TryConsumeNameFromInputSourceAt(const XmlReaderInputSource* inputSource, std::size_t& position) noexcept {
    if (inputSource == nullptr) {
        return false;
    }

    const char first = inputSource->CharAt(position);
    if (first == '\0' || !IsNameStartChar(first)) {
        return false;
    }

    ++position;
    while (true) {
        const char ch = inputSource->CharAt(position);
        if (ch == '\0' || !IsNameChar(ch)) {
            break;
        }
        ++position;
    }

    return true;
}

std::string ParseNameFromInputSourceAt(const XmlReaderInputSource* inputSource, std::size_t& position) {
    const std::size_t start = position;
    if (!TryConsumeNameFromInputSourceAt(inputSource, position)) {
        return {};
    }

    return inputSource->Slice(start, position - start);
}

bool SkipTagFromInputSourceAt(const XmlReaderInputSource* inputSource, std::size_t& position, bool& isEmptyElement) noexcept {
    if (inputSource == nullptr || inputSource->CharAt(position) != '<') {
        return false;
    }

    ++position;
    if (!TryConsumeNameFromInputSourceAt(inputSource, position)) {
        return false;
    }

    while (true) {
        const char ch = inputSource->CharAt(position);
        if (ch == '\0') {
            return false;
        }

        if (ch == '"' || ch == '\'') {
            const auto quoteEnd = ScanQuotedValueEndAt(position, [inputSource](std::size_t probe) noexcept {
                return inputSource->CharAt(probe);
            });
            if (quoteEnd == std::string::npos) {
                return false;
            }
            position = quoteEnd + 1;
            continue;
        }

        if (inputSource->CharAt(position) == '/' && inputSource->CharAt(position + 1) == '>') {
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
}

bool StartsWithAt(const XmlReaderInputSource* inputSource, std::size_t position, std::string_view token) noexcept {
    if (inputSource == nullptr) {
        return false;
    }

    for (std::size_t index = 0; index < token.size(); ++index) {
        if (inputSource->CharAt(position + index) != token[index]) {
            return false;
        }
    }

    return true;
}

std::size_t ScanDocumentTypeInternalSubsetEndAt(const XmlReaderInputSource* inputSource, std::size_t contentStart) noexcept {
    if (inputSource == nullptr) {
        return std::string::npos;
    }

    std::size_t position = contentStart;
    int bracketDepth = 1;
    bool inQuote = false;
    char quote = '\0';

    while (bracketDepth > 0) {
        const char ch = inputSource->CharAt(position);
        if (ch == '\0') {
            break;
        }

        ++position;
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

std::size_t ScanQuotedValueEndAt(const XmlReaderInputSource* inputSource, std::size_t quoteStart) noexcept {
    if (inputSource == nullptr) {
        return std::string::npos;
    }

    if (const auto* stringSource = dynamic_cast<const StringXmlReaderInputSource*>(inputSource); stringSource != nullptr) {
        return stringSource->ScanQuotedValueEnd(quoteStart);
    }
    if (const auto* streamSource = dynamic_cast<const StreamXmlReaderInputSource*>(inputSource); streamSource != nullptr) {
        return streamSource->ScanQuotedValueEnd(quoteStart);
    }
    if (const auto* subrangeSource = dynamic_cast<const SubrangeXmlReaderInputSource*>(inputSource); subrangeSource != nullptr) {
        return subrangeSource->ScanQuotedValueEnd(quoteStart);
    }

    return ScanQuotedValueEndAt(quoteStart, [inputSource](std::size_t probe) noexcept {
        return inputSource->CharAt(probe);
    });
}

std::size_t ScanDelimitedSectionEndAt(
    const XmlReaderInputSource* inputSource,
    std::size_t contentStart,
    std::string_view terminator) noexcept {
    if (inputSource == nullptr) {
        return std::string::npos;
    }

    return ScanDelimitedSectionEndAt(contentStart, terminator, [inputSource](std::string_view token, std::size_t position) noexcept {
        return inputSource->Find(std::string(token), position);
    });
}

struct XmlQuotedValueToken {
    std::string rawValue;
    bool valid = false;
};

XmlQuotedValueToken ParseQuotedLiteralFromInputSourceAt(const XmlReaderInputSource* inputSource, std::size_t& position) {
    XmlQuotedValueToken token;
    if (inputSource == nullptr) {
        return token;
    }

    const char quote = inputSource->CharAt(position);
    const auto quoteEnd = ScanQuotedValueEndAt(position, [inputSource](std::size_t probe) noexcept {
        return inputSource->CharAt(probe);
    });
    if ((quote != '"' && quote != '\'') || quoteEnd == std::string::npos) {
        return token;
    }

    token.rawValue = inputSource->Slice(position + 1, quoteEnd - position - 1);
    position = quoteEnd + 1;
    token.valid = true;
    return token;
}

struct XmlEntityReferenceToken {
    std::string raw;
    std::string name;
    std::size_t end = std::string::npos;
};

XmlEntityReferenceToken ScanEntityReferenceFromInputSourceAt(const XmlReaderInputSource* inputSource, std::size_t ampersandPosition) {
    if (inputSource == nullptr) {
        return {};
    }

    const auto semicolon = inputSource->Find(";", ampersandPosition);
    if (semicolon == std::string::npos) {
        return {};
    }

    return {
        inputSource->Slice(ampersandPosition, semicolon - ampersandPosition + 1),
        inputSource->Slice(ampersandPosition + 1, semicolon - ampersandPosition - 1),
        semicolon + 1};
}

class XmlReaderTokenizer final {
public:
    using AttributeAssignmentToken = XmlAttributeAssignmentToken;
    using QuotedValueToken = XmlQuotedValueToken;
    using EntityReferenceToken = XmlEntityReferenceToken;

    explicit XmlReaderTokenizer(std::shared_ptr<const XmlReaderInputSource> inputSource)
                : inputSource_(std::move(inputSource)),
                    stringInputSource_(dynamic_cast<const StringXmlReaderInputSource*>(inputSource_.get())),
                    streamInputSource_(dynamic_cast<const StreamXmlReaderInputSource*>(inputSource_.get())),
                    subrangeInputSource_(dynamic_cast<const SubrangeXmlReaderInputSource*>(inputSource_.get())) {
    }

    bool HasChar(std::size_t position) const noexcept {
        return CharAt(position) != '\0';
    }

    char CharAt(std::size_t position) const noexcept {
        return inputSource_ == nullptr ? '\0' : inputSource_->CharAt(position);
    }

    const char* PtrAt(std::size_t position, std::size_t& available) const noexcept {
        available = 0;
        return inputSource_ == nullptr ? nullptr : inputSource_->PtrAt(position, available);
    }

    bool StartsWith(std::size_t position, std::string_view token) const noexcept {
        return StartsWithAt(inputSource_.get(), position, token);
    }

    std::size_t Find(const std::string& token, std::size_t position) const noexcept {
        return inputSource_ == nullptr ? std::string::npos : inputSource_->Find(token, position);
    }

    std::size_t ScanQuotedValueEnd(std::size_t quoteStart) const noexcept {
        return ScanQuotedValueEndAt(inputSource_.get(), quoteStart);
    }

    std::size_t ScanDelimitedSectionEnd(std::size_t contentStart, std::string_view terminator) const noexcept {
        return ScanDelimitedSectionEndAt(inputSource_.get(), contentStart, terminator);
    }

    std::size_t ScanDocumentTypeInternalSubsetEnd(std::size_t contentStart) const noexcept {
        return ScanDocumentTypeInternalSubsetEndAt(inputSource_.get(), contentStart);
    }

    XmlMarkupKind ClassifyMarkup(std::size_t position) const noexcept {
        return ClassifyXmlMarkupWithCharAt(position, [this](std::size_t probe) noexcept {
            return CharAt(probe);
        });
    }

    std::string ParseNameAt(std::size_t& position) const {
        return ParseNameFromInputSourceAt(inputSource_.get(), position);
    }

    bool SkipTag(std::size_t& position, bool& isEmptyElement) const {
        return SkipTagFromInputSourceAt(inputSource_.get(), position, isEmptyElement);
    }

    void SkipWhitespace(std::size_t& position) const noexcept {
        SkipXmlWhitespaceAt(inputSource_.get(), position);
    }

    bool ConsumeStartTagClose(std::size_t& position, bool& isEmptyElement) const noexcept {
        return ConsumeXmlStartTagCloseAt(
            position,
            isEmptyElement,
            [this](std::size_t probe) noexcept {
                return CharAt(probe);
            },
            [this](std::size_t probe, std::string_view token) noexcept {
                return StartsWith(probe, token);
            });
    }

    bool ConsumeEndTagClose(std::size_t& position) const noexcept {
        return ConsumeXmlEndTagCloseAt(
            position,
            [this](std::size_t& probe) noexcept {
                SkipWhitespace(probe);
            },
            [this](std::size_t probe) noexcept {
                return CharAt(probe);
            });
    }

    AttributeAssignmentToken ParseAttributeAssignment(std::size_t& position) const {
        return ParseXmlAttributeAssignmentAt(
            position,
            [this](std::size_t& probe) {
                return ParseNameAt(probe);
            },
            [this](std::size_t& probe) noexcept {
                SkipWhitespace(probe);
            },
            [this](std::size_t probe) noexcept {
                return CharAt(probe);
            },
            [this](std::size_t probe) noexcept {
                return ScanQuotedValueEnd(probe);
            });
    }

    QuotedValueToken ParseQuotedLiteral(std::size_t& position) const {
        return ParseQuotedLiteralFromInputSourceAt(inputSource_.get(), position);
    }

    EntityReferenceToken ScanEntityReference(std::size_t ampersandPosition) const {
        return ScanEntityReferenceFromInputSourceAt(inputSource_.get(), ampersandPosition);
    }

    std::string Slice(std::size_t start, std::size_t count = std::string::npos) const {
        return inputSource_ == nullptr ? std::string{} : inputSource_->Slice(start, count);
    }

private:
    std::shared_ptr<const XmlReaderInputSource> inputSource_;
    const StringXmlReaderInputSource* stringInputSource_ = nullptr;
    const StreamXmlReaderInputSource* streamInputSource_ = nullptr;
    const SubrangeXmlReaderInputSource* subrangeInputSource_ = nullptr;
};

XmlReader::XmlReader(XmlReaderSettings settings) : settings_(std::move(settings)) {
}

char XmlReader::SourceCharAt(std::size_t position) const noexcept {
    return inputSource_ == nullptr ? '\0' : inputSource_->CharAt(position);
}

const char* XmlReader::SourcePtrAt(std::size_t position, std::size_t& available) const noexcept {
    available = 0;
    return inputSource_ == nullptr ? nullptr : inputSource_->PtrAt(position, available);
}

bool XmlReader::HasSourceChar(std::size_t position) const noexcept {
    return SourceCharAt(position) != '\0';
}

std::size_t XmlReader::FindInSource(const std::string& token, std::size_t position) const noexcept {
    return inputSource_ == nullptr ? std::string::npos : inputSource_->Find(token, position);
}

std::string XmlReader::SourceSubstr(std::size_t start, std::size_t count) const {
    return inputSource_ == nullptr ? std::string{} : inputSource_->Slice(start, count);
}

bool XmlReader::SourceRangeContains(std::size_t start, std::size_t end, char value) const noexcept {
    if (inputSource_ == nullptr || end <= start) {
        return false;
    }

    for (std::size_t index = start; index < end && HasSourceChar(index); ++index) {
        if (SourceCharAt(index) == value) {
            return true;
        }
    }

    return false;
}

void XmlReader::AppendSourceSubstrTo(std::string& target, std::size_t start, std::size_t count) const {
    if (inputSource_ == nullptr) {
        return;
    }
    inputSource_->AppendSliceTo(target, start, count);
}

std::size_t XmlReader::AppendDecodedSourceRangeTo(std::string& target, std::size_t start, std::size_t end) const {
    if (inputSource_ == nullptr || end <= start) {
        return 0;
    }

    const std::size_t originalSize = target.size();
    target.reserve(originalSize + (end - start));

    if (const auto* stringSource = dynamic_cast<const StringXmlReaderInputSource*>(inputSource_.get()); stringSource != nullptr) {
        const std::string& source = *stringSource->Text();
        
        if (entityDeclarations_.empty()) {
            target.resize(originalSize + (end - start));
            char* out = target.data() + originalSize;
            const char* in = source.data() + start;
            const char* const inEnd = source.data() + end;

            while (in < inEnd) {
                const char* ampersand = static_cast<const char*>(std::memchr(in, '&', inEnd - in));
                if (!ampersand) {
                    const std::size_t len = inEnd - in;
                    std::memcpy(out, in, len);
                    out += len;
                    break;
                }

                const std::size_t len = ampersand - in;
                if (len > 0) {
                    std::memcpy(out, in, len);
                    out += len;
                }
                in = ampersand;

                const char* semicolon = static_cast<const char*>(std::memchr(in + 1, ';', inEnd - (in + 1)));
                if (!semicolon) {
                    Throw("Unterminated entity reference");
                }

                const std::string_view entity(in + 1, semicolon - (in + 1));
                const auto predefined = ResolvePredefinedEntityReferenceValueView(entity);
                if (!predefined.empty()) {
                    std::memcpy(out, predefined.data(), predefined.size());
                    out += predefined.size();
                } else if (!entity.empty() && entity.front() == '#') {
                    unsigned int codePoint = 0;
                    if (!TryParseNumericEntityReferenceCodePoint(entity, codePoint)) {
                        Throw("Invalid numeric entity reference: &" + std::string(entity) + ';');
                    }
                    if (codePoint <= 0x7F) {
                        *out++ = static_cast<char>(codePoint);
                    } else if (codePoint <= 0x7FF) {
                        *out++ = static_cast<char>(0xC0 | (codePoint >> 6));
                        *out++ = static_cast<char>(0x80 | (codePoint & 0x3F));
                    } else if (codePoint <= 0xFFFF) {
                        *out++ = static_cast<char>(0xE0 | (codePoint >> 12));
                        *out++ = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                        *out++ = static_cast<char>(0x80 | (codePoint & 0x3F));
                    } else {
                        *out++ = static_cast<char>(0xF0 | (codePoint >> 18));
                        *out++ = static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
                        *out++ = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                        *out++ = static_cast<char>(0x80 | (codePoint & 0x3F));
                    }
                } else {
                    Throw("Unknown entity reference: &" + std::string(entity) + ';');
                }
                in = semicolon + 1;
            }
            
            const std::size_t finalLen = out - target.data();
            target.resize(finalLen);
            const std::size_t appendedBytes = finalLen - originalSize;
            if (settings_.MaxCharactersFromEntities != 0) {
                const_cast<XmlReader*>(this)->entityCharactersRead_ += appendedBytes;
                if (const_cast<XmlReader*>(this)->entityCharactersRead_ > settings_.MaxCharactersFromEntities) {
                    Throw("The XML document exceeds the configured MaxCharactersFromEntities limit");
                }
            }
            return appendedBytes;
        }

        std::size_t cursor = start;
        while (cursor < end) {
            std::size_t ampersand = source.find('&', cursor);
            if (ampersand == std::string::npos || ampersand >= end) {
                target.append(source, cursor, end - cursor);
                break;
            }

            if (ampersand > cursor) {
                target.append(source, cursor, ampersand - cursor);
            }

            const std::size_t semicolon = source.find(';', ampersand + 1);
            if (semicolon == std::string::npos || semicolon >= end) {
                Throw("Unterminated entity reference");
            }

            const std::string_view entity(source.data() + ampersand + 1, semicolon - ampersand - 1);
            const auto predefined = ResolvePredefinedEntityReferenceValueView(entity);
            if (!predefined.empty()) {
                target.append(predefined.data(), predefined.size());
            } else if (!entity.empty() && entity.front() == '#') {
                unsigned int codePoint = 0;
                if (!TryParseNumericEntityReferenceCodePoint(entity, codePoint)) {
                    Throw("Invalid numeric entity reference: &" + std::string(entity) + ';');
                }
                AppendCodePointUtf8(target, codePoint);
            } else {
                const std::string entityName(entity);
                if (const auto found = entityDeclarations_.find(entityName); found != entityDeclarations_.end()) {
                    DecodeEntityTextTo(
                        target,
                        found->second,
                        [this](const std::string& nestedEntity) -> std::optional<std::string> {
                            const auto resolved = entityDeclarations_.find(nestedEntity);
                            return resolved == entityDeclarations_.end() ? std::nullopt : std::optional<std::string>(resolved->second);
                        },
                        [this](const std::string& message) {
                            Throw(message);
                        });
                } else {
                    Throw("Unknown entity reference: &" + entityName + ';');
                }
            }

            cursor = semicolon + 1;
        }

        const std::size_t appendedBytes = target.size() - originalSize;
        if (settings_.MaxCharactersFromEntities != 0) {
            const_cast<XmlReader*>(this)->entityCharactersRead_ += appendedBytes;
            if (const_cast<XmlReader*>(this)->entityCharactersRead_ > settings_.MaxCharactersFromEntities) {
                Throw("The XML document exceeds the configured MaxCharactersFromEntities limit");
            }
        }

        return appendedBytes;
    }

    std::size_t cursor = start;
    while (cursor < end) {
        std::size_t ampersand = cursor;
        while (ampersand < end && SourceCharAt(ampersand) != '&') {
            ++ampersand;
        }
        if (ampersand > cursor) {
            AppendSourceSubstrTo(target, cursor, ampersand - cursor);
            cursor = ampersand;
            continue;
        }

        std::size_t semicolon = ampersand + 1;
        while (semicolon < end && SourceCharAt(semicolon) != ';') {
            ++semicolon;
        }
        if (semicolon >= end || SourceCharAt(semicolon) != ';') {
            Throw("Unterminated entity reference");
        }

        std::string entity;
        entity.reserve(semicolon - ampersand - 1);
        for (std::size_t probe = ampersand + 1; probe < semicolon; ++probe) {
            entity.push_back(SourceCharAt(probe));
        }

        const auto predefined = ResolvePredefinedEntityReferenceValueView(entity);
        if (!predefined.empty()) {
            target.append(predefined.data(), predefined.size());
        } else if (!entity.empty() && entity.front() == '#') {
            unsigned int codePoint = 0;
            if (!TryParseNumericEntityReferenceCodePoint(entity, codePoint)) {
                Throw("Invalid numeric entity reference: &" + entity + ';');
            }
            AppendCodePointUtf8(target, codePoint);
        } else if (const auto found = entityDeclarations_.find(entity); found != entityDeclarations_.end()) {
            DecodeEntityTextTo(
                target,
                found->second,
                [this](const std::string& nestedEntity) -> std::optional<std::string> {
                    const auto resolved = entityDeclarations_.find(nestedEntity);
                    return resolved == entityDeclarations_.end() ? std::nullopt : std::optional<std::string>(resolved->second);
                },
                [this](const std::string& message) {
                    Throw(message);
                });
        } else {
            Throw("Unknown entity reference: &" + entity + ';');
        }

        cursor = semicolon + 1;
    }

    const std::size_t appendedBytes = target.size() - originalSize;
    if (settings_.MaxCharactersFromEntities != 0) {
        const_cast<XmlReader*>(this)->entityCharactersRead_ += appendedBytes;
        if (const_cast<XmlReader*>(this)->entityCharactersRead_ > settings_.MaxCharactersFromEntities) {
            Throw("The XML document exceeds the configured MaxCharactersFromEntities limit");
        }
    }

    return appendedBytes;
}

void XmlReader::AppendCurrentValueTo(std::string& target) const {
    if (!currentValue_.empty()) {
        target += currentValue_;
        return;
    }

    if (currentValueStart_ != std::string::npos
        && currentValueEnd_ != std::string::npos
        && currentValueEnd_ >= currentValueStart_) {
        AppendSourceSubstrTo(target, currentValueStart_, currentValueEnd_ - currentValueStart_);
        return;
    }

    target += Value();
}

void XmlReader::DecodeAndAppendCurrentBase64(std::vector<unsigned char>& buffer, unsigned int& accumulator, int& bits) const {
    if (!currentValue_.empty()) {
        AppendDecodedBase64Chunk(currentValue_, buffer, accumulator, bits);
        return;
    }

    if (currentValueStart_ != std::string::npos
        && currentValueEnd_ != std::string::npos
        && currentValueEnd_ >= currentValueStart_) {
        for (std::size_t index = currentValueStart_; index < currentValueEnd_; ++index) {
            const char ch = SourceCharAt(index);
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
        return;
    }

    AppendDecodedBase64Chunk(Value(), buffer, accumulator, bits);
}

std::size_t XmlReader::EarliestRetainedSourceOffset() const noexcept {
    std::size_t earliest = position_;

    auto includeOffset = [&earliest](std::size_t offset) {
        if (offset != std::string::npos && offset < earliest) {
            earliest = offset;
        }
    };

    auto includeValueRange = [&includeOffset](const std::string& value, std::size_t valueStart, std::size_t valueEnd) {
        if (valueStart != std::string::npos && valueEnd != std::string::npos && valueEnd >= valueStart && value.empty()) {
            includeOffset(valueStart);
        }
    };

    includeValueRange(currentValue_, currentValueStart_, currentValueEnd_);

    if (currentNodeType_ == XmlNodeType::Element) {
        if ((currentOuterXml_.empty() || (!currentIsEmptyElement_ && currentInnerXml_.empty()))
            && currentElementStart_ != std::string::npos) {
            includeOffset(currentElementStart_);
        }
        includeOffset(currentEarliestRetainedAttributeValueStart_);
    } else if (currentOuterXml_.empty() && currentNodeStart_ != std::string::npos && currentNodeEnd_ != std::string::npos) {
        includeOffset(currentNodeStart_);
    }

    for (const auto& node : bufferedNodes_) {
        includeValueRange(node.value, node.valueStart, node.valueEnd);
        if (node.nodeType == XmlNodeType::Element) {
            if ((node.outerXml.empty() || (!node.isEmptyElement && node.innerXml.empty()))
                && node.elementStart != std::string::npos) {
                includeOffset(node.elementStart);
            }
        } else if (node.outerXml.empty() && node.nodeStart != std::string::npos && node.nodeEnd != std::string::npos) {
            includeOffset(node.nodeStart);
        }
    }

    return earliest;
}

void XmlReader::MaybeDiscardSourcePrefix() const {
    if (inputSource_ == nullptr) {
        return;
    }

    const std::size_t discardBefore = EarliestRetainedSourceOffset();
    if (discardBefore <= discardedSourceOffset_) {
        return;
    }

    std::size_t line = 0;
    std::size_t column = 0;
    if (discardBefore == position_) {
        line = lineNumber_;
        column = linePosition_;
    } else {
        const auto computed = ComputeLineColumn(discardBefore);
        line = computed.first;
        column = computed.second;
    }
    inputSource_->DiscardBefore(discardBefore);
    discardedSourceOffset_ = discardBefore;
    discardedLineNumber_ = line;
    discardedLinePosition_ = column;
}

void XmlReader::FinalizeSuccessfulRead() {
    eof_ = false;
    MaybeDiscardSourcePrefix();
}

char XmlReader::Peek() const noexcept {
    return SourceCharAt(position_);
}

char XmlReader::ReadChar() {
    if (!HasSourceChar(position_)) {
        Throw("Unexpected end of XML document");
    }

    const char ch = SourceCharAt(position_++);
    ++totalCharactersRead_;
    if (settings_.MaxCharactersInDocument != 0 && totalCharactersRead_ > settings_.MaxCharactersInDocument) {
        Throw("The XML document exceeds the configured MaxCharactersInDocument limit");
    }
    if (ch == '\n') {
        ++lineNumber_;
        linePosition_ = 1;
    } else {
        ++linePosition_;
    }

    return ch;
}

bool XmlReader::StartsWith(const std::string& token) const noexcept {
    for (std::size_t index = 0; index < token.size(); ++index) {
        if (SourceCharAt(position_ + index) != token[index]) {
            return false;
        }
    }
    return true;
}

void XmlReader::SkipWhitespace() {
    while (HasSourceChar(position_) && IsWhitespace(SourceCharAt(position_))) {
        if (SourceCharAt(position_) == '\n') {
            ++lineNumber_;
            linePosition_ = 1;
        } else {
            ++linePosition_;
        }
        ++position_;
    }
}

std::string XmlReader::ParseName() {
    if (!IsNameStartChar(Peek())) {
        Throw("Invalid XML name");
    }

    const auto start = position_++;
    while (HasSourceChar(position_) && IsNameChar(SourceCharAt(position_))) {
        ++position_;
    }

    linePosition_ += position_ - start;

    return SourceSubstr(start, position_ - start);
}

std::string XmlReader::DecodeEntities(const std::string& value) const {
    const std::string decoded = DecodeEntityText(
        value,
        [this](const std::string& entity) -> std::optional<std::string> {
            const auto found = entityDeclarations_.find(entity);
            return found == entityDeclarations_.end() ? std::nullopt : std::optional<std::string>(found->second);
        },
        [this](const std::string& message) {
            Throw(message);
        });
    if (settings_.MaxCharactersFromEntities != 0) {
        const_cast<XmlReader*>(this)->entityCharactersRead_ += decoded.size();
        if (const_cast<XmlReader*>(this)->entityCharactersRead_ > settings_.MaxCharactersFromEntities) {
            Throw("The XML document exceeds the configured MaxCharactersFromEntities limit");
        }
    }
    return decoded;
}

void XmlReader::QueueNode(
    XmlNodeType nodeType,
    std::string name,
    std::string namespaceUri,
    std::string value,
    int depth,
    bool isEmptyElement,
    std::string innerXml,
    std::string outerXml,
    std::size_t valueStart,
    std::size_t valueEnd,
    std::size_t nodeStart,
    std::size_t nodeEnd,
    std::vector<std::pair<std::string, std::string>> attributes,
    std::vector<std::string> attributeNamespaceUris,
    std::size_t elementStart,
    std::size_t contentStart,
    std::size_t closeStart,
    std::size_t closeEnd) {
    bufferedNodes_.push_back(BufferedNode{
        nodeType,
        std::move(name),
        std::move(namespaceUri),
        std::move(value),
        valueStart,
        valueEnd,
        depth,
        isEmptyElement,
        std::move(innerXml),
        std::move(outerXml),
        nodeStart,
        nodeEnd,
        std::move(attributes),
        std::move(attributeNamespaceUris),
        elementStart,
        contentStart,
        closeStart,
        closeEnd});
}

bool XmlReader::TryConsumeBufferedNode() {
    if (bufferedNodes_.empty()) {
        return false;
    }

    auto node = std::move(bufferedNodes_.front());
    bufferedNodes_.pop_front();
    SetCurrentNode(
        node.nodeType,
        std::move(node.name),
        std::move(node.namespaceUri),
        std::move(node.value),
        node.depth,
        node.isEmptyElement,
        std::move(node.innerXml),
        std::move(node.outerXml),
        node.valueStart,
        node.valueEnd,
        node.nodeStart,
        node.nodeEnd,
        std::move(node.attributes),
        std::move(node.attributeNamespaceUris),
        node.elementStart,
        node.contentStart,
        node.closeStart,
        node.closeEnd);
    return true;
}

std::string XmlReader::ParseQuotedValue(bool decodeEntities) {
    XmlReaderTokenizer tokenizer(inputSource_);
    const char quote = ReadChar();
    if (quote != '\"' && quote != '\'') {
        Throw("Expected quoted value");
    }

    const auto start = position_;
    const auto quoteEnd = tokenizer.ScanQuotedValueEnd(start - 1);
    if (quoteEnd == std::string::npos) {
        Throw("Unterminated quoted value");
    }

    const auto raw = tokenizer.Slice(start, quoteEnd - start);
    position_ = quoteEnd + 1;
    return decodeEntities ? DecodeEntities(raw) : raw;
}

void XmlReader::ResetCurrentNode() {
    currentNodeType_ = XmlNodeType::None;
    currentName_.clear();
    currentNamespaceUri_.clear();
    currentValue_.clear();
    currentInnerXml_.clear();
    currentOuterXml_.clear();
    currentAttributes_.clear();
    currentAttributeNamespaceUrisResolved_.clear();
    currentAttributeValueMetadata_.clear();
    currentAttributeNamespaceUris_.clear();
    currentLocalNamespaceDeclarations_.clear();
    currentEarliestRetainedAttributeValueStart_ = std::string::npos;
    currentDeclarationVersion_.clear();
    currentDeclarationEncoding_.clear();
    currentDeclarationStandalone_.clear();
    currentValueStart_ = std::string::npos;
    currentValueEnd_ = std::string::npos;
    currentDepth_ = 0;
    currentIsEmptyElement_ = false;
    currentNodeStart_ = std::string::npos;
    currentNodeEnd_ = std::string::npos;
    currentElementStart_ = std::string::npos;
    currentContentStart_ = std::string::npos;
    currentCloseStart_ = std::string::npos;
    currentCloseEnd_ = std::string::npos;
}

void XmlReader::SetCurrentNode(
    XmlNodeType nodeType,
    std::string name,
    std::string namespaceUri,
    std::string value,
    int depth,
    bool isEmptyElement,
    std::string innerXml,
    std::string outerXml,
    std::size_t valueStart,
    std::size_t valueEnd,
    std::size_t nodeStart,
    std::size_t nodeEnd,
    std::vector<std::pair<std::string, std::string>> attributes,
    std::vector<std::string> attributeNamespaceUris,
    std::size_t elementStart,
    std::size_t contentStart,
    std::size_t closeStart,
    std::size_t closeEnd) {
    if (!name.empty()) {
        nameTable_.Add(name);
    }
    if (!namespaceUri.empty()) {
        nameTable_.Add(namespaceUri);
    }
    for (const auto& attribute : attributes) {
        if (!attribute.first.empty()) {
            nameTable_.Add(attribute.first);
        }
    }
    for (const auto& attributeNamespaceUri : attributeNamespaceUris) {
        if (!attributeNamespaceUri.empty()) {
            nameTable_.Add(attributeNamespaceUri);
        }
    }
    currentNodeType_ = nodeType;
    currentName_ = std::move(name);
    currentNamespaceUri_ = std::move(namespaceUri);
    currentValue_ = std::move(value);
    currentDepth_ = depth;
    currentIsEmptyElement_ = isEmptyElement;
    currentInnerXml_ = std::move(innerXml);
    currentOuterXml_ = std::move(outerXml);
    currentAttributes_ = std::move(attributes);
    currentAttributeNamespaceUrisResolved_.assign(attributeNamespaceUris.size(), static_cast<unsigned char>(1));
    currentAttributeValueMetadata_.clear();
    currentAttributeNamespaceUris_ = std::move(attributeNamespaceUris);
    currentLocalNamespaceDeclarations_.clear();
    currentDeclarationVersion_.clear();
    currentDeclarationEncoding_.clear();
    currentDeclarationStandalone_.clear();
    currentValueStart_ = valueStart;
    currentValueEnd_ = valueEnd;
    currentNodeStart_ = nodeStart;
    currentNodeEnd_ = nodeEnd;
    currentElementStart_ = elementStart;
    currentContentStart_ = contentStart;
    currentCloseStart_ = closeStart;
    currentCloseEnd_ = closeEnd;
}

const XmlNameTable& XmlReader::NameTable() const noexcept {
    return nameTable_;
}

std::pair<std::size_t, std::size_t> XmlReader::ComputeLineColumn(std::size_t position) const noexcept {
    std::size_t scanStart = 0;
    std::size_t line = 1;
    std::size_t column = 1;
    if (position >= discardedSourceOffset_) {
        scanStart = discardedSourceOffset_;
        line = discardedLineNumber_;
        column = discardedLinePosition_;
    }

    if (position > scanStart && inputSource_ != nullptr) {
        const std::size_t firstNewline = FindFirstOfFromInputSource(inputSource_.get(), "\n", scanStart);
        if (firstNewline == std::string::npos || firstNewline >= position) {
            return {line, column + (position - scanStart)};
        }
    }

    for (std::size_t index = scanStart; index < position && HasSourceChar(index); ++index) {
        if (SourceCharAt(index) == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    return {line, column};
}

[[noreturn]] void XmlReader::Throw(const std::string& message) const {
    const auto [line, column] = ComputeLineColumn(position_);
    throw XmlException(message, line, column);
}

void XmlReader::ParseDeclaration() {
    XmlReaderTokenizer tokenizer(inputSource_);
    const auto start = position_;
    if (!xmlDeclarationAllowed_) {
        position_ += 5;
        linePosition_ += 5;
        Throw("XML declaration is only allowed at the beginning of the document");
    }
    position_ += 5;
    linePosition_ += 5;

    std::string version = "1.0";
    std::string encoding;
    std::string standalone;

    while (true) {
        SkipWhitespace();
        if (tokenizer.StartsWith(position_, "?>")) {
            position_ += 2;
            linePosition_ += 2;
            break;
        }

        const auto attributeStart = position_;
        std::size_t cursor = position_;
        const auto attributeToken = tokenizer.ParseAttributeAssignment(cursor);
        position_ = cursor;
        AdvanceLineInfoFromInputSource(inputSource_.get(), attributeStart, position_, lineNumber_, linePosition_);
        if (attributeToken.name.empty()) {
            Throw("Invalid XML name");
        }

        if (!attributeToken.valid) {
            if (!attributeToken.sawEquals) {
                if (tokenizer.HasChar(position_)) {
                    ++linePosition_;
                    ++position_;
                }
                Throw("Expected '=' in XML declaration");
            }
            if (tokenizer.HasChar(position_)) {
                ++linePosition_;
                ++position_;
            }
            Throw("Expected quoted value");
        }

        const std::string name = attributeToken.name;
        const std::string value = DecodeEntities(SourceSubstr(attributeToken.rawValueStart, attributeToken.rawValueEnd - attributeToken.rawValueStart));

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

    XmlDeclaration declaration(version, encoding, standalone);
    SetCurrentNode(
        XmlNodeType::XmlDeclaration,
        "xml",
        {},
        {},
        0,
        false,
        {},
        {},
        std::string::npos,
        std::string::npos,
        start,
        position_);
    currentDeclarationVersion_ = declaration.Version();
    currentDeclarationEncoding_ = declaration.Encoding();
    currentDeclarationStandalone_ = declaration.Standalone();
    xmlDeclarationAllowed_ = false;
}

void XmlReader::ParseDocumentType() {
    XmlReaderTokenizer tokenizer(inputSource_);
    if (settings_.DtdProcessing == DtdProcessing::Prohibit) {
        Throw("DTD is prohibited in this XML document");
    }

    const auto start = position_;
    position_ += 9;
    linePosition_ += 9;
    SkipWhitespace();

    const std::string name = ParseName();
    SkipWhitespace();

    std::string publicId;
    std::string systemId;
    std::string internalSubset;

    entityDeclarations_.clear();
    declaredEntityNames_.clear();
    notationDeclarationNames_.clear();
    unparsedEntityDeclarationNames_.clear();
    externalEntitySystemIds_.clear();

    if (tokenizer.StartsWith(position_, "PUBLIC")) {
        position_ += 6;
        linePosition_ += 6;
        SkipWhitespace();
        const auto publicLiteralStart = position_;
        std::size_t cursor = position_;
        auto publicIdToken = tokenizer.ParseQuotedLiteral(cursor);
        position_ = cursor;
        AdvanceLineInfoFromInputSource(inputSource_.get(), publicLiteralStart, position_, lineNumber_, linePosition_);
        if (!publicIdToken.valid) {
            if (tokenizer.HasChar(position_)) {
                ++linePosition_;
                ++position_;
            }
            Throw("Expected quoted value");
        }
        publicId = publicIdToken.rawValue;

        const auto betweenIdsStart = position_;
        tokenizer.SkipWhitespace(position_);
        AdvanceLineInfoFromInputSource(inputSource_.get(), betweenIdsStart, position_, lineNumber_, linePosition_);
        const auto systemLiteralStart = position_;
        cursor = position_;
        auto systemIdToken = tokenizer.ParseQuotedLiteral(cursor);
        position_ = cursor;
        AdvanceLineInfoFromInputSource(inputSource_.get(), systemLiteralStart, position_, lineNumber_, linePosition_);
        if (!systemIdToken.valid) {
            if (tokenizer.HasChar(position_)) {
                ++linePosition_;
                ++position_;
            }
            Throw("Expected quoted value");
        }
        systemId = systemIdToken.rawValue;
        SkipWhitespace();
    } else if (tokenizer.StartsWith(position_, "SYSTEM")) {
        position_ += 6;
        linePosition_ += 6;
        SkipWhitespace();
        const auto systemLiteralStart = position_;
        std::size_t cursor = position_;
        auto systemIdToken = tokenizer.ParseQuotedLiteral(cursor);
        position_ = cursor;
        AdvanceLineInfoFromInputSource(inputSource_.get(), systemLiteralStart, position_, lineNumber_, linePosition_);
        if (!systemIdToken.valid) {
            if (tokenizer.HasChar(position_)) {
                ++linePosition_;
                ++position_;
            }
            Throw("Expected quoted value");
        }
        systemId = systemIdToken.rawValue;
        SkipWhitespace();
    }

    if (Peek() == '[') {
        ++position_;
        ++linePosition_;
        const auto subsetStart = position_;
        const auto subsetEnd = tokenizer.ScanDocumentTypeInternalSubsetEnd(subsetStart);
        if (subsetEnd == std::string::npos) {
            Throw("Unterminated DOCTYPE internal subset");
        }
        position_ = subsetEnd + 1;
        AdvanceLineInfoFromInputSource(inputSource_.get(), subsetStart, position_, lineNumber_, linePosition_);
        internalSubset = Trim(SourceSubstr(subsetStart, subsetEnd - subsetStart));
        if (settings_.DtdProcessing == DtdProcessing::Parse) {
            std::vector<std::shared_ptr<XmlNode>> entities;
            std::vector<std::shared_ptr<XmlNode>> notations;
            ParseDocumentTypeInternalSubset(internalSubset, entities, notations);
            PopulateInternalEntityDeclarations(entities, entityDeclarations_);
            for (const auto& entity : entities) {
                if (entity != nullptr) {
                    declaredEntityNames_.insert(entity->Name());
                    const auto typedEntity = std::dynamic_pointer_cast<XmlEntity>(entity);
                    if (typedEntity != nullptr) {
                        if (!typedEntity->SystemId().empty()) {
                            externalEntitySystemIds_[typedEntity->Name()] = typedEntity->SystemId();
                        }
                        if (!typedEntity->NotationName().empty()) {
                            unparsedEntityDeclarationNames_.insert(typedEntity->Name());
                        }
                    }
                }
            }
            for (const auto& notation : notations) {
                if (notation != nullptr) {
                    notationDeclarationNames_.insert(notation->Name());
                }
            }
        }
        SkipWhitespace();
    }

    if (ReadChar() != '>') {
        Throw("Expected '>' after DOCTYPE");
    }

    SetCurrentNode(
        XmlNodeType::DocumentType,
        name,
        {},
        {},
        static_cast<int>(elementStack_.size()),
        false,
        {},
        {},
        std::string::npos,
        std::string::npos,
        start,
        position_);
    sawDocumentType_ = true;
}

void XmlReader::ParseProcessingInstruction() {
    XmlReaderTokenizer tokenizer(inputSource_);
    const auto start = position_;
    position_ += 2;
    linePosition_ += 2;
    const std::string target = ParseName();
    std::size_t dataStart = std::string::npos;
    std::size_t dataEnd = std::string::npos;
    if (!StartsWith("?>")) {
        const auto end = tokenizer.ScanDelimitedSectionEnd(position_, "?>");
        if (end == std::string::npos) {
            Throw("Unterminated processing instruction");
        }
        dataStart = position_;
        dataEnd = end;
        while (dataStart < dataEnd && IsWhitespace(SourceCharAt(dataStart))) {
            ++dataStart;
        }
        while (dataEnd > dataStart && IsWhitespace(SourceCharAt(dataEnd - 1))) {
            --dataEnd;
        }
        const auto lineInfoStart = position_;
        position_ = end;
        AdvanceLineInfoFromInputSource(inputSource_.get(), lineInfoStart, position_, lineNumber_, linePosition_);
    }
    position_ += 2;
    linePosition_ += 2;

    SetCurrentNode(
        XmlNodeType::ProcessingInstruction,
        target,
        {},
        {},
        static_cast<int>(elementStack_.size()),
        false,
        {},
        {},
        dataStart,
        dataEnd,
        start,
        position_);
}

void XmlReader::ParseComment() {
    XmlReaderTokenizer tokenizer(inputSource_);
    const auto start = position_;
    const auto end = tokenizer.ScanDelimitedSectionEnd(position_ + 4, "-->");
    if (end == std::string::npos) {
        const auto [line, column] = ComputeLineColumn(position_ + 4);
        throw XmlException("Unterminated comment", line, column);
    }
    const auto valueStart = position_ + 4;
    const auto valueEnd = end;
    position_ = end + 3;
    AdvanceLineInfoFromInputSource(inputSource_.get(), start, position_, lineNumber_, linePosition_);

    SetCurrentNode(XmlNodeType::Comment, {}, {}, {}, static_cast<int>(elementStack_.size()), false, {}, {}, valueStart, valueEnd, start, position_);
}

void XmlReader::ParseCData() {
    XmlReaderTokenizer tokenizer(inputSource_);
    const auto start = position_;
    const auto end = tokenizer.ScanDelimitedSectionEnd(position_ + 9, "]]>");
    if (end == std::string::npos) {
        const auto [line, column] = ComputeLineColumn(position_ + 9);
        throw XmlException("Unterminated CDATA section", line, column);
    }
    const auto valueStart = position_ + 9;
    const auto valueEnd = end;
    position_ = end + 3;
    AdvanceLineInfoFromInputSource(inputSource_.get(), start, position_, lineNumber_, linePosition_);

    SetCurrentNode(XmlNodeType::CDATA, {}, {}, {}, static_cast<int>(elementStack_.size()), false, {}, {}, valueStart, valueEnd, start, position_);
}

void XmlReader::ParseText() {
    XmlReaderTokenizer tokenizer(inputSource_);
    const int textDepth = static_cast<int>(elementStack_.size());
    const bool preserveSpace = !xmlSpacePreserveStack_.empty() && xmlSpacePreserveStack_.back();
    std::string textBuffer;
    std::size_t rawSegmentStart = std::string::npos;
    bool textBufferMaterialized = false;
    bool rawSegmentKnownNonWhitespace = false;

    auto isWhitespaceOnlySourceRange = [&](std::size_t start, std::size_t end) {
        for (std::size_t index = start; index < end; ++index) {
            if (!IsWhitespace(SourceCharAt(index))) {
                return false;
            }
        }
        return true;
    };

    auto flushText = [&](std::size_t segmentEnd) {
        if (rawSegmentStart == std::string::npos) {
            return;
        }

        bool isWhitespaceOnly = false;
        if (textBufferMaterialized) {
            isWhitespaceOnly = IsWhitespaceOnly(textBuffer);
        } else if (rawSegmentKnownNonWhitespace) {
            isWhitespaceOnly = false;
        } else {
            isWhitespaceOnly = isWhitespaceOnlySourceRange(rawSegmentStart, segmentEnd);
        }

        XmlNodeType nodeType = XmlNodeType::Text;
        if (isWhitespaceOnly) {
            nodeType = preserveSpace ? XmlNodeType::SignificantWhitespace : XmlNodeType::Whitespace;
        }

        if (textBufferMaterialized) {
            QueueNode(nodeType, {}, {}, textBuffer, textDepth, false, {}, {}, std::string::npos, std::string::npos, rawSegmentStart, segmentEnd);
        } else {
            QueueNode(nodeType, {}, {}, {}, textDepth, false, {}, {}, rawSegmentStart, segmentEnd, rawSegmentStart, segmentEnd);
        }
        textBuffer.clear();
        rawSegmentStart = std::string::npos;
        textBufferMaterialized = false;
        rawSegmentKnownNonWhitespace = false;
    };

    while (HasSourceChar(position_) && SourceCharAt(position_) != '<') {
        if (SourceCharAt(position_) != '&') {
            const auto segmentEnd = FindFirstOfFromInputSource(inputSource_.get(), "\n<&", position_);

            if (segmentEnd != std::string::npos && segmentEnd > position_) {
                if (rawSegmentStart == std::string::npos) {
                    rawSegmentStart = position_;
                }
                if (!rawSegmentKnownNonWhitespace && !IsWhitespace(SourceCharAt(position_))) {
                    rawSegmentKnownNonWhitespace = true;
                }
                if (textBufferMaterialized) {
                    AppendSourceSubstrTo(textBuffer, position_, segmentEnd - position_);
                }
                linePosition_ += segmentEnd - position_;
                position_ = segmentEnd;
                continue;
            }

            if (rawSegmentStart == std::string::npos) {
                rawSegmentStart = position_;
            }
            if (!rawSegmentKnownNonWhitespace && !IsWhitespace(SourceCharAt(position_))) {
                rawSegmentKnownNonWhitespace = true;
            }
            if (textBufferMaterialized) {
                textBuffer.push_back(SourceCharAt(position_));
            }
            if (SourceCharAt(position_) == '\n') {
                ++lineNumber_;
                linePosition_ = 1;
            } else {
                ++linePosition_;
            }
            ++position_;
            continue;
        }

        const auto entityStart = position_;
        const auto entityToken = tokenizer.ScanEntityReference(position_);
        if (entityToken.end == std::string::npos) {
            Throw("Unterminated entity reference");
        }

        const std::string rawEntity = entityToken.raw;
        const std::string entity = entityToken.name;
        position_ = entityToken.end;
        linePosition_ += entityToken.end - entityStart;

        const auto predefined = ResolvePredefinedEntityReferenceValueView(entity);
        if (!predefined.empty()) {
            if (rawSegmentStart == std::string::npos) {
                rawSegmentStart = entityStart;
            }
            if (!textBufferMaterialized) {
                textBuffer = SourceSubstr(rawSegmentStart, entityStart - rawSegmentStart);
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
                textBuffer = SourceSubstr(rawSegmentStart, entityStart - rawSegmentStart);
                textBufferMaterialized = true;
            }
            unsigned int codePoint = 0;
            if (!TryParseNumericEntityReferenceCodePoint(entity, codePoint)) {
                Throw("Invalid numeric entity reference: &" + entity + ';');
            }
            AppendCodePointUtf8(textBuffer, codePoint);
            continue;
        }

        if (declaredEntityNames_.find(entity) == declaredEntityNames_.end()) {
            Throw("Unknown entity reference: &" + entity + ';');
        }

        flushText(entityStart);

        std::string resolvedValue;
        const auto declared = entityDeclarations_.find(entity);
        if (declared != entityDeclarations_.end()) {
            resolvedValue = DecodeEntityText(
                declared->second,
                [this](const std::string& nestedEntity) -> std::optional<std::string> {
                    const auto found = entityDeclarations_.find(nestedEntity);
                    return found == entityDeclarations_.end() ? std::nullopt : std::optional<std::string>(found->second);
                },
                [this](const std::string& message) {
                    Throw(message);
                });
            if (settings_.MaxCharactersFromEntities != 0) {
                entityCharactersRead_ += resolvedValue.size();
                if (entityCharactersRead_ > settings_.MaxCharactersFromEntities) {
                    Throw("The XML document exceeds the configured MaxCharactersFromEntities limit");
                }
            }
        } else if (settings_.Resolver != nullptr) {
            const auto external = externalEntitySystemIds_.find(entity);
            if (external != externalEntitySystemIds_.end()) {
                const std::string absoluteUri = settings_.Resolver->ResolveUri(baseUri_, external->second);
                resolvedValue = settings_.Resolver->GetEntity(absoluteUri);
                if (settings_.MaxCharactersFromEntities != 0) {
                    entityCharactersRead_ += resolvedValue.size();
                    if (entityCharactersRead_ > settings_.MaxCharactersFromEntities) {
                        Throw("The XML document exceeds the configured MaxCharactersFromEntities limit");
                    }
                }
            }
        }

        QueueNode(XmlNodeType::EntityReference, entity, {}, resolvedValue, textDepth, false, {}, {}, std::string::npos, std::string::npos, entityStart, entityToken.end);
        if (!resolvedValue.empty()) {
            const XmlNodeType resolvedNodeType = IsWhitespaceOnly(resolvedValue)
                ? (preserveSpace ? XmlNodeType::SignificantWhitespace : XmlNodeType::Whitespace)
                : XmlNodeType::Text;
            QueueNode(resolvedNodeType, {}, {}, resolvedValue, textDepth + 1, false, {}, resolvedValue);
        }
        QueueNode(XmlNodeType::EndEntity, entity, {}, {}, textDepth, false, {}, {}, std::string::npos, std::string::npos, entityStart, entityToken.end);
    }

    flushText(position_);
    if (!TryConsumeBufferedNode()) {
        Throw("Unexpected empty text segment");
    }
}

bool XmlReader::TryReadSimpleElementContentAsString(std::string& result, std::size_t& closeStart, std::size_t& closeEnd) {
    if (inputSource_ == nullptr
        || currentElementStart_ == std::string::npos
        || currentContentStart_ == std::string::npos
        || currentIsEmptyElement_) {
        return false;
    }

    const auto bounds = EnsureCurrentElementXmlBounds();
    closeStart = bounds.first;
    closeEnd = bounds.second;
    if (closeStart == std::string::npos || closeEnd == std::string::npos || closeStart < currentContentStart_) {
        return false;
    }

    XmlReaderTokenizer tokenizer(inputSource_);
    result.clear();
    result.reserve(closeStart - currentContentStart_);

    std::size_t position = currentContentStart_;
    while (position < closeStart) {
        const std::size_t nextSpecial = FindFirstOfFromInputSource(inputSource_.get(), "<&", position);
        const std::size_t segmentEnd = nextSpecial == std::string::npos || nextSpecial > closeStart
            ? closeStart
            : nextSpecial;

        if (segmentEnd > position) {
            AppendSourceSubstrTo(result, position, segmentEnd - position);
            position = segmentEnd;
            continue;
        }

        if (position >= closeStart) {
            break;
        }

        if (SourceCharAt(position) == '&') {
            const auto entityToken = tokenizer.ScanEntityReference(position);
            if (entityToken.end == std::string::npos) {
                return false;
            }

            const std::string entity = entityToken.name;
            const auto predefined = ResolvePredefinedEntityReferenceValueView(entity);
            if (!predefined.empty()) {
                result.append(predefined.data(), predefined.size());
                position = entityToken.end;
                continue;
            }

            if (!entity.empty() && entity.front() == '#') {
                unsigned int codePoint = 0;
                if (!TryParseNumericEntityReferenceCodePoint(entity, codePoint)) {
                    return false;
                }
                AppendCodePointUtf8(result, codePoint);
                position = entityToken.end;
                continue;
            }

            return false;
        }

        if (tokenizer.StartsWith(position, "<![CDATA[")) {
            const auto end = tokenizer.ScanDelimitedSectionEnd(position + 9, "]]>");
            if (end == std::string::npos || end > closeStart) {
                return false;
            }
            AppendSourceSubstrTo(result, position + 9, end - (position + 9));
            position = end + 3;
            continue;
        }

        if (tokenizer.StartsWith(position, "<!--")) {
            const auto end = tokenizer.ScanDelimitedSectionEnd(position + 4, "-->");
            if (end == std::string::npos || end > closeStart) {
                return false;
            }
            position = end + 3;
            continue;
        }

        if (tokenizer.StartsWith(position, "<?")) {
            const auto end = tokenizer.ScanDelimitedSectionEnd(position + 2, "?>");
            if (end == std::string::npos || end > closeStart) {
                return false;
            }
            position = end + 2;
            continue;
        }

        return false;
    }

    return position == closeStart;
}

std::pair<std::size_t, std::size_t> XmlReader::EnsureCurrentElementXmlBounds() const {
    if (currentCloseStart_ == std::string::npos || currentCloseEnd_ == std::string::npos) {
        const auto bounds = FindElementXmlBounds(currentElementStart_, currentContentStart_, currentName_);
        currentCloseStart_ = bounds.first;
        currentCloseEnd_ = bounds.second;
    }
    return {currentCloseStart_, currentCloseEnd_};
}

std::pair<std::size_t, std::size_t> XmlReader::FindElementXmlBounds(
    std::size_t,
    std::size_t contentStart,
    const std::string& elementName) const {
    XmlReaderTokenizer tokenizer(inputSource_);
    std::vector<std::string> elementNames;
    elementNames.push_back(elementName);

    auto throwAt = [this](const std::string& message, std::size_t errorPosition) -> void {
        const auto [line, column] = ComputeLineColumn(errorPosition);
        throw XmlException(message, line, column);
    };

    std::size_t position = contentStart;
    std::size_t closeStart = std::string::npos;
    std::size_t closeEnd = std::string::npos;

    // Fast path for the common text-only case: the first markup after content start
    // is the matching closing tag for the current element.
    const std::size_t firstMarkup = FindInSource("<", contentStart);
    if (firstMarkup != std::string::npos && tokenizer.StartsWith(firstMarkup, "</")) {
        std::size_t closeCursor = firstMarkup + 2;
        const std::string closeName = tokenizer.ParseNameAt(closeCursor);
        if (closeName == elementName && tokenizer.ConsumeEndTagClose(closeCursor)) {
            return {firstMarkup, closeCursor};
        }
    }

    while (HasSourceChar(position)) {
        if (SourceCharAt(position) != '<') {
            ++position;
            continue;
        }

        if (tokenizer.StartsWith(position, "<!--")) {
            const auto end = tokenizer.ScanDelimitedSectionEnd(position + 4, "-->");
            if (end == std::string::npos) {
                throwAt("Unterminated comment", position + 4);
            }
            position = end + 3;
            continue;
        }

        if (tokenizer.StartsWith(position, "<![CDATA[")) {
            const auto end = tokenizer.ScanDelimitedSectionEnd(position + 9, "]]>");
            if (end == std::string::npos) {
                throwAt("Unterminated CDATA section", position + 9);
            }
            position = end + 3;
            continue;
        }

        if (tokenizer.StartsWith(position, "<?")) {
            const auto end = tokenizer.ScanDelimitedSectionEnd(position + 2, "?>");
            if (end == std::string::npos) {
                std::size_t errorPosition = position + 2;
                const std::size_t nameStart = errorPosition;
                std::string target = tokenizer.ParseNameAt(errorPosition);
                if (target.empty()) {
                    errorPosition = nameStart;
                }
                throwAt("Unterminated processing instruction", errorPosition);
            }
            position = end + 2;
            continue;
        }

        if (tokenizer.StartsWith(position, "</")) {
            closeStart = position;
            position += 2;
            const std::string closeName = tokenizer.ParseNameAt(position);
            if (closeName.empty()) {
                break;
            }

            const std::size_t errorPosition = position;
            while (HasSourceChar(position) && SourceCharAt(position) != '>') {
                ++position;
            }
            if (!HasSourceChar(position)) {
                break;
            }
            ++position;

            if (elementNames.empty()) {
                throwAt("Unexpected closing tag: </" + closeName + ">", errorPosition);
            }
            if (closeName != elementNames.back()) {
                throwAt(
                    "Mismatched closing tag. Expected </" + elementNames.back() + "> but found </" + closeName + ">",
                    errorPosition);
            }

            elementNames.pop_back();
            if (elementNames.empty()) {
                closeEnd = position;
                break;
            }
            continue;
        }

        if (tokenizer.StartsWith(position, "<!")) {
            const auto end = tokenizer.ScanDelimitedSectionEnd(position + 2, ">");
            if (end == std::string::npos) {
                break;
            }
            position = end + 1;
            continue;
        }

        std::size_t namePosition = position + 1;
        const std::string openName = tokenizer.ParseNameAt(namePosition);
        bool isEmptyElement = false;
        if (!tokenizer.SkipTag(position, isEmptyElement)) {
            break;
        }
        if (!isEmptyElement) {
            elementNames.push_back(openName);
        }
    }

    if (closeStart == std::string::npos || closeEnd == std::string::npos) {
        if (!elementNames.empty()) {
            throwAt("Unexpected end of input inside element <" + elementNames.back() + ">", position);
        }
        Throw("Unterminated element");
    }

    return {closeStart, closeEnd};
}

std::pair<std::string, std::string> XmlReader::CaptureElementXml(
    std::size_t elementStart,
    std::size_t contentStart) const {
    const auto [closeStart, closeEnd] = EnsureCurrentElementXmlBounds();
    return {
        SourceSubstr(contentStart, closeStart - contentStart),
        SourceSubstr(elementStart, closeEnd - elementStart)};
}

void XmlReader::ParseElement() {
    XmlReaderTokenizer tokenizer(inputSource_);
    const auto start = position_;
    ReadChar();
    const std::string name = ParseName();
    const std::string elementPrefix{SplitQualifiedNameView(name).first};
    const bool topLevelElement = elementStack_.empty();
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<AttributeValueMetadata> attributeValueMetadata;
    std::vector<std::pair<std::string, std::string>> localNamespaceDeclarations;
    bool retainLocalNamespaceDeclarationsForAttributes = false;
    const bool inheritedPreserveSpace = !xmlSpacePreserveStack_.empty() && xmlSpacePreserveStack_.back();
    bool preserveSpace = inheritedPreserveSpace;

    auto lookupNamespaceInScopes = [this, &localNamespaceDeclarations](const std::string& prefix) {
        for (auto it = localNamespaceDeclarations.rbegin(); it != localNamespaceDeclarations.rend(); ++it) {
            if (it->first == prefix) {
                return it->second;
            }
        }
        return LookupNamespaceUri(prefix);
    };

    auto setLocalNamespaceDeclaration = [&localNamespaceDeclarations](std::string prefix, std::string namespaceUri) {
        for (auto it = localNamespaceDeclarations.rbegin(); it != localNamespaceDeclarations.rend(); ++it) {
            if (it->first == prefix) {
                it->second = std::move(namespaceUri);
                return;
            }
        }

        localNamespaceDeclarations.emplace_back(std::move(prefix), std::move(namespaceUri));
    };

    auto sourceRangeEqualsLiteral = [this](std::size_t rangeStart, std::size_t rangeEnd, std::string_view literal) {
        if (rangeStart == std::string::npos || rangeEnd == std::string::npos || rangeEnd < rangeStart) {
            return false;
        }

        const std::size_t length = rangeEnd - rangeStart;
        if (length != literal.size()) {
            return false;
        }

        for (std::size_t index = 0; index < length; ++index) {
            if (SourceCharAt(rangeStart + index) != literal[index]) {
                return false;
            }
        }

        return true;
    };

    while (true) {
        const auto whitespaceStart = position_;
        tokenizer.SkipWhitespace(position_);
        AdvanceLineInfoFromInputSource(inputSource_.get(), whitespaceStart, position_, lineNumber_, linePosition_);
        bool isEmptyElement = false;
        const auto closeStart = position_;
        if (tokenizer.ConsumeStartTagClose(position_, isEmptyElement)) {
            AdvanceLineInfoFromInputSource(inputSource_.get(), closeStart, position_, lineNumber_, linePosition_);
            if (isEmptyElement) {
                SetCurrentNode(
                    XmlNodeType::Element,
                    name,
                    lookupNamespaceInScopes(elementPrefix),
                    {},
                    static_cast<int>(elementStack_.size()),
                    true,
                    {},
                    {},
                    std::string::npos,
                    std::string::npos,
                    start,
                    position_,
                    std::move(attributes),
                    {},
                    start,
                    position_);
                currentLocalNamespaceDeclarations_ = retainLocalNamespaceDeclarationsForAttributes
                    ? localNamespaceDeclarations
                    : std::vector<std::pair<std::string, std::string>>{};
                currentAttributeValueMetadata_ = std::move(attributeValueMetadata);
                RefreshCurrentEarliestRetainedAttributeValueStart();
                if (topLevelElement) {
                    sawRootElement_ = true;
                    completedRootElement_ = true;
                }
                return;
            }
            break;
        }

        if (!tokenizer.HasChar(position_)) {
            Throw("Unexpected end of XML document");
        }

        const auto attributeStart = position_;
        auto attributeToken = tokenizer.ParseAttributeAssignment(position_);
        AdvanceLineInfoFromInputSource(inputSource_.get(), attributeStart, position_, lineNumber_, linePosition_);
        if (attributeToken.name.empty()) {
            if (tokenizer.CharAt(position_) == '/' || tokenizer.CharAt(position_) == '>') {
                Throw("Expected '>' after element attributes");
            }
            Throw("Invalid XML name");
        }
        if (!attributeToken.valid) {
            if (!attributeToken.sawEquals) {
                if (tokenizer.HasChar(position_)) {
                    ++position_;
                }
                Throw("Expected '=' after attribute name");
            }
            if (tokenizer.HasChar(position_)) {
                ++position_;
            }
            Throw("Expected quoted value");
        }
        const auto rawValueStart = attributeToken.rawValueStart;
        const auto rawValueEnd = attributeToken.rawValueEnd;
        const bool needsDecoding = SourceRangeContains(rawValueStart, rawValueEnd, '&');
        attributes.emplace_back(std::move(attributeToken.name), std::string{});
        const std::string& attributeName = attributes.back().first;
        const auto attributeColon = attributeName.find(':');
        if (attributeColon != std::string::npos
            && std::string_view(attributeName).substr(0, attributeColon) != "xmlns") {
            retainLocalNamespaceDeclarationsForAttributes = true;
        }
        attributeValueMetadata.push_back(AttributeValueMetadata{
            rawValueStart,
            rawValueEnd,
            static_cast<unsigned char>(needsDecoding ? kAttributeValueNeedsDecoding : 0)});
        if (attributeName == "xmlns") {
            std::string attributeValue = SourceSubstr(rawValueStart, rawValueEnd - rawValueStart);
            if (needsDecoding) {
                attributeValue = DecodeEntities(attributeValue);
            }
            setLocalNamespaceDeclaration({}, std::move(attributeValue));
        } else if (attributeName.rfind("xmlns:", 0) == 0) {
            std::string attributeValue = SourceSubstr(rawValueStart, rawValueEnd - rawValueStart);
            if (needsDecoding) {
                attributeValue = DecodeEntities(attributeValue);
            }
            setLocalNamespaceDeclaration(attributeName.substr(6), std::move(attributeValue));
        } else if (attributeName == "xml:space") {
            if (!needsDecoding) {
                if (sourceRangeEqualsLiteral(rawValueStart, rawValueEnd, "preserve")) {
                    preserveSpace = true;
                } else if (sourceRangeEqualsLiteral(rawValueStart, rawValueEnd, "default")) {
                    preserveSpace = false;
                }
            } else {
                std::string attributeValue = SourceSubstr(rawValueStart, rawValueEnd - rawValueStart);
                attributeValue = DecodeEntities(attributeValue);
                if (IsXmlSpacePreserve(attributeValue)) {
                    preserveSpace = true;
                } else if (IsXmlSpaceDefault(attributeValue)) {
                    preserveSpace = false;
                }
            }
        }
    }

    const bool pushedNamespaceScope = !localNamespaceDeclarations.empty();
    const std::string elementNamespaceUri = lookupNamespaceInScopes(elementPrefix);
    currentLocalNamespaceDeclarations_ = retainLocalNamespaceDeclarationsForAttributes
        ? localNamespaceDeclarations
        : std::vector<std::pair<std::string, std::string>>{};
    if (pushedNamespaceScope) {
        std::unordered_map<std::string, std::string> localScope;
        localScope.reserve(localNamespaceDeclarations.size());
        for (auto& [prefix, namespaceUri] : localNamespaceDeclarations) {
            localScope[std::move(prefix)] = std::move(namespaceUri);
        }
        namespaceScopes_.push_back(std::move(localScope));
    }
    SetCurrentNode(
        XmlNodeType::Element,
        name,
        std::move(elementNamespaceUri),
        {},
        static_cast<int>(elementStack_.size()),
        false,
        {},
        {},
        std::string::npos,
        std::string::npos,
        start,
        position_,
        std::move(attributes),
        {},
        start,
        position_);
    currentAttributeValueMetadata_ = std::move(attributeValueMetadata);
    RefreshCurrentEarliestRetainedAttributeValueStart();
    const bool pushedXmlSpacePreserve = preserveSpace != inheritedPreserveSpace;
    if (pushedXmlSpacePreserve) {
        xmlSpacePreserveStack_.push_back(preserveSpace);
    }
    namespaceScopeFramePushedStack_.push_back(pushedNamespaceScope);
    xmlSpacePreserveFramePushedStack_.push_back(pushedXmlSpacePreserve);
    elementStack_.push_back(name);
    if (topLevelElement) {
        sawRootElement_ = true;
    }
}

void XmlReader::ParseEndElement() {
    XmlReaderTokenizer tokenizer(inputSource_);
    const auto start = position_;
    std::size_t cursor = position_ + 2;
    const std::string name = tokenizer.ParseNameAt(cursor);
    const std::string namePrefix{SplitQualifiedNameView(name).first};
    position_ = cursor;
    if (elementStack_.empty()) {
        Throw("Unexpected closing tag: </" + name + ">");
    }
    if (elementStack_.back() != name) {
        Throw("Mismatched closing tag. Expected </" + elementStack_.back() + "> but found </" + name + ">");
    }
    if (!tokenizer.ConsumeEndTagClose(cursor)) {
        const std::size_t errorPosition = tokenizer.HasChar(cursor) ? cursor + 1 : cursor;
        const auto [line, column] = ComputeLineColumn(errorPosition);
        throw XmlException("Expected '>' after closing tag", line, column);
    }
    position_ = cursor;
    AdvanceLineInfoFromInputSource(inputSource_.get(), start, position_, lineNumber_, linePosition_);

    const int depth = static_cast<int>(elementStack_.size()) - 1;
    const std::string namespaceUri = LookupNamespaceUri(namePrefix);
    bool popNamespaceScope = false;
    if (!namespaceScopeFramePushedStack_.empty()) {
        popNamespaceScope = namespaceScopeFramePushedStack_.back();
        namespaceScopeFramePushedStack_.pop_back();
    }
    bool popXmlSpacePreserve = false;
    if (!xmlSpacePreserveFramePushedStack_.empty()) {
        popXmlSpacePreserve = xmlSpacePreserveFramePushedStack_.back();
        xmlSpacePreserveFramePushedStack_.pop_back();
    }
    elementStack_.pop_back();
    if (popNamespaceScope && namespaceScopes_.size() > 1) {
        namespaceScopes_.pop_back();
    }
    if (popXmlSpacePreserve && xmlSpacePreserveStack_.size() > 1) {
        xmlSpacePreserveStack_.pop_back();
    }
    if (elementStack_.empty() && sawRootElement_) {
        completedRootElement_ = true;
    }
    SetCurrentNode(
        XmlNodeType::EndElement,
        std::move(name),
        namespaceUri,
        {},
        depth,
        false,
        {},
        {},
        std::string::npos,
        std::string::npos,
        start,
        position_);
}

std::string XmlReader::LookupNamespaceUri(const std::string& prefix) const {
    for (auto it = namespaceScopes_.rbegin(); it != namespaceScopes_.rend(); ++it) {
        const auto found = it->find(prefix);
        if (found != it->end()) {
            return found->second;
        }
    }

    return {};
}

const std::vector<std::pair<std::string, std::string>>& XmlReader::CurrentAttributes() const {
    return currentAttributes_;
}

std::string XmlReader::CurrentLocalName() const {
    return std::string{SplitQualifiedNameView(currentName_).second};
}

std::string XmlReader::CurrentPrefix() const {
    return std::string{SplitQualifiedNameView(currentName_).first};
}

std::string XmlReader::CurrentAttributeLocalName(std::size_t index) const {
    if (index >= currentAttributes_.size()) {
        return {};
    }
    return std::string{SplitQualifiedNameView(currentAttributes_[index].first).second};
}

std::string XmlReader::CurrentAttributePrefix(std::size_t index) const {
    if (index >= currentAttributes_.size()) {
        return {};
    }
    return std::string{SplitQualifiedNameView(currentAttributes_[index].first).first};
}

const std::string& XmlReader::CurrentAttributeNamespaceUri(std::size_t index) const {
    if (index >= currentAttributes_.size()) {
        return EmptyString();
    }

    if (index >= currentAttributeNamespaceUris_.size()) {
        currentAttributeNamespaceUris_.resize(currentAttributes_.size());
    }
    if (index >= currentAttributeNamespaceUrisResolved_.size()) {
        currentAttributeNamespaceUrisResolved_.resize(currentAttributes_.size(), static_cast<unsigned char>(0));
    }
    if (currentAttributeNamespaceUrisResolved_[index] != 0) {
        return currentAttributeNamespaceUris_[index];
    }

    const std::string& attributeName = currentAttributes_[index].first;
    const auto colon = attributeName.find(':');
    const bool hasPrefix = colon != std::string::npos;
    const std::string_view attributePrefix = hasPrefix
        ? std::string_view(attributeName).substr(0, colon)
        : std::string_view{};

    std::string namespaceUri;
    if (attributeName == "xmlns" || attributePrefix == "xmlns") {
        namespaceUri = "http://www.w3.org/2000/xmlns/";
    } else if (hasPrefix) {
        for (const auto& entry : currentLocalNamespaceDeclarations_) {
            if (std::string_view(entry.first) == attributePrefix) {
                namespaceUri = entry.second;
                break;
            }
        }
        if (namespaceUri.empty()) {
            const std::string prefix(attributePrefix);
            namespaceUri = LookupNamespaceUri(prefix);
        }
    }

    currentAttributeNamespaceUris_[index] = std::move(namespaceUri);
    currentAttributeNamespaceUrisResolved_[index] = static_cast<unsigned char>(1);
    return currentAttributeNamespaceUris_[index];
}

void XmlReader::RefreshCurrentEarliestRetainedAttributeValueStart() const noexcept {
    currentEarliestRetainedAttributeValueStart_ = std::string::npos;
    for (const auto& metadata : currentAttributeValueMetadata_) {
        if ((metadata.flags & kAttributeValueDecoded) != 0) {
            continue;
        }
        if (metadata.valueStart == std::string::npos
            || metadata.valueEnd == std::string::npos
            || metadata.valueEnd < metadata.valueStart) {
            continue;
        }
        if (currentEarliestRetainedAttributeValueStart_ == std::string::npos
            || metadata.valueStart < currentEarliestRetainedAttributeValueStart_) {
            currentEarliestRetainedAttributeValueStart_ = metadata.valueStart;
        }
    }
}

void XmlReader::EnsureCurrentAttributeValueDecoded(std::size_t index) const {
    if (index >= currentAttributes_.size()) {
        return;
    }
    if (index >= currentAttributeValueMetadata_.size()) {
        return;
    }

    auto& metadata = currentAttributeValueMetadata_[index];
    if ((metadata.flags & kAttributeValueDecoded) != 0) {
        return;
    }

    currentAttributes_[index].second.clear();
    if (metadata.valueStart != std::string::npos
        && metadata.valueEnd != std::string::npos
        && metadata.valueEnd >= metadata.valueStart) {
        if ((metadata.flags & kAttributeValueNeedsDecoding) != 0) {
            AppendDecodedSourceRangeTo(currentAttributes_[index].second, metadata.valueStart, metadata.valueEnd);
        } else {
            currentAttributes_[index].second = SourceSubstr(metadata.valueStart, metadata.valueEnd - metadata.valueStart);
        }
    }
    metadata.flags = static_cast<unsigned char>(metadata.flags | kAttributeValueDecoded);

    if (metadata.valueStart != std::string::npos
        && metadata.valueEnd != std::string::npos
        && metadata.valueEnd >= metadata.valueStart
        && metadata.valueStart == currentEarliestRetainedAttributeValueStart_) {
        RefreshCurrentEarliestRetainedAttributeValueStart();
    }
}

const std::string& XmlReader::CurrentAttributeValue(std::size_t index) const {
    EnsureCurrentAttributeValueDecoded(index);
    return currentAttributes_[index].second;
}

void XmlReader::AppendCurrentAttributesForLoad(XmlElement& element) {
    if (currentAttributes_.empty()) {
        return;
    }

    const bool hasMetadataForAllAttributes = currentAttributeValueMetadata_.size() == currentAttributes_.size();
    std::size_t totalStorageBytes = 0;
    if (hasMetadataForAllAttributes) {
        for (std::size_t index = 0; index < currentAttributes_.size(); ++index) {
            const auto& attribute = currentAttributes_[index];
            const auto& metadata = currentAttributeValueMetadata_[index];
            totalStorageBytes += attribute.first.size();
            if ((metadata.flags & kAttributeValueDecoded) == 0
                && metadata.valueStart != std::string::npos
                && metadata.valueEnd != std::string::npos
                && metadata.valueEnd >= metadata.valueStart) {
                totalStorageBytes += metadata.valueEnd - metadata.valueStart;
                continue;
            }
            totalStorageBytes += attribute.second.size();
        }
    } else {
        for (std::size_t index = 0; index < currentAttributes_.size(); ++index) {
            const auto& attribute = currentAttributes_[index];
            totalStorageBytes += attribute.first.size();
            if (index < currentAttributeValueMetadata_.size()) {
                const auto& metadata = currentAttributeValueMetadata_[index];
                if ((metadata.flags & kAttributeValueDecoded) == 0
                    && metadata.valueStart != std::string::npos
                    && metadata.valueEnd != std::string::npos
                    && metadata.valueEnd >= metadata.valueStart) {
                    totalStorageBytes += metadata.valueEnd - metadata.valueStart;
                    continue;
                }
            } else {
                EnsureCurrentAttributeValueDecoded(index);
            }
            totalStorageBytes += attribute.second.size();
        }
    }

    currentEarliestRetainedAttributeValueStart_ = std::string::npos;

    element.ReserveAttributesForLoad(currentAttributes_.size(), totalStorageBytes);
    auto& pendingStorage = element.pendingLoadAttributeStorage_;
    auto& pendingAttributes = element.pendingLoadAttributes_;
    const auto appendPendingAttribute = [&pendingAttributes](
        std::size_t nameOffset,
        std::size_t nameLength,
        std::size_t valueOffset,
        std::size_t valueLength) {
        pendingAttributes.push_back(XmlElement::PendingLoadAttribute{
            nameOffset,
            nameLength,
            valueOffset,
            valueLength});
    };

    if (hasMetadataForAllAttributes) {
        for (std::size_t index = 0; index < currentAttributes_.size(); ++index) {
            const auto& attribute = currentAttributes_[index];
            const auto& metadata = currentAttributeValueMetadata_[index];
            const std::size_t nameOffset = pendingStorage.size();
            pendingStorage += attribute.first;

            const std::size_t valueOffset = pendingStorage.size();
            std::size_t valueLength = attribute.second.size();
            if ((metadata.flags & kAttributeValueDecoded) == 0
                && metadata.valueStart != std::string::npos
                && metadata.valueEnd != std::string::npos
                && metadata.valueEnd >= metadata.valueStart) {
                const std::size_t rawLength = metadata.valueEnd - metadata.valueStart;
                if ((metadata.flags & kAttributeValueNeedsDecoding) == 0) {
                    valueLength = rawLength;
                    AppendSourceSubstrTo(pendingStorage, metadata.valueStart, valueLength);
                } else {
                    valueLength = AppendDecodedSourceRangeTo(pendingStorage, metadata.valueStart, metadata.valueEnd);
                }

                appendPendingAttribute(nameOffset, attribute.first.size(), valueOffset, valueLength);
                continue;
            }

            pendingStorage += attribute.second;
            appendPendingAttribute(nameOffset, attribute.first.size(), valueOffset, valueLength);
        }
        return;
    }

    for (std::size_t index = 0; index < currentAttributes_.size(); ++index) {
        const auto& attribute = currentAttributes_[index];
        const std::size_t nameOffset = pendingStorage.size();
        pendingStorage += attribute.first;

        const std::size_t valueOffset = pendingStorage.size();
        std::size_t valueLength = attribute.second.size();

        if (index < currentAttributeValueMetadata_.size()) {
            const auto& metadata = currentAttributeValueMetadata_[index];
            if ((metadata.flags & kAttributeValueDecoded) == 0
                && metadata.valueStart != std::string::npos
                && metadata.valueEnd != std::string::npos
                && metadata.valueEnd >= metadata.valueStart) {
                const std::size_t rawLength = metadata.valueEnd - metadata.valueStart;
                if ((metadata.flags & kAttributeValueNeedsDecoding) == 0) {
                    valueLength = rawLength;
                    AppendSourceSubstrTo(pendingStorage, metadata.valueStart, valueLength);
                } else {
                    valueLength = AppendDecodedSourceRangeTo(pendingStorage, metadata.valueStart, metadata.valueEnd);
                }

                appendPendingAttribute(nameOffset, attribute.first.size(), valueOffset, valueLength);
                continue;
            }
        }

        pendingStorage += attribute.second;
        appendPendingAttribute(nameOffset, attribute.first.size(), valueOffset, valueLength);
    }
}

void XmlReader::InitializeInputState() {
    namespaceScopes_.clear();
    namespaceScopeFramePushedStack_.clear();
    xmlSpacePreserveStack_.clear();
    xmlSpacePreserveFramePushedStack_.clear();
    namespaceScopes_.push_back({});
    namespaceScopes_.back().emplace("xml", "http://www.w3.org/XML/1998/namespace");
    namespaceScopes_.back().emplace("xmlns", "http://www.w3.org/2000/xmlns/");
    xmlSpacePreserveStack_.push_back(false);
    if (HasSourceChar(2)
        && static_cast<unsigned char>(SourceCharAt(0)) == 0xEF
        && static_cast<unsigned char>(SourceCharAt(1)) == 0xBB
        && static_cast<unsigned char>(SourceCharAt(2)) == 0xBF) {
        position_ = 3;
    }
    eof_ = !HasSourceChar(position_);
    lineNumber_ = 1;
    linePosition_ = 1;
    discardedSourceOffset_ = 0;
    discardedLineNumber_ = 1;
    discardedLinePosition_ = 1;
}

XmlReader XmlReader::CreateFromValidatedString(std::shared_ptr<const std::string> xml, const XmlReaderSettings& settings) {
    const std::size_t sourceSize = xml == nullptr ? 0 : xml->size();
    XmlReader reader(settings);
    reader.inputSource_ = std::make_shared<StringXmlReaderInputSource>(std::move(xml));
    reader.InitializeInputState();
    if (reader.settings_.MaxCharactersInDocument != 0
        && sourceSize > reader.position_
        && sourceSize - reader.position_ > reader.settings_.MaxCharactersInDocument) {
        throw XmlException("The XML document exceeds the configured MaxCharactersInDocument limit");
    }
    return reader;
}

XmlReader XmlReader::Create(const std::string& xml, const XmlReaderSettings& settings) {
    auto sourceText = std::make_shared<std::string>(xml);
    ValidateXmlReaderInputAgainstSchemas(*sourceText, settings);
    return CreateFromValidatedString(sourceText, settings);
}

XmlReader XmlReader::Create(std::istream& stream, const XmlReaderSettings& settings) {
    if (settings.Validation == ValidationType::Schema) {
        const auto startPosition = stream.tellg();
        if (startPosition != std::streampos(-1)) {
            XmlReaderSettings validationSettings = settings;
            validationSettings.Validation = ValidationType::None;

            auto validatingReader = XmlReader::Create(stream, validationSettings);
            ValidateXmlReaderInputAgainstSchemas(validatingReader, settings);

            stream.clear();
            stream.seekg(startPosition);
            if (stream.good()) {
                XmlReader reader(settings);
                std::string initialBuffer;
                if (settings.MaxCharactersInDocument != 0) {
                    initialBuffer = ReadStreamPrefix(stream, settings.MaxCharactersInDocument + 4);
                    const std::size_t bomLength = GetUtf8BomLength(initialBuffer);
                    if (initialBuffer.size() > bomLength
                        && initialBuffer.size() - bomLength > settings.MaxCharactersInDocument) {
                        throw XmlException("The XML document exceeds the configured MaxCharactersInDocument limit");
                    }
                }
                reader.inputSource_ = std::make_shared<StreamXmlReaderInputSource>(stream, std::move(initialBuffer));
                reader.InitializeInputState();
                return reader;
            }

            stream.clear();
        }

        const auto replayPath = SpoolStreamToTemporaryFile(stream);
        try {
            std::ifstream validationStream(replayPath, std::ios::binary);
            if (!validationStream) {
                throw XmlException("Failed to open temporary XML replay file");
            }

            XmlReaderSettings validationSettings = settings;
            validationSettings.Validation = ValidationType::None;
            auto validatingReader = XmlReader::Create(validationStream, validationSettings);
            ValidateXmlReaderInputAgainstSchemas(validatingReader, settings);

            auto replayStream = OpenTemporaryXmlReplayStream(replayPath);
            XmlReader reader(settings);
            std::string initialBuffer;
            if (settings.MaxCharactersInDocument != 0) {
                initialBuffer = ReadStreamPrefix(*replayStream, settings.MaxCharactersInDocument + 4);
                const std::size_t bomLength = GetUtf8BomLength(initialBuffer);
                if (initialBuffer.size() > bomLength
                    && initialBuffer.size() - bomLength > settings.MaxCharactersInDocument) {
                    throw XmlException("The XML document exceeds the configured MaxCharactersInDocument limit");
                }
            }
            reader.inputSource_ = std::make_shared<StreamXmlReaderInputSource>(replayStream, std::move(initialBuffer));
            reader.InitializeInputState();
            return reader;
        } catch (...) {
            std::error_code error;
            std::filesystem::remove(replayPath, error);
            throw;
        }
    }

    XmlReader reader(settings);
    std::string initialBuffer;
    if (settings.MaxCharactersInDocument != 0) {
        initialBuffer = ReadStreamPrefix(stream, settings.MaxCharactersInDocument + 4);
        const std::size_t bomLength = GetUtf8BomLength(initialBuffer);
        if (initialBuffer.size() > bomLength
            && initialBuffer.size() - bomLength > settings.MaxCharactersInDocument) {
            throw XmlException("The XML document exceeds the configured MaxCharactersInDocument limit");
        }
    }
    reader.inputSource_ = std::make_shared<StreamXmlReaderInputSource>(stream, std::move(initialBuffer));
    reader.InitializeInputState();
    return reader;
}

XmlReader XmlReader::CreateFromFile(const std::string& path, const XmlReaderSettings& settings) {
    if (settings.Validation == ValidationType::Schema) {
        std::ifstream validationStream(std::filesystem::path(path), std::ios::binary);
        if (!validationStream) {
            throw XmlException("Failed to open XML file: " + path);
        }

        XmlReaderSettings validationSettings = settings;
        validationSettings.Validation = ValidationType::None;
        auto validatingReader = XmlReader::Create(validationStream, validationSettings);
        ValidateXmlReaderInputAgainstSchemas(validatingReader, settings);

        auto stream = std::make_shared<std::ifstream>(std::filesystem::path(path), std::ios::binary);
        if (!*stream) {
            throw XmlException("Failed to open XML file: " + path);
        }

        XmlReader reader(settings);
        reader.inputSource_ = std::make_shared<StreamXmlReaderInputSource>(
            std::static_pointer_cast<std::istream>(stream));
        reader.InitializeInputState();
        reader.baseUri_ = std::filesystem::absolute(std::filesystem::path(path)).string();
        return reader;
    }

    auto stream = std::make_shared<std::ifstream>(std::filesystem::path(path), std::ios::binary);
    if (!*stream) {
        throw XmlException("Failed to open XML file: " + path);
    }

    std::string initialBuffer;
    if (settings.MaxCharactersInDocument != 0) {
        initialBuffer = ReadStreamPrefix(*stream, settings.MaxCharactersInDocument + 4);
        const std::size_t bomLength = GetUtf8BomLength(initialBuffer);
        if (initialBuffer.size() > bomLength
            && initialBuffer.size() - bomLength > settings.MaxCharactersInDocument) {
            throw XmlException("The XML document exceeds the configured MaxCharactersInDocument limit");
        }
    }

    XmlReader reader(settings);
    reader.inputSource_ = std::make_shared<StreamXmlReaderInputSource>(
        std::static_pointer_cast<std::istream>(stream),
        std::move(initialBuffer));
    reader.InitializeInputState();
    reader.baseUri_ = std::filesystem::absolute(std::filesystem::path(path)).string();
    return reader;
}

bool XmlReader::Read() {
    XmlReaderTokenizer tokenizer(inputSource_);
    if (closed_) {
        return false;
    }
    attributeIndex_ = -1;
    started_ = true;

    while (true) {
        ResetCurrentNode();
        MaybeDiscardSourcePrefix();
        if (TryConsumeBufferedNode()) {
            if (settings_.IgnoreWhitespace
                && (currentNodeType_ == XmlNodeType::Whitespace || currentNodeType_ == XmlNodeType::SignificantWhitespace)) {
                continue;
            }
            FinalizeSuccessfulRead();
            return true;
        }

        if (!HasSourceChar(position_)) {
            MaybeDiscardSourcePrefix();
            eof_ = true;
            return false;
        }

        if (elementStack_.empty()) {
            const auto markupKind = tokenizer.ClassifyMarkup(position_);
            if (settings_.Conformance == ConformanceLevel::Document && completedRootElement_) {
                if (IsWhitespace(Peek())) {
                    ParseText();
                    if (settings_.IgnoreWhitespace
                        && (currentNodeType_ == XmlNodeType::Whitespace || currentNodeType_ == XmlNodeType::SignificantWhitespace)) {
                        continue;
                    }
                    FinalizeSuccessfulRead();
                    return true;
                }

                if (markupKind == XmlMarkupKind::Comment) {
                    ParseComment();
                    if (settings_.IgnoreComments) {
                        continue;
                    }
                    FinalizeSuccessfulRead();
                    return true;
                }

                if (markupKind == XmlMarkupKind::ProcessingInstruction) {
                    ParseProcessingInstruction();
                    if (settings_.IgnoreProcessingInstructions) {
                        continue;
                    }
                    FinalizeSuccessfulRead();
                    return true;
                }

                if (markupKind == XmlMarkupKind::DocumentType) {
                    Throw("DOCTYPE must appear before the root element");
                }

                Throw("Unexpected content after the root element");
            }

            if (settings_.Conformance == ConformanceLevel::Document
                && markupKind == XmlMarkupKind::DocumentType
                && sawDocumentType_) {
                Throw("XML document can only contain a single DOCTYPE declaration");
            }
        }

        if (Peek() != '<') {
            ParseText();
            if (elementStack_.empty()
                && currentNodeType_ != XmlNodeType::Whitespace
                && currentNodeType_ != XmlNodeType::SignificantWhitespace) {
                xmlDeclarationAllowed_ = false;
            }
            if (settings_.IgnoreWhitespace
                && (currentNodeType_ == XmlNodeType::Whitespace || currentNodeType_ == XmlNodeType::SignificantWhitespace)) {
                continue;
            }
            FinalizeSuccessfulRead();
            return true;
        }

        switch (tokenizer.ClassifyMarkup(position_)) {
        case XmlMarkupKind::XmlDeclaration:
            if (settings_.Conformance == ConformanceLevel::Fragment) {
                Throw("XML declaration is only allowed in document conformance mode");
            }
            ParseDeclaration();
            break;
        case XmlMarkupKind::Comment:
            ParseComment();
            if (elementStack_.empty()) {
                xmlDeclarationAllowed_ = false;
            }
            if (settings_.IgnoreComments) {
                continue;
            }
            break;
        case XmlMarkupKind::CData:
            ParseCData();
            break;
        case XmlMarkupKind::ProcessingInstruction:
            ParseProcessingInstruction();
            if (elementStack_.empty()) {
                xmlDeclarationAllowed_ = false;
            }
            if (settings_.IgnoreProcessingInstructions) {
                continue;
            }
            break;
        case XmlMarkupKind::EndTag:
            ParseEndElement();
            break;
        case XmlMarkupKind::DocumentType:
            if (settings_.Conformance == ConformanceLevel::Fragment) {
                Throw("DOCTYPE is only allowed in document conformance mode");
            }
            ParseDocumentType();
            if (elementStack_.empty()) {
                xmlDeclarationAllowed_ = false;
            }
            if (settings_.DtdProcessing == DtdProcessing::Ignore) {
                continue;
            }
            break;
        case XmlMarkupKind::UnsupportedDeclaration:
            Throw("Unsupported markup declaration");
            break;
        case XmlMarkupKind::Element:
            ParseElement();
            xmlDeclarationAllowed_ = false;
            if (settings_.Conformance == ConformanceLevel::Document && elementStack_.empty() && sawRootElement_ && completedRootElement_) {
                // Existing document-mode validation remains enforced by top-level checks.
            }
            break;
        case XmlMarkupKind::None:
            Throw("Unexpected content after the root element");
            break;
        }

        FinalizeSuccessfulRead();
        return true;
    }
}

bool XmlReader::IsEOF() const noexcept {
    return eof_;
}

void XmlReader::Close() {
    closed_ = true;
    eof_ = true;
    started_ = true;
    attributeIndex_ = -1;
    ResetCurrentNode();
    bufferedNodes_.clear();
    elementStack_.clear();
    namespaceScopes_.clear();
    namespaceScopeFramePushedStack_.clear();
    xmlSpacePreserveStack_.clear();
    xmlSpacePreserveFramePushedStack_.clear();
}

XmlNodeType XmlReader::NodeType() const {
    if (attributeIndex_ >= 0) {
        return XmlNodeType::Attribute;
    }
    if (!started_ || eof_) {
        return XmlNodeType::None;
    }
    return currentNodeType_;
}

const std::string& XmlReader::Name() const {
    if (attributeIndex_ >= 0) {
        return CurrentAttributes()[static_cast<std::size_t>(attributeIndex_)].first;
    }
    if (!started_ || eof_) {
        return EmptyString();
    }
    return currentName_;
}

std::string XmlReader::LocalName() const {
    if (attributeIndex_ >= 0) {
        return CurrentAttributeLocalName(static_cast<std::size_t>(attributeIndex_));
    }
    if (!started_ || eof_) {
        return {};
    }
    return CurrentLocalName();
}

std::string XmlReader::Prefix() const {
    if (attributeIndex_ >= 0) {
        return CurrentAttributePrefix(static_cast<std::size_t>(attributeIndex_));
    }
    if (!started_ || eof_) {
        return {};
    }
    return CurrentPrefix();
}

const std::string& XmlReader::NamespaceURI() const {
    if (attributeIndex_ >= 0) {
        return CurrentAttributeNamespaceUri(static_cast<std::size_t>(attributeIndex_));
    }
    return currentNamespaceUri_;
}

const std::string& XmlReader::Value() const {
    if (attributeIndex_ >= 0) {
        return CurrentAttributeValue(static_cast<std::size_t>(attributeIndex_));
    }
    if (!started_ || eof_) {
        return EmptyString();
    }
    if (currentNodeType_ == XmlNodeType::XmlDeclaration && currentValue_.empty()) {
        currentValue_ = BuildDeclarationValue(
            currentDeclarationVersion_,
            currentDeclarationEncoding_,
            currentDeclarationStandalone_);
    }
    if (currentValue_.empty()
        && currentValueStart_ != std::string::npos
        && currentValueEnd_ != std::string::npos) {
        currentValue_ = SourceSubstr(currentValueStart_, currentValueEnd_ - currentValueStart_);
    }
    return currentValue_;
}

int XmlReader::Depth() const noexcept {
    if (!started_ || eof_) {
        return 0;
    }
    return currentDepth_ + (attributeIndex_ >= 0 ? 1 : 0);
}

bool XmlReader::IsEmptyElement() const noexcept {
    return NodeType() == XmlNodeType::Element && started_ && !eof_ && currentIsEmptyElement_;
}

bool XmlReader::HasValue() const noexcept {
    switch (NodeType()) {
    case XmlNodeType::Attribute:
    case XmlNodeType::Text:
    case XmlNodeType::CDATA:
    case XmlNodeType::EntityReference:
    case XmlNodeType::ProcessingInstruction:
    case XmlNodeType::Comment:
    case XmlNodeType::Whitespace:
    case XmlNodeType::SignificantWhitespace:
    case XmlNodeType::XmlDeclaration:
        return true;
    default:
        return false;
    }
}

int XmlReader::AttributeCount() const noexcept {
    return static_cast<int>(CurrentAttributes().size());
}

bool XmlReader::HasAttributes() const noexcept {
    return !CurrentAttributes().empty();
}

ReadState XmlReader::GetReadState() const noexcept {
    if (closed_) return ReadState::Closed;
    if (eof_) return ReadState::EndOfFile;
    if (!started_) return ReadState::Initial;
    return ReadState::Interactive;
}

bool XmlReader::HasLineInfo() const noexcept {
    return true;
}

std::size_t XmlReader::LineNumber() const noexcept {
    return lineNumber_;
}

std::size_t XmlReader::LinePosition() const noexcept {
    return linePosition_;
}

std::string XmlReader::GetAttribute(const std::string& name) const {
    const auto& attributes = CurrentAttributes();
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        const std::string_view attributeName(attributes[i].first);
        if (attributeName == name) {
            return CurrentAttributeValue(i);
        }
        if (SplitQualifiedNameView(attributeName).second == name) {
            return CurrentAttributeValue(i);
        }
    }
    return {};
}

std::string XmlReader::GetAttribute(int index) const {
    const auto& attributes = CurrentAttributes();
    if (index < 0 || static_cast<std::size_t>(index) >= attributes.size()) {
        return {};
    }
    return CurrentAttributeValue(static_cast<std::size_t>(index));
}

std::string XmlReader::GetAttribute(const std::string& localName, const std::string& namespaceUri) const {
    const auto& attributes = currentAttributes_;
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        const std::string_view attributeName(attributes[i].first);
        if (SplitQualifiedNameView(attributeName).second != localName) {
            continue;
        }
        if (CurrentAttributeNamespaceUri(i) == namespaceUri) {
            return CurrentAttributeValue(i);
        }
    }
    return {};
}

bool XmlReader::MoveToAttribute(const std::string& name) {
    const auto& attributes = CurrentAttributes();
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        const std::string_view attributeName(attributes[i].first);
        if (attributeName == name || SplitQualifiedNameView(attributeName).second == name) {
            attributeIndex_ = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

bool XmlReader::MoveToAttribute(int index) {
    const auto& attributes = CurrentAttributes();
    if (index < 0 || static_cast<std::size_t>(index) >= attributes.size()) {
        return false;
    }
    attributeIndex_ = index;
    return true;
}

bool XmlReader::MoveToAttribute(const std::string& localName, const std::string& namespaceUri) {
    for (std::size_t i = 0; i < currentAttributes_.size(); ++i) {
        const std::string_view attributeName(currentAttributes_[i].first);
        if (SplitQualifiedNameView(attributeName).second != localName) {
            continue;
        }
        if (CurrentAttributeNamespaceUri(i) == namespaceUri) {
            attributeIndex_ = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

bool XmlReader::MoveToFirstAttribute() {
    if (NodeType() != XmlNodeType::Element || CurrentAttributes().empty()) {
        return false;
    }

    attributeIndex_ = 0;
    return true;
}

bool XmlReader::MoveToNextAttribute() {
    if (attributeIndex_ < 0) {
        return false;
    }
    if (static_cast<std::size_t>(attributeIndex_ + 1) >= CurrentAttributes().size()) {
        return false;
    }

    ++attributeIndex_;
    return true;
}

bool XmlReader::MoveToElement() {
    const bool moved = attributeIndex_ >= 0;
    attributeIndex_ = -1;
    return moved;
}

std::string XmlReader::LookupNamespace(const std::string& prefix) const {
    return LookupNamespaceUri(prefix);
}

std::string XmlReader::ReadInnerXml() const {
    if (attributeIndex_ >= 0) {
        return Value();
    }
    if (NodeType() != XmlNodeType::Element) {
        return {};
    }
    if (!currentIsEmptyElement_ && currentInnerXml_.empty() && currentElementStart_ != std::string::npos) {
        const auto captured = CaptureElementXml(currentElementStart_, currentContentStart_);
        currentInnerXml_ = captured.first;
        currentOuterXml_ = captured.second;
    }
    return currentInnerXml_;
}

std::string XmlReader::ReadOuterXml() const {
    if (attributeIndex_ >= 0) {
        return Name() + "=\"" + EscapeAttribute(Value(), XmlWriterSettings{}) + "\"";
    }
    if (!started_ || eof_) {
        return {};
    }
    if (currentNodeType_ == XmlNodeType::EndElement || currentNodeType_ == XmlNodeType::EndEntity) {
        return {};
    }
    if (currentOuterXml_.empty()) {
        if (currentNodeType_ == XmlNodeType::Element && currentElementStart_ != std::string::npos) {
            if (currentIsEmptyElement_) {
                currentOuterXml_ = SourceSubstr(currentElementStart_, currentContentStart_ - currentElementStart_);
            } else {
                const auto captured = CaptureElementXml(currentElementStart_, currentContentStart_);
                currentInnerXml_ = captured.first;
                currentOuterXml_ = captured.second;
            }
        } else if (currentNodeStart_ != std::string::npos && currentNodeEnd_ != std::string::npos) {
            currentOuterXml_ = SourceSubstr(currentNodeStart_, currentNodeEnd_ - currentNodeStart_);
        }
    }
    return currentOuterXml_;
}

std::string XmlReader::ReadContentAsString() {
    std::string result;
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::Text || nt == XmlNodeType::CDATA || nt == XmlNodeType::Whitespace ||
            nt == XmlNodeType::SignificantWhitespace) {
            AppendCurrentValueTo(result);
            Read();
        } else {
            break;
        }
    }
    return result;
}

int XmlReader::ReadContentAsInt() {
    return XmlConvert::ToInt32(ReadContentAsString());
}

long long XmlReader::ReadContentAsLong() {
    return XmlConvert::ToInt64(ReadContentAsString());
}

double XmlReader::ReadContentAsDouble() {
    return XmlConvert::ToDouble(ReadContentAsString());
}

bool XmlReader::ReadContentAsBoolean() {
    return XmlConvert::ToBoolean(ReadContentAsString());
}

std::string XmlReader::ReadString() {
    // Legacy API: accumulate Text/CDATA content until a non-text node
    if (NodeType() == XmlNodeType::Element) {
        if (!Read()) return {};
    }
    std::string result;
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::Text || nt == XmlNodeType::CDATA ||
            nt == XmlNodeType::Whitespace || nt == XmlNodeType::SignificantWhitespace) {
            AppendCurrentValueTo(result);
            Read();
        } else {
            break;
        }
    }
    return result;
}

int XmlReader::ReadBase64(std::vector<unsigned char>& buffer) {
    const std::size_t originalSize = buffer.size();
    unsigned int accumulator = 0;
    int bits = 0;

    while (true) {
        const auto nodeType = NodeType();
        if (nodeType == XmlNodeType::Text || nodeType == XmlNodeType::CDATA
            || nodeType == XmlNodeType::Whitespace || nodeType == XmlNodeType::SignificantWhitespace) {
            DecodeAndAppendCurrentBase64(buffer, accumulator, bits);
            Read();
        } else {
            break;
        }
    }

    return static_cast<int>(buffer.size() - originalSize);
}

XmlNodeType XmlReader::MoveToContent() {
    MoveToElement();
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::Element || nt == XmlNodeType::Text || nt == XmlNodeType::CDATA ||
            nt == XmlNodeType::EntityReference || nt == XmlNodeType::EndElement || nt == XmlNodeType::EndEntity) {
            return nt;
        }
        if (!Read()) {
            return NodeType();
        }
    }
}

bool XmlReader::IsStartElement() {
    return MoveToContent() == XmlNodeType::Element;
}

bool XmlReader::IsStartElement(const std::string& name) {
    return MoveToContent() == XmlNodeType::Element && Name() == name;
}

void XmlReader::ReadStartElement() {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadStartElement called when the reader is not positioned on an element");
    }
    Read();
}

void XmlReader::ReadStartElement(const std::string& name) {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadStartElement called when the reader is not positioned on an element");
    }
    if (Name() != name) {
        throw XmlException("Element '" + name + "' was not found. Current element is '" + Name() + "'");
    }
    Read();
}

void XmlReader::ReadEndElement() {
    if (MoveToContent() != XmlNodeType::EndElement) {
        throw XmlException("ReadEndElement called when the reader is not positioned on an end element");
    }
    Read();
}

std::string XmlReader::ReadElementContentAsString() {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadElementContentAsString called when the reader is not positioned on an element");
    }
    if (IsEmptyElement()) {
        Read();
        return {};
    }

    std::string result;
    std::size_t closeStart = std::string::npos;
    std::size_t closeEnd = std::string::npos;
    if (TryReadSimpleElementContentAsString(result, closeStart, closeEnd)) {
        const auto [lineNumber, linePosition] = ComputeLineColumn(closeStart);
        position_ = closeStart;
        lineNumber_ = lineNumber;
        linePosition_ = linePosition;
        ParseEndElement();
        Read();
        return result;
    }

    Read();  // move past start element
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::EndElement) {
            Read();  // consume end element
            return result;
        }
        if (nt == XmlNodeType::Text || nt == XmlNodeType::CDATA || nt == XmlNodeType::Whitespace ||
            nt == XmlNodeType::SignificantWhitespace) {
            AppendCurrentValueTo(result);
            Read();
        } else if (nt == XmlNodeType::Comment || nt == XmlNodeType::ProcessingInstruction) {
            Read();
        } else {
            throw XmlException("ReadElementContentAsString encountered unexpected node type");
        }
    }
}

std::string XmlReader::ReadElementString() {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadElementString called when the reader is not positioned on an element");
    }
    if (IsEmptyElement()) {
        Read();
        return {};
    }

    std::string result;
    std::size_t closeStart = std::string::npos;
    std::size_t closeEnd = std::string::npos;
    if (TryReadSimpleElementContentAsString(result, closeStart, closeEnd)) {
        const auto [lineNumber, linePosition] = ComputeLineColumn(closeStart);
        position_ = closeStart;
        lineNumber_ = lineNumber;
        linePosition_ = linePosition;
        ParseEndElement();
        Read();
        return result;
    }

    Read();
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::EndElement) {
            Read();
            return result;
        }
        if (nt == XmlNodeType::Text || nt == XmlNodeType::CDATA || nt == XmlNodeType::Whitespace ||
            nt == XmlNodeType::SignificantWhitespace) {
            AppendCurrentValueTo(result);
            Read();
        } else if (nt == XmlNodeType::Comment || nt == XmlNodeType::ProcessingInstruction) {
            Read();
        } else {
            throw XmlException("ReadElementString does not support mixed content elements");
        }
    }
}

std::string XmlReader::ReadElementString(const std::string& name) {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadElementString called when the reader is not positioned on an element");
    }
    if (Name() != name) {
        throw XmlException("Element '" + name + "' was not found. Current element is '" + Name() + "'");
    }
    return ReadElementString();
}

void XmlReader::Skip() {
    MoveToElement();
    if (NodeType() == XmlNodeType::Element && !IsEmptyElement()) {
        int depth = Depth();
        while (Read()) {
            if (NodeType() == XmlNodeType::EndElement && Depth() == depth) {
                Read();
                return;
            }
        }
    } else {
        Read();
    }
}

bool XmlReader::ReadToFollowing(const std::string& name) {
    while (Read()) {
        if (NodeType() == XmlNodeType::Element && Name() == name) {
            return true;
        }
    }
    return false;
}

bool XmlReader::ReadToDescendant(const std::string& name) {
    if (NodeType() != XmlNodeType::Element || IsEmptyElement()) {
        return false;
    }
    int startDepth = Depth();
    while (Read()) {
        if (NodeType() == XmlNodeType::Element && Name() == name) {
            return true;
        }
        if (NodeType() == XmlNodeType::EndElement && Depth() == startDepth) {
            return false;
        }
    }
    return false;
}

bool XmlReader::ReadToNextSibling(const std::string& name) {
    int targetDepth = Depth();
    // Skip current node
    if (NodeType() == XmlNodeType::Element && !IsEmptyElement()) {
        Skip();
    } else {
        Read();
    }
    while (!IsEOF()) {
        if (NodeType() == XmlNodeType::Element && Depth() == targetDepth && Name() == name) {
            return true;
        }
        if (NodeType() == XmlNodeType::EndElement && Depth() < targetDepth) {
            return false;
        }
        if (NodeType() == XmlNodeType::Element && !IsEmptyElement()) {
            Skip();
        } else {
            Read();
        }
    }
    return false;
}

XmlReader XmlReader::ReadSubtree() {
    if (!started_ || eof_ || NodeType() != XmlNodeType::Element) {
        return XmlReader::Create(std::string{}, XmlReaderSettings{});
    }

    XmlReaderSettings subtreeSettings = settings_;
    subtreeSettings.Conformance = ConformanceLevel::Fragment;
    if (inputSource_ == nullptr || currentElementStart_ == std::string::npos) {
        return XmlReader::Create(ReadOuterXml(), subtreeSettings);
    }

    std::size_t subtreeLength = 0;
    std::size_t closeStart = std::string::npos;
    if (currentIsEmptyElement_) {
        subtreeLength = currentContentStart_ - currentElementStart_;
    } else {
        const auto bounds = EnsureCurrentElementXmlBounds();
        closeStart = bounds.first;
        const auto closeEnd = bounds.second;
        subtreeLength = closeEnd - currentElementStart_;
    }

    static constexpr std::size_t InMemorySubtreeMaterializeThreshold = 20 * 1024 * 1024;
    const bool streamBackedSource = dynamic_cast<const StreamXmlReaderInputSource*>(inputSource_.get()) != nullptr
        || dynamic_cast<const SubrangeXmlReaderInputSource*>(inputSource_.get()) != nullptr;
    if (streamBackedSource && subtreeLength <= InMemorySubtreeMaterializeThreshold) {
        return XmlReader::Create(SourceSubstr(currentElementStart_, subtreeLength), subtreeSettings);
    }

    inputSource_->EnableReplay();

    XmlReader reader(subtreeSettings);
    reader.inputSource_ = std::make_shared<SubrangeXmlReaderInputSource>(inputSource_, currentElementStart_, subtreeLength);
    reader.InitializeInputState();
    return reader;
}

XmlNodeReader::XmlNodeReader(const XmlNode& node, const XmlReaderSettings& settings)
    : settings_(settings) {
    BuildEvents(node, 0, false);
}

bool XmlNodeReader::Read() {
    attributeIndex_ = -1;
    started_ = true;

    while (currentIndex_ < events_.size()) {
        const auto& event = events_[currentIndex_++];
        if (settings_.IgnoreComments && event.nodeType == XmlNodeType::Comment) {
            continue;
        }
        if (settings_.IgnoreProcessingInstructions && event.nodeType == XmlNodeType::ProcessingInstruction) {
            continue;
        }
        if (settings_.IgnoreWhitespace
            && (event.nodeType == XmlNodeType::Whitespace || event.nodeType == XmlNodeType::SignificantWhitespace)) {
            continue;
        }

        eof_ = false;
        return true;
    }

    eof_ = true;
    return false;
}

bool XmlNodeReader::IsEOF() const noexcept {
    return eof_;
}

ReadState XmlNodeReader::GetReadState() const noexcept {
    if (eof_) return ReadState::EndOfFile;
    if (!started_) return ReadState::Initial;
    return ReadState::Interactive;
}

const XmlNameTable& XmlNodeReader::NameTable() const noexcept {
    return nameTable_;
}

XmlNodeType XmlNodeReader::NodeType() const {
    if (attributeIndex_ >= 0) {
        return XmlNodeType::Attribute;
    }
    const auto* event = CurrentEvent();
    return event == nullptr ? XmlNodeType::None : event->nodeType;
}

const std::string& XmlNodeReader::Name() const {
    if (attributeIndex_ >= 0) {
        return CurrentAttributes()[static_cast<std::size_t>(attributeIndex_)].first;
    }
    const auto* event = CurrentEvent();
    return event == nullptr ? EmptyString() : event->name;
}

std::string XmlNodeReader::LocalName() const {
    return std::string{SplitQualifiedNameView(Name()).second};
}

std::string XmlNodeReader::Prefix() const {
    return std::string{SplitQualifiedNameView(Name()).first};
}

const std::string& XmlNodeReader::NamespaceURI() const {
    const auto* event = CurrentEvent();
    if (attributeIndex_ >= 0) {
        return event == nullptr ? EmptyString() : event->attributeNamespaceUris[static_cast<std::size_t>(attributeIndex_)];
    }
    return event == nullptr ? EmptyString() : event->namespaceUri;
}

const std::string& XmlNodeReader::Value() const {
    if (attributeIndex_ >= 0) {
        return CurrentAttributes()[static_cast<std::size_t>(attributeIndex_)].second;
    }
    const auto* event = CurrentEvent();
    return event == nullptr ? EmptyString() : event->value;
}

int XmlNodeReader::Depth() const noexcept {
    const auto* event = CurrentEvent();
    if (event == nullptr) {
        return 0;
    }
    return event->depth + (attributeIndex_ >= 0 ? 1 : 0);
}

bool XmlNodeReader::IsEmptyElement() const noexcept {
    const auto* event = CurrentEvent();
    return attributeIndex_ < 0 && event != nullptr && event->nodeType == XmlNodeType::Element && event->isEmptyElement;
}

bool XmlNodeReader::HasValue() const noexcept {
    switch (NodeType()) {
    case XmlNodeType::Attribute:
    case XmlNodeType::Text:
    case XmlNodeType::CDATA:
    case XmlNodeType::EntityReference:
    case XmlNodeType::ProcessingInstruction:
    case XmlNodeType::Comment:
    case XmlNodeType::Whitespace:
    case XmlNodeType::SignificantWhitespace:
    case XmlNodeType::XmlDeclaration:
        return true;
    default:
        return false;
    }
}

int XmlNodeReader::AttributeCount() const noexcept {
    return static_cast<int>(CurrentAttributes().size());
}

bool XmlNodeReader::HasAttributes() const noexcept {
    return !CurrentAttributes().empty();
}

std::string XmlNodeReader::GetAttribute(const std::string& name) const {
    const auto& attributes = CurrentAttributes();
    const auto found = std::find_if(attributes.begin(), attributes.end(), [&name](const auto& attribute) {
        const std::string_view attributeName(attribute.first);
        return attributeName == name || SplitQualifiedNameView(attributeName).second == name;
    });
    return found == attributes.end() ? std::string{} : found->second;
}

std::string XmlNodeReader::GetAttribute(int index) const {
    const auto& attributes = CurrentAttributes();
    if (index < 0 || static_cast<std::size_t>(index) >= attributes.size()) {
        return {};
    }
    return attributes[static_cast<std::size_t>(index)].second;
}

std::string XmlNodeReader::GetAttribute(const std::string& localName, const std::string& namespaceUri) const {
    const auto* event = CurrentEvent();
    if (event == nullptr) return {};
    for (std::size_t i = 0; i < event->attributes.size(); ++i) {
        const auto local = SplitQualifiedNameView(event->attributes[i].first).second;
        const auto ns = i < event->attributeNamespaceUris.size() ? event->attributeNamespaceUris[i] : std::string{};
        if (local == localName && ns == namespaceUri) {
            return event->attributes[i].second;
        }
    }
    return {};
}

bool XmlNodeReader::MoveToAttribute(const std::string& name) {
    const auto& attributes = CurrentAttributes();
    const auto found = std::find_if(attributes.begin(), attributes.end(), [&name](const auto& attribute) {
        const std::string_view attributeName(attribute.first);
        return attributeName == name || SplitQualifiedNameView(attributeName).second == name;
    });
    if (found == attributes.end()) {
        return false;
    }

    attributeIndex_ = static_cast<int>(std::distance(attributes.begin(), found));
    return true;
}

bool XmlNodeReader::MoveToAttribute(int index) {
    const auto& attributes = CurrentAttributes();
    if (index < 0 || static_cast<std::size_t>(index) >= attributes.size()) {
        return false;
    }
    attributeIndex_ = index;
    return true;
}

bool XmlNodeReader::MoveToAttribute(const std::string& localName, const std::string& namespaceUri) {
    const auto* event = CurrentEvent();
    if (event == nullptr) return false;
    for (std::size_t i = 0; i < event->attributes.size(); ++i) {
        const auto local = SplitQualifiedNameView(event->attributes[i].first).second;
        const auto ns = i < event->attributeNamespaceUris.size() ? event->attributeNamespaceUris[i] : std::string{};
        if (local == localName && ns == namespaceUri) {
            attributeIndex_ = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

bool XmlNodeReader::MoveToFirstAttribute() {
    if (NodeType() != XmlNodeType::Element || CurrentAttributes().empty()) {
        return false;
    }

    attributeIndex_ = 0;
    return true;
}

bool XmlNodeReader::MoveToNextAttribute() {
    if (attributeIndex_ < 0) {
        return false;
    }
    if (static_cast<std::size_t>(attributeIndex_ + 1) >= CurrentAttributes().size()) {
        return false;
    }

    ++attributeIndex_;
    return true;
}

bool XmlNodeReader::MoveToElement() {
    const bool moved = attributeIndex_ >= 0;
    attributeIndex_ = -1;
    return moved;
}

std::string XmlNodeReader::LookupNamespace(const std::string& prefix) const {
    // XmlNodeReader resolves namespaces from the current event's attribute namespace URIs
    const auto* event = CurrentEvent();
    if (event == nullptr) return {};
    for (std::size_t i = 0; i < event->attributes.size(); ++i) {
        const auto& [name, value] = event->attributes[i];
        if (name == "xmlns" && prefix.empty()) return value;
        if (name.rfind("xmlns:", 0) == 0 && name.substr(6) == prefix) return value;
    }
    if (prefix == "xml") return "http://www.w3.org/XML/1998/namespace";
    if (prefix == "xmlns") return "http://www.w3.org/2000/xmlns/";
    return {};
}

std::string XmlNodeReader::ReadInnerXml() const {
    if (attributeIndex_ >= 0) {
        return Value();
    }
    const auto* event = CurrentEvent();
    if (event != nullptr && event->nodeType == XmlNodeType::Attribute) {
        return event->value;
    }
    if (event == nullptr || event->nodeType != XmlNodeType::Element) {
        return {};
    }
    return event->innerXml;
}

std::string XmlNodeReader::ReadOuterXml() const {
    if (attributeIndex_ >= 0) {
        return Name() + "=\"" + EscapeAttribute(Value(), XmlWriterSettings{}) + "\"";
    }
    const auto* event = CurrentEvent();
    if (event == nullptr || event->nodeType == XmlNodeType::EndElement || event->nodeType == XmlNodeType::EndEntity) {
        return {};
    }
    return event->outerXml;
}

std::string XmlNodeReader::ReadContentAsString() {
    std::string result;
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::Text || nt == XmlNodeType::CDATA || nt == XmlNodeType::Whitespace ||
            nt == XmlNodeType::SignificantWhitespace) {
            result += Value();
            Read();
        } else {
            break;
        }
    }
    return result;
}

int XmlNodeReader::ReadContentAsInt() {
    return XmlConvert::ToInt32(ReadContentAsString());
}

long long XmlNodeReader::ReadContentAsLong() {
    return XmlConvert::ToInt64(ReadContentAsString());
}

double XmlNodeReader::ReadContentAsDouble() {
    return XmlConvert::ToDouble(ReadContentAsString());
}

bool XmlNodeReader::ReadContentAsBoolean() {
    return XmlConvert::ToBoolean(ReadContentAsString());
}

std::string XmlNodeReader::ReadString() {
    if (NodeType() == XmlNodeType::Element) {
        if (!Read()) return {};
    }
    std::string result;
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::Text || nt == XmlNodeType::CDATA ||
            nt == XmlNodeType::Whitespace || nt == XmlNodeType::SignificantWhitespace) {
            result += Value();
            Read();
        } else {
            break;
        }
    }
    return result;
}

int XmlNodeReader::ReadBase64(std::vector<unsigned char>& buffer) {
    const std::size_t originalSize = buffer.size();
    unsigned int accumulator = 0;
    int bits = 0;

    while (true) {
        const auto nodeType = NodeType();
        if (nodeType == XmlNodeType::Text || nodeType == XmlNodeType::CDATA
            || nodeType == XmlNodeType::Whitespace || nodeType == XmlNodeType::SignificantWhitespace) {
            AppendDecodedBase64Chunk(Value(), buffer, accumulator, bits);
            Read();
        } else {
            break;
        }
    }

    return static_cast<int>(buffer.size() - originalSize);
}

XmlNodeType XmlNodeReader::MoveToContent() {
    MoveToElement();
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::Element || nt == XmlNodeType::Text || nt == XmlNodeType::CDATA ||
            nt == XmlNodeType::EntityReference || nt == XmlNodeType::EndElement || nt == XmlNodeType::EndEntity) {
            return nt;
        }
        if (!Read()) {
            return NodeType();
        }
    }
}

bool XmlNodeReader::IsStartElement() {
    return MoveToContent() == XmlNodeType::Element;
}

bool XmlNodeReader::IsStartElement(const std::string& name) {
    return MoveToContent() == XmlNodeType::Element && Name() == name;
}

void XmlNodeReader::ReadStartElement() {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadStartElement called when the reader is not positioned on an element");
    }
    Read();
}

void XmlNodeReader::ReadStartElement(const std::string& name) {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadStartElement called when the reader is not positioned on an element");
    }
    if (Name() != name) {
        throw XmlException("Element '" + name + "' was not found. Current element is '" + Name() + "'");
    }
    Read();
}

void XmlNodeReader::ReadEndElement() {
    if (MoveToContent() != XmlNodeType::EndElement) {
        throw XmlException("ReadEndElement called when the reader is not positioned on an end element");
    }
    Read();
}

std::string XmlNodeReader::ReadElementContentAsString() {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadElementContentAsString called when the reader is not positioned on an element");
    }
    if (IsEmptyElement()) {
        Read();
        return {};
    }
    Read();
    std::string result;
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::EndElement) {
            Read();
            return result;
        }
        if (nt == XmlNodeType::Text || nt == XmlNodeType::CDATA || nt == XmlNodeType::Whitespace ||
            nt == XmlNodeType::SignificantWhitespace) {
            result += Value();
            Read();
        } else if (nt == XmlNodeType::Comment || nt == XmlNodeType::ProcessingInstruction) {
            Read();
        } else {
            throw XmlException("ReadElementContentAsString encountered unexpected node type");
        }
    }
}

std::string XmlNodeReader::ReadElementString() {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadElementString called when the reader is not positioned on an element");
    }
    if (IsEmptyElement()) {
        Read();
        return {};
    }
    Read();
    std::string result;
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::EndElement) {
            Read();
            return result;
        }
        if (nt == XmlNodeType::Text || nt == XmlNodeType::CDATA || nt == XmlNodeType::Whitespace ||
            nt == XmlNodeType::SignificantWhitespace) {
            result += Value();
            Read();
        } else if (nt == XmlNodeType::Comment || nt == XmlNodeType::ProcessingInstruction) {
            Read();
        } else {
            throw XmlException("ReadElementString does not support mixed content elements");
        }
    }
}

std::string XmlNodeReader::ReadElementString(const std::string& name) {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadElementString called when the reader is not positioned on an element");
    }
    if (Name() != name) {
        throw XmlException("Element '" + name + "' was not found. Current element is '" + Name() + "'");
    }
    return ReadElementString();
}

void XmlNodeReader::Skip() {
    MoveToElement();
    if (NodeType() == XmlNodeType::Element && !IsEmptyElement()) {
        int depth = Depth();
        while (Read()) {
            if (NodeType() == XmlNodeType::EndElement && Depth() == depth) {
                Read();
                return;
            }
        }
    } else {
        Read();
    }
}

bool XmlNodeReader::ReadToFollowing(const std::string& name) {
    while (Read()) {
        if (NodeType() == XmlNodeType::Element && Name() == name) {
            return true;
        }
    }
    return false;
}

bool XmlNodeReader::ReadToDescendant(const std::string& name) {
    if (NodeType() != XmlNodeType::Element || IsEmptyElement()) {
        return false;
    }
    int startDepth = Depth();
    while (Read()) {
        if (NodeType() == XmlNodeType::Element && Name() == name) {
            return true;
        }
        if (NodeType() == XmlNodeType::EndElement && Depth() == startDepth) {
            return false;
        }
    }
    return false;
}

bool XmlNodeReader::ReadToNextSibling(const std::string& name) {
    int targetDepth = Depth();
    if (NodeType() == XmlNodeType::Element && !IsEmptyElement()) {
        Skip();
    } else {
        Read();
    }
    while (!IsEOF()) {
        if (NodeType() == XmlNodeType::Element && Depth() == targetDepth && Name() == name) {
            return true;
        }
        if (NodeType() == XmlNodeType::EndElement && Depth() < targetDepth) {
            return false;
        }
        if (NodeType() == XmlNodeType::Element && !IsEmptyElement()) {
            Skip();
        } else {
            Read();
        }
    }
    return false;
}

void XmlNodeReader::BuildEvents(const XmlNode& node, int depth, bool preserveSpace) {
    switch (node.NodeType()) {
    case XmlNodeType::Document:
    case XmlNodeType::DocumentFragment:
        for (const auto& child : node.ChildNodes()) {
            if (child != nullptr) {
                BuildEvents(*child, depth, preserveSpace);
            }
        }
        return;
    case XmlNodeType::Element: {
        std::vector<std::pair<std::string, std::string>> attributes;
        std::vector<std::string> attributeNamespaceUris;
        const auto& element = static_cast<const XmlElement&>(node);
        bool childPreserveSpace = preserveSpace;
        attributes.reserve(element.Attributes().size());
        attributeNamespaceUris.reserve(element.Attributes().size());
        for (const auto& attribute : element.Attributes()) {
            attributes.emplace_back(attribute->Name(), attribute->Value());
            attributeNamespaceUris.push_back(attribute->NamespaceURI());
            if (attribute->Name() == "xml:space") {
                if (IsXmlSpacePreserve(attribute->Value())) {
                    childPreserveSpace = true;
                } else if (IsXmlSpaceDefault(attribute->Value())) {
                    childPreserveSpace = false;
                }
            }
        }

        const bool isEmptyElement = !element.HasChildNodes() && !element.WritesFullEndElement();
        AppendEvent(
            XmlNodeType::Element,
            element.Name(),
            element.NamespaceURI(),
            {},
            depth,
            isEmptyElement,
            element.InnerXml(),
            element.OuterXml(),
            std::move(attributes),
            std::move(attributeNamespaceUris));

        for (const auto& child : element.ChildNodes()) {
            if (child != nullptr) {
                BuildEvents(*child, depth + 1, childPreserveSpace);
            }
        }

        if (!isEmptyElement) {
            AppendEvent(XmlNodeType::EndElement, element.Name(), element.NamespaceURI(), {}, depth, false, {}, "</" + element.Name() + ">" );
        }
        return;
    }
    case XmlNodeType::EntityReference:
        AppendEvent(
            XmlNodeType::EntityReference,
            node.Name(),
            node.NamespaceURI(),
            node.Value(),
            depth,
            false,
            {},
            node.OuterXml());
        if (!node.Value().empty()) {
            const XmlNodeType textNodeType = IsWhitespaceOnly(node.Value())
                ? (preserveSpace ? XmlNodeType::SignificantWhitespace : XmlNodeType::Whitespace)
                : XmlNodeType::Text;
            AppendEvent(textNodeType, {}, {}, node.Value(), depth + 1, false, {}, node.Value());
        }
        AppendEvent(XmlNodeType::EndEntity, node.Name(), node.NamespaceURI(), {}, depth, false, {}, node.OuterXml());
        return;
    default:
        const bool anonymousReaderNode = node.NodeType() == XmlNodeType::Text
            || node.NodeType() == XmlNodeType::CDATA
            || node.NodeType() == XmlNodeType::Comment
            || node.NodeType() == XmlNodeType::Whitespace
            || node.NodeType() == XmlNodeType::SignificantWhitespace;
        AppendEvent(
            node.NodeType(),
            anonymousReaderNode ? std::string{} : node.Name(),
            node.NamespaceURI(),
            node.Value(),
            depth,
            false,
            node.NodeType() == XmlNodeType::Element ? node.InnerXml() : std::string{},
            node.OuterXml());
        return;
    }
}

void XmlNodeReader::AppendEvent(
    XmlNodeType nodeType,
    std::string name,
    std::string namespaceUri,
    std::string value,
    int depth,
    bool isEmptyElement,
    std::string innerXml,
    std::string outerXml,
    std::vector<std::pair<std::string, std::string>> attributes,
    std::vector<std::string> attributeNamespaceUris) {
    if (!name.empty()) {
        nameTable_.Add(name);
    }
    if (!namespaceUri.empty()) {
        nameTable_.Add(namespaceUri);
    }
    for (const auto& attribute : attributes) {
        if (!attribute.first.empty()) {
            nameTable_.Add(attribute.first);
        }
    }
    for (const auto& attributeNamespaceUri : attributeNamespaceUris) {
        if (!attributeNamespaceUri.empty()) {
            nameTable_.Add(attributeNamespaceUri);
        }
    }
    events_.push_back(NodeEvent{
        nodeType,
        std::move(name),
        std::move(namespaceUri),
        std::move(value),
        depth,
        isEmptyElement,
        std::move(innerXml),
        std::move(outerXml),
        std::move(attributes),
        std::move(attributeNamespaceUris)});
}

const XmlNodeReader::NodeEvent* XmlNodeReader::CurrentEvent() const noexcept {
    if (!started_ || currentIndex_ == 0 || currentIndex_ > events_.size()) {
        return nullptr;
    }
    return &events_[currentIndex_ - 1];
}

const std::vector<std::pair<std::string, std::string>>& XmlNodeReader::CurrentAttributes() const noexcept {
    const auto* event = CurrentEvent();
    static const std::vector<std::pair<std::string, std::string>> empty;
    return event == nullptr ? empty : event->attributes;
}

void XmlDocument::ValidateReaderAgainstSchemas(XmlReader& reader, const XmlSchemaSet& schemas) const {
    static const std::string kSchemaInstanceNamespace = "http://www.w3.org/2001/XMLSchema-instance";

    const auto readerCanEvaluateIdentityConstraint = [](const XmlSchemaSet::ElementRule::IdentityConstraint& constraint) {
        if (!constraint.compiledSelectorPath.has_value()) {
            return false;
        }
        if (constraint.compiledFieldPaths.size() != constraint.fieldXPaths.size()) {
            return false;
        }
        return std::all_of(constraint.compiledFieldPaths.begin(), constraint.compiledFieldPaths.end(), [](const auto& fieldPath) {
            return fieldPath.has_value();
        });
    };

    const auto wildcardNamespaceTokenMatchesForDomFallback = [](const std::string& token,
                                                                const std::string& namespaceUri,
                                                                const std::string& rootNamespaceUri) {
        if (token.empty() || token == "##any") {
            return true;
        }
        if (token == "##other") {
            return namespaceUri != rootNamespaceUri;
        }
        if (token == "##targetNamespace") {
            return namespaceUri == rootNamespaceUri;
        }
        if (token == "##local") {
            return namespaceUri.empty();
        }
        return namespaceUri == token;
    };

    const auto matchesWildcardNamespaceForDomFallback = [&](const std::string& namespaceConstraint,
                                                            const std::string& namespaceUri,
                                                            const std::string& rootNamespaceUri) {
        if (namespaceConstraint.empty() || namespaceConstraint == "##any") {
            return true;
        }

        std::istringstream tokens(namespaceConstraint);
        std::string token;
        while (tokens >> token) {
            if (wildcardNamespaceTokenMatchesForDomFallback(token, namespaceUri, rootNamespaceUri)) {
                return true;
            }
        }
        return false;
    };

    std::string rootRuleNamespaceUri;

    struct RuntimeDerivedComplexTypeCacheKey {
        const XmlSchemaSet::ComplexTypeRule* declaredType = nullptr;
        bool declarationBlockRestriction = false;
        bool declarationBlockExtension = false;

        bool operator==(const RuntimeDerivedComplexTypeCacheKey& other) const noexcept {
            return declaredType == other.declaredType
                && declarationBlockRestriction == other.declarationBlockRestriction
                && declarationBlockExtension == other.declarationBlockExtension;
        }
    };

    struct RuntimeDerivedComplexTypeCacheKeyHasher {
        std::size_t operator()(const RuntimeDerivedComplexTypeCacheKey& key) const noexcept {
            std::size_t seed = std::hash<const void*>{}(key.declaredType);
            seed ^= std::hash<bool>{}(key.declarationBlockRestriction) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= std::hash<bool>{}(key.declarationBlockExtension) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    std::unordered_map<RuntimeDerivedComplexTypeCacheKey,
        std::vector<const XmlSchemaSet::ComplexTypeRule*>,
        RuntimeDerivedComplexTypeCacheKeyHasher> runtimeDerivedComplexTypeCandidatesCache;
    std::unordered_map<std::string, std::vector<const XmlSchemaSet::ComplexTypeRule*>> complexTypesByBaseKey;

    for (const auto& namedComplexType : schemas.complexTypes_) {
        if (!namedComplexType.rule.derivationBaseName.empty()) {
            const std::string baseKey = namedComplexType.rule.derivationBaseNamespaceUri + "\n" + namedComplexType.rule.derivationBaseName;
            complexTypesByBaseKey[baseKey].push_back(std::addressof(namedComplexType.rule));
        }
    }

    const auto runtimeComplexTypeCanSubstituteViaXsiType = [&](const XmlSchemaSet::ComplexTypeRule& derivedType,
                                                               const XmlSchemaSet::ComplexTypeRule& baseType,
                                                               bool declarationBlockRestriction,
                                                               bool declarationBlockExtension) {
        if (baseType.namedTypeName.empty() || derivedType.namedTypeName.empty()) {
            return false;
        }
        if (derivedType.namedTypeName == baseType.namedTypeName
            && derivedType.namedTypeNamespaceUri == baseType.namedTypeNamespaceUri) {
            return false;
        }

        bool usesRestriction = false;
        bool usesExtension = false;
        const XmlSchemaSet::ComplexTypeRule* current = &derivedType;
        std::unordered_set<std::string> visited;
        while (true) {
            if (current->derivationMethod == XmlSchemaSet::ComplexTypeRule::DerivationMethod::Restriction) {
                usesRestriction = true;
            } else if (current->derivationMethod == XmlSchemaSet::ComplexTypeRule::DerivationMethod::Extension) {
                usesExtension = true;
            }

            if (current->derivationBaseName == baseType.namedTypeName
                && current->derivationBaseNamespaceUri == baseType.namedTypeNamespaceUri) {
                const bool blockedByBaseType = (usesRestriction && baseType.blockRestriction)
                    || (usesExtension && baseType.blockExtension);
                return !((usesRestriction && declarationBlockRestriction)
                    || (usesExtension && declarationBlockExtension)
                    || blockedByBaseType);
            }
            if (current->derivationBaseName.empty()) {
                return false;
            }

            const std::string currentKey = current->derivationBaseNamespaceUri + "\n" + current->derivationBaseName;
            if (!visited.insert(currentKey).second) {
                return false;
            }

            current = schemas.FindComplexTypeRule(current->derivationBaseName, current->derivationBaseNamespaceUri);
            if (current == nullptr) {
                return false;
            }
        }
    };

    std::function<bool(const XmlSchemaSet::ElementRule&, std::unordered_set<const XmlSchemaSet::ElementRule*>&, std::unordered_set<const XmlSchemaSet::ComplexTypeRule*>&)> elementRuleRequiresDom;
    std::function<bool(const XmlSchemaSet::ComplexTypeRule&, std::unordered_set<const XmlSchemaSet::ElementRule*>&, std::unordered_set<const XmlSchemaSet::ComplexTypeRule*>&)> complexTypeRequiresDom;
    std::function<bool(const XmlSchemaSet::Particle&, std::unordered_set<const XmlSchemaSet::ElementRule*>&, std::unordered_set<const XmlSchemaSet::ComplexTypeRule*>&)> particleRequiresDom;
    std::function<bool(const XmlSchemaSet::ComplexTypeRule&, bool, bool, std::unordered_set<const XmlSchemaSet::ElementRule*>&, std::unordered_set<const XmlSchemaSet::ComplexTypeRule*>&)> runtimeDerivedComplexTypesRequireDom;
    std::function<const std::vector<const XmlSchemaSet::ComplexTypeRule*>&(const XmlSchemaSet::ComplexTypeRule&, bool, bool)> getRuntimeDerivedComplexTypeCandidates;

    getRuntimeDerivedComplexTypeCandidates = [&](const XmlSchemaSet::ComplexTypeRule& declaredType,
                                                 bool declarationBlockRestriction,
                                                 bool declarationBlockExtension) -> const std::vector<const XmlSchemaSet::ComplexTypeRule*>& {
        static const std::vector<const XmlSchemaSet::ComplexTypeRule*> empty;
        if (declaredType.namedTypeName.empty()) {
            return empty;
        }

        const RuntimeDerivedComplexTypeCacheKey key{
            std::addressof(declaredType),
            declarationBlockRestriction,
            declarationBlockExtension};
        const auto cached = runtimeDerivedComplexTypeCandidatesCache.find(key);
        if (cached != runtimeDerivedComplexTypeCandidatesCache.end()) {
            return cached->second;
        }

        std::vector<const XmlSchemaSet::ComplexTypeRule*> candidates;
        std::vector<const XmlSchemaSet::ComplexTypeRule*> pending;
        std::unordered_set<std::string> visitedDerivedKeys;

        const std::string declaredKey = declaredType.namedTypeNamespaceUri + "\n" + declaredType.namedTypeName;
        if (const auto derived = complexTypesByBaseKey.find(declaredKey); derived != complexTypesByBaseKey.end()) {
            pending = derived->second;
        }

        while (!pending.empty()) {
            const XmlSchemaSet::ComplexTypeRule* candidate = pending.back();
            pending.pop_back();
            if (candidate == nullptr || candidate->namedTypeName.empty()) {
                continue;
            }

            const std::string candidateKey = candidate->namedTypeNamespaceUri + "\n" + candidate->namedTypeName;
            if (!visitedDerivedKeys.insert(candidateKey).second) {
                continue;
            }

            if (runtimeComplexTypeCanSubstituteViaXsiType(
                    *candidate,
                    declaredType,
                    declarationBlockRestriction,
                    declarationBlockExtension)) {
                candidates.push_back(candidate);
            }

            if (const auto derived = complexTypesByBaseKey.find(candidateKey); derived != complexTypesByBaseKey.end()) {
                pending.insert(pending.end(), derived->second.begin(), derived->second.end());
            }
        }

        return runtimeDerivedComplexTypeCandidatesCache.emplace(key, std::move(candidates)).first->second;
    };

    runtimeDerivedComplexTypesRequireDom = [&](const XmlSchemaSet::ComplexTypeRule& declaredType,
                                               bool declarationBlockRestriction,
                                               bool declarationBlockExtension,
                                               std::unordered_set<const XmlSchemaSet::ElementRule*>& visitedElementRules,
                                               std::unordered_set<const XmlSchemaSet::ComplexTypeRule*>& visitedComplexTypes) {
        const auto& candidates = getRuntimeDerivedComplexTypeCandidates(
            declaredType,
            declarationBlockRestriction,
            declarationBlockExtension);

        return std::any_of(candidates.begin(), candidates.end(), [&](const auto* candidate) {
            return candidate != nullptr && complexTypeRequiresDom(*candidate, visitedElementRules, visitedComplexTypes);
        });
    };

    particleRequiresDom = [&](const XmlSchemaSet::Particle& particle,
                              std::unordered_set<const XmlSchemaSet::ElementRule*>& visitedElementRules,
                              std::unordered_set<const XmlSchemaSet::ComplexTypeRule*>& visitedComplexTypes) {
        if (particle.kind == XmlSchemaSet::Particle::Kind::Element) {
            if (particle.elementComplexType != nullptr
                && complexTypeRequiresDom(*particle.elementComplexType, visitedElementRules, visitedComplexTypes)) {
                return true;
            }
            if (particle.elementComplexType != nullptr
                && runtimeDerivedComplexTypesRequireDom(
                    *particle.elementComplexType,
                    particle.elementBlockRestriction,
                    particle.elementBlockExtension,
                    visitedElementRules,
                    visitedComplexTypes)) {
                return true;
            }
            if (const auto* declaredRule = schemas.FindElementRule(particle.name, particle.namespaceUri);
                declaredRule != nullptr && elementRuleRequiresDom(*declaredRule, visitedElementRules, visitedComplexTypes)) {
                return true;
            }
        }

        if (particle.kind == XmlSchemaSet::Particle::Kind::Any && particle.processContents != "skip") {
            for (const auto& candidate : schemas.elements_) {
                if (matchesWildcardNamespaceForDomFallback(particle.namespaceUri, candidate.namespaceUri, rootRuleNamespaceUri)
                    && elementRuleRequiresDom(candidate, visitedElementRules, visitedComplexTypes)) {
                    return true;
                }
            }
        }

        return std::any_of(particle.children.begin(), particle.children.end(), [&](const auto& childParticle) {
            return particleRequiresDom(childParticle, visitedElementRules, visitedComplexTypes);
        });
    };

    complexTypeRequiresDom = [&](const XmlSchemaSet::ComplexTypeRule& complexType,
                                 std::unordered_set<const XmlSchemaSet::ElementRule*>& visitedElementRules,
                                 std::unordered_set<const XmlSchemaSet::ComplexTypeRule*>& visitedComplexTypes) {
        if (!visitedComplexTypes.insert(std::addressof(complexType)).second) {
            return false;
        }

        if (complexType.particle.has_value()
            && particleRequiresDom(*complexType.particle, visitedElementRules, visitedComplexTypes)) {
            return true;
        }
        if (runtimeDerivedComplexTypesRequireDom(
                complexType,
                false,
                false,
                visitedElementRules,
                visitedComplexTypes)) {
            return true;
        }

        for (const auto& childRule : complexType.children) {
            if (childRule.declaredComplexType != nullptr
                && complexTypeRequiresDom(*childRule.declaredComplexType, visitedElementRules, visitedComplexTypes)) {
                return true;
            }
            if (childRule.declaredComplexType != nullptr
                && runtimeDerivedComplexTypesRequireDom(
                    *childRule.declaredComplexType,
                    childRule.blockRestriction,
                    childRule.blockExtension,
                    visitedElementRules,
                    visitedComplexTypes)) {
                return true;
            }
            if (const auto* declaredRule = schemas.FindElementRule(childRule.name, childRule.namespaceUri);
                declaredRule != nullptr && elementRuleRequiresDom(*declaredRule, visitedElementRules, visitedComplexTypes)) {
                return true;
            }
        }

        return false;
    };

    elementRuleRequiresDom = [&](const XmlSchemaSet::ElementRule& element,
                                 std::unordered_set<const XmlSchemaSet::ElementRule*>& visitedElementRules,
                                 std::unordered_set<const XmlSchemaSet::ComplexTypeRule*>& visitedComplexTypes) {
        if (!visitedElementRules.insert(std::addressof(element)).second) {
            return false;
        }

        if (std::any_of(element.identityConstraints.begin(), element.identityConstraints.end(), [&](const auto& constraint) {
                return !readerCanEvaluateIdentityConstraint(constraint);
            })) {
            return true;
        }
        if (element.declaredComplexType.has_value()
            && complexTypeRequiresDom(*element.declaredComplexType, visitedElementRules, visitedComplexTypes)) {
            return true;
        }
        if (element.declaredComplexType.has_value()
            && runtimeDerivedComplexTypesRequireDom(
                *element.declaredComplexType,
                element.blockRestriction,
                element.blockExtension,
                visitedElementRules,
                visitedComplexTypes)) {
            return true;
        }
        if (element.particle.has_value()
            && particleRequiresDom(*element.particle, visitedElementRules, visitedComplexTypes)) {
            return true;
        }

        for (const auto& childRule : element.children) {
            if (childRule.declaredComplexType != nullptr
                && complexTypeRequiresDom(*childRule.declaredComplexType, visitedElementRules, visitedComplexTypes)) {
                return true;
            }
            if (const auto* declaredRule = schemas.FindElementRule(childRule.name, childRule.namespaceUri);
                declaredRule != nullptr && elementRuleRequiresDom(*declaredRule, visitedElementRules, visitedComplexTypes)) {
                return true;
            }
        }

        for (const auto& candidate : schemas.elements_) {
            if (candidate.substitutionGroupHeadName == element.name
                && candidate.substitutionGroupHeadNamespaceUri == element.namespaceUri
                && elementRuleRequiresDom(candidate, visitedElementRules, visitedComplexTypes)) {
                return true;
            }
        }

        return false;
    };

    SchemaIdentityValidationState identityState;

    struct ReaderIdentityConstraintTable {
        const XmlSchemaSet::ElementRule::IdentityConstraint* constraint = nullptr;
        std::unordered_set<std::string> tuples;
    };

    struct ReaderPendingKeyRefValidation {
        const XmlSchemaSet::ElementRule::IdentityConstraint* constraint = nullptr;
        std::vector<std::string> tuples;
    };

    struct ReaderIdentityScope {
        int depth = 0;
        std::shared_ptr<XmlSchemaSet::ElementRule> rule;
        std::shared_ptr<SchemaIdentityNodeSnapshot> snapshot;
        std::vector<ReaderIdentityConstraintTable> tables;
        std::vector<ReaderPendingKeyRefValidation> pendingKeyRefs;
        std::unordered_set<std::string> declaredConstraintKeys;
        std::unordered_map<std::string, std::size_t> tableIndexByConstraintKey;
    };

    const auto evaluateIdentitySnapshotStringValue = [&](const auto& self, const SchemaIdentityNodeSnapshot& snapshot) -> std::string {
        std::string value = snapshot.textValue;
        for (const auto& child : snapshot.children) {
            if (child != nullptr) {
                value += self(self, *child);
            }
        }
        return value;
    };

    struct ReaderIdentitySelection {
        const SchemaIdentityNodeSnapshot* node = nullptr;
        const SchemaObservedAttribute* attribute = nullptr;
    };

    const auto evaluateCompiledIdentityPathOnSnapshot = [](const SchemaIdentityNodeSnapshot& contextSnapshot, const auto& path) {
        const auto matchesCompiledIdentityStep = [&](const auto& step,
                                                     const std::string& localName,
                                                     const std::string& namespaceUri) {
            const bool localNameMatches = step.localName == "*" || localName == step.localName;
            const bool namespaceMatches = step.localName == "*" && step.namespaceUri.empty()
                ? true
                : namespaceUri == step.namespaceUri;
            return localNameMatches && namespaceMatches;
        };

        const auto matchesCompiledIdentityPredicate = [&](const auto& step, const SchemaIdentityNodeSnapshot& snapshot) {
            if (!step.predicateAttributeValue.has_value()) {
                return true;
            }

            const auto attribute = std::find_if(snapshot.attributes.begin(), snapshot.attributes.end(), [&](const auto& candidate) {
                return candidate.localName == step.predicateAttributeLocalName
                    && candidate.namespaceUri == step.predicateAttributeNamespaceUri
                    && candidate.value == *step.predicateAttributeValue;
            });
            return attribute != snapshot.attributes.end();
        };

        const auto appendMatchingDescendants = [&](const auto& self,
                                                   const SchemaIdentityNodeSnapshot& snapshot,
                                                   const auto& step,
                                                   std::vector<ReaderIdentitySelection>& matches) -> void {
            for (const auto& child : snapshot.children) {
                if (child == nullptr) {
                    continue;
                }
                if (matchesCompiledIdentityStep(step, child->localName, child->namespaceUri)
                    && matchesCompiledIdentityPredicate(step, *child)) {
                    matches.push_back(ReaderIdentitySelection{child.get(), nullptr});
                }
                self(self, *child, step, matches);
            }
        };

        std::vector<ReaderIdentitySelection> currentSelections{{std::addressof(contextSnapshot), nullptr}};
        for (const auto& step : path.steps) {
            std::vector<ReaderIdentitySelection> nextSelections;
            for (const auto& selection : currentSelections) {
                if (selection.node == nullptr) {
                    continue;
                }

                if (step.kind == std::decay_t<decltype(step)>::Kind::Self) {
                    nextSelections.push_back(selection);
                    continue;
                }

                if (step.kind == std::decay_t<decltype(step)>::Kind::Element) {
                    for (const auto& child : selection.node->children) {
                        if (child != nullptr
                            && matchesCompiledIdentityStep(step, child->localName, child->namespaceUri)
                            && matchesCompiledIdentityPredicate(step, *child)) {
                            nextSelections.push_back(ReaderIdentitySelection{child.get(), nullptr});
                        }
                    }
                    continue;
                }

                if (step.kind == std::decay_t<decltype(step)>::Kind::DescendantElement) {
                    appendMatchingDescendants(appendMatchingDescendants, *selection.node, step, nextSelections);
                    continue;
                }

                if (step.kind == std::decay_t<decltype(step)>::Kind::Attribute) {
                    for (const auto& attribute : selection.node->attributes) {
                        if (matchesCompiledIdentityStep(step, attribute.localName, attribute.namespaceUri)) {
                            nextSelections.push_back(ReaderIdentitySelection{nullptr, std::addressof(attribute)});
                        }
                    }
                }
            }
            currentSelections = std::move(nextSelections);
            if (currentSelections.empty()) {
                break;
            }
        }

        return currentSelections;
    };

    const auto evaluateCompiledIdentityFieldValueOnSnapshot = [&](const SchemaIdentityNodeSnapshot& contextSnapshot,
                                                                  const auto& path) {
        IdentityConstraintFieldEvaluationResult result;

        const auto matchesCompiledIdentityStep = [&](const auto& step,
                                                     const std::string& localName,
                                                     const std::string& namespaceUri) {
            const bool localNameMatches = step.localName == "*" || localName == step.localName;
            const bool namespaceMatches = step.localName == "*" && step.namespaceUri.empty()
                ? true
                : namespaceUri == step.namespaceUri;
            return localNameMatches && namespaceMatches;
        };

        const auto matchesCompiledIdentityPredicate = [&](const auto& step, const SchemaIdentityNodeSnapshot& snapshot) {
            if (!step.predicateAttributeValue.has_value()) {
                return true;
            }

            const auto attribute = std::find_if(snapshot.attributes.begin(), snapshot.attributes.end(), [&](const auto& candidate) {
                return candidate.localName == step.predicateAttributeLocalName
                    && candidate.namespaceUri == step.predicateAttributeNamespaceUri
                    && candidate.value == *step.predicateAttributeValue;
            });
            return attribute != snapshot.attributes.end();
        };

        const auto appendMatchingDescendants = [&](const auto& self,
                                                   const SchemaIdentityNodeSnapshot& snapshot,
                                                   const auto& step,
                                                   std::vector<const SchemaIdentityNodeSnapshot*>& matches) -> void {
            for (const auto& child : snapshot.children) {
                if (child == nullptr) {
                    continue;
                }
                if (matchesCompiledIdentityStep(step, child->localName, child->namespaceUri)
                    && matchesCompiledIdentityPredicate(step, *child)) {
                    matches.push_back(child.get());
                }
                self(self, *child, step, matches);
            }
        };

        std::vector<const SchemaIdentityNodeSnapshot*> currentNodes{std::addressof(contextSnapshot)};
        for (std::size_t stepIndex = 0; stepIndex < path.steps.size(); ++stepIndex) {
            const auto& step = path.steps[stepIndex];
            const bool isLastStep = stepIndex + 1 == path.steps.size();

            if (isLastStep && step.kind == std::decay_t<decltype(step)>::Kind::Attribute) {
                for (const auto* node : currentNodes) {
                    if (node == nullptr) {
                        continue;
                    }

                    for (const auto& attribute : node->attributes) {
                        if (!matchesCompiledIdentityStep(step, attribute.localName, attribute.namespaceUri)) {
                            continue;
                        }

                        if (result.found) {
                            result.multiple = true;
                            return result;
                        }

                        result.found = true;
                        result.value = attribute.value;
                    }
                }

                return result;
            }

            std::vector<const SchemaIdentityNodeSnapshot*> nextNodes;
            for (const auto* node : currentNodes) {
                if (node == nullptr) {
                    continue;
                }

                if (step.kind == std::decay_t<decltype(step)>::Kind::Self) {
                    nextNodes.push_back(node);
                    continue;
                }

                if (step.kind == std::decay_t<decltype(step)>::Kind::Element) {
                    for (const auto& child : node->children) {
                        if (child != nullptr
                            && matchesCompiledIdentityStep(step, child->localName, child->namespaceUri)
                            && matchesCompiledIdentityPredicate(step, *child)) {
                            nextNodes.push_back(child.get());
                        }
                    }
                    continue;
                }

                if (step.kind == std::decay_t<decltype(step)>::Kind::DescendantElement) {
                    appendMatchingDescendants(appendMatchingDescendants, *node, step, nextNodes);
                }
            }

            currentNodes = std::move(nextNodes);
            if (currentNodes.empty()) {
                return result;
            }
        }

        for (const auto* node : currentNodes) {
            if (node == nullptr) {
                continue;
            }
            if (result.found) {
                result.multiple = true;
                return result;
            }

            result.found = true;
            result.value = evaluateIdentitySnapshotStringValue(evaluateIdentitySnapshotStringValue, *node);
        }

        return result;
    };

    const auto forEachIdentityConstraintTupleFromSnapshot = [&](const SchemaIdentityNodeSnapshot& scopeSnapshot,
                                                               const XmlSchemaSet::ElementRule::IdentityConstraint& constraint,
                                                               const auto& onTuple) {
        const auto selectedNodes = evaluateCompiledIdentityPathOnSnapshot(scopeSnapshot, *constraint.compiledSelectorPath);

        for (const auto& selectedNode : selectedNodes) {
            if (selectedNode.node == nullptr) {
                continue;
            }

            bool skipTuple = false;
            if (constraint.fieldXPaths.size() == 1) {
                const std::string& fieldXPath = constraint.fieldXPaths.front();
                const auto fieldEvaluation = evaluateCompiledIdentityFieldValueOnSnapshot(
                    *selectedNode.node,
                    *constraint.compiledFieldPaths.front());
                if (fieldEvaluation.multiple) {
                    throw XmlException(
                        "Schema validation failed: identity constraint '" + constraint.name
                        + "' field XPath '" + fieldXPath + "' must select at most one node per selector match");
                }
                if (!fieldEvaluation.found) {
                    if (constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::Key) {
                        throw XmlException(
                            "Schema validation failed: identity constraint '" + constraint.name
                            + "' requires every selector match to provide field '" + fieldXPath + "'");
                    }
                    continue;
                }
                if (fieldEvaluation.value.empty()) {
                    if (constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::Key) {
                        throw XmlException(
                            "Schema validation failed: identity constraint '" + constraint.name
                            + "' requires every selector match to provide a non-empty value for field '" + fieldXPath + "'");
                    }
                    continue;
                }

                onTuple(fieldEvaluation.value);
                continue;
            }

            std::vector<std::string> fieldValues;
            fieldValues.reserve(constraint.fieldXPaths.size());
            for (std::size_t fieldIndex = 0; fieldIndex < constraint.fieldXPaths.size(); ++fieldIndex) {
                const std::string& fieldXPath = constraint.fieldXPaths[fieldIndex];
                const auto fieldEvaluation = evaluateCompiledIdentityFieldValueOnSnapshot(*selectedNode.node, *constraint.compiledFieldPaths[fieldIndex]);
                if (fieldEvaluation.multiple) {
                    throw XmlException(
                        "Schema validation failed: identity constraint '" + constraint.name
                        + "' field XPath '" + fieldXPath + "' must select at most one node per selector match");
                }
                if (!fieldEvaluation.found) {
                    if (constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::Key) {
                        throw XmlException(
                            "Schema validation failed: identity constraint '" + constraint.name
                            + "' requires every selector match to provide field '" + fieldXPath + "'");
                    }
                    skipTuple = true;
                    break;
                }

                if (fieldEvaluation.value.empty()) {
                    if (constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::Key) {
                        throw XmlException(
                            "Schema validation failed: identity constraint '" + constraint.name
                            + "' requires every selector match to provide a non-empty value for field '" + fieldXPath + "'");
                    }
                    skipTuple = true;
                    break;
                }
                fieldValues.push_back(fieldEvaluation.value);
            }

            if (!skipTuple) {
                onTuple(SerializeIdentityConstraintTuple(fieldValues));
            }
        }
    };

    std::vector<ReaderIdentityScope> identityScopeStack;

    const auto findOpenIdentityScopeForConstraint = [&](const std::string& localName, const std::string& namespaceUri) -> ReaderIdentityScope* {
        const std::string constraintKey = BuildIdentityConstraintLookupKey(localName, namespaceUri);
        for (std::size_t index = identityScopeStack.size(); index > 0; --index) {
            ReaderIdentityScope& scope = identityScopeStack[index - 1];
            if (scope.rule == nullptr || scope.declaredConstraintKeys.find(constraintKey) == scope.declaredConstraintKeys.end()) {
                continue;
            }
            return std::addressof(scope);
        }
        return nullptr;
    };

    const auto validatePendingIdentityKeyRefsAgainstScope = [&](const ReaderIdentityScope& scope) {
        for (const auto& pendingKeyRef : scope.pendingKeyRefs) {
            const auto found = scope.tableIndexByConstraintKey.find(
                BuildIdentityConstraintLookupKey(
                    pendingKeyRef.constraint->referName,
                    pendingKeyRef.constraint->referNamespaceUri));
            if (found == scope.tableIndexByConstraintKey.end()) {
                throw XmlException(
                    "Schema validation failed: keyref '" + pendingKeyRef.constraint->name
                    + "' could not find an in-scope referenced key/unique constraint");
            }

            const auto& referencedTable = scope.tables[found->second];

            for (const auto& tuple : pendingKeyRef.tuples) {
                if (referencedTable.tuples.find(tuple) == referencedTable.tuples.end()) {
                    throw XmlException(
                        "Schema validation failed: keyref '" + pendingKeyRef.constraint->name
                        + "' references a value with no matching key/unique tuple");
                }
            }
        }
    };

    const auto finalizeReaderIdentityScope = [&](const SchemaValidationFrame& frame) {
        if (identityScopeStack.empty() || identityScopeStack.back().depth != frame.depth) {
            return;
        }

        ReaderIdentityScope& scope = identityScopeStack.back();
        if (scope.rule == nullptr || scope.snapshot == nullptr) {
            identityScopeStack.pop_back();
            return;
        }

        for (const auto& constraint : scope.rule->identityConstraints) {
            if (constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::KeyRef) {
                continue;
            }

            ReaderIdentityConstraintTable table;
            table.constraint = std::addressof(constraint);
            forEachIdentityConstraintTupleFromSnapshot(*scope.snapshot, constraint, [&](const std::string& tuple) {
                if (!table.tuples.insert(tuple).second) {
                    const char* kindLabel = constraint.kind == XmlSchemaSet::ElementRule::IdentityConstraint::Kind::Key
                        ? "key"
                        : "unique";
                    throw XmlException(
                        std::string("Schema validation failed: identity constraint '") + constraint.name
                        + "' violates " + kindLabel + " uniqueness under element '" + frame.name + "'");
                }
            });
            scope.tables.push_back(std::move(table));
            scope.tableIndexByConstraintKey.emplace(
                BuildIdentityConstraintLookupKey(constraint.name, constraint.namespaceUri),
                scope.tables.size() - 1);
        }

        validatePendingIdentityKeyRefsAgainstScope(scope);

        for (const auto& constraint : scope.rule->identityConstraints) {
            if (constraint.kind != XmlSchemaSet::ElementRule::IdentityConstraint::Kind::KeyRef) {
                continue;
            }

            const auto found = scope.tableIndexByConstraintKey.find(
                BuildIdentityConstraintLookupKey(constraint.referName, constraint.referNamespaceUri));
            if (found != scope.tableIndexByConstraintKey.end()) {
                const auto& referencedTable = scope.tables[found->second];
                forEachIdentityConstraintTupleFromSnapshot(*scope.snapshot, constraint, [&](const std::string& tuple) {
                    if (referencedTable.tuples.find(tuple) == referencedTable.tuples.end()) {
                        throw XmlException(
                            "Schema validation failed: keyref '" + constraint.name
                            + "' references a value with no matching key/unique tuple");
                    }
                });
                continue;
            }

            ReaderIdentityScope* referencedScope = findOpenIdentityScopeForConstraint(constraint.referName, constraint.referNamespaceUri);
            if (referencedScope == nullptr) {
                throw XmlException(
                    "Schema validation failed: keyref '" + constraint.name
                    + "' could not find an in-scope referenced key/unique constraint");
            }

            std::vector<std::string> tuples;
            forEachIdentityConstraintTupleFromSnapshot(*scope.snapshot, constraint, [&](const std::string& tuple) {
                tuples.push_back(tuple);
            });

            referencedScope->pendingKeyRefs.push_back(ReaderPendingKeyRefValidation{
                std::addressof(constraint),
                tuples});
        }

        identityScopeStack.pop_back();
    };

    std::function<void(const XmlSchemaSet::SimpleTypeRule&, const std::string&, const std::string&, const std::vector<std::pair<std::string, std::string>>&)> recordIdentityTypedValue;
    recordIdentityTypedValue = [&](const XmlSchemaSet::SimpleTypeRule& type, const std::string& value, const std::string& label,
                                   const std::vector<std::pair<std::string, std::string>>& namespaceBindings) {
        if (type.variety == XmlSchemaSet::SimpleTypeRule::Variety::List) {
            if (type.itemType == nullptr) {
                return;
            }

            std::istringstream stream(NormalizeSchemaSimpleTypeValue(type, value));
            std::string item;
            std::size_t index = 0;
            while (stream >> item) {
                ++index;
                recordIdentityTypedValue(*type.itemType, item, label + " list item " + std::to_string(index), namespaceBindings);
            }
            return;
        }

        if (type.variety == XmlSchemaSet::SimpleTypeRule::Variety::Union) {
            for (const auto& memberType : type.memberTypes) {
                try {
                    ValidateSchemaSimpleValue(
                        memberType,
                        value,
                        label,
                        namespaceBindings,
                        reader.notationDeclarationNames_,
                        reader.unparsedEntityDeclarationNames_);
                    recordIdentityTypedValue(memberType, value, label, namespaceBindings);
                    return;
                } catch (const XmlException&) {
                }
            }
            return;
        }

        const std::string normalizedValue = NormalizeSchemaSimpleTypeValue(type, value);
        if (type.baseType == "xs:ID") {
            if (!identityState.declaredIds.insert(normalizedValue).second) {
                throw XmlException("Schema validation failed: " + label + " duplicates xs:ID value '" + normalizedValue + "'");
            }
            return;
        }
        if (type.baseType == "xs:IDREF") {
            identityState.pendingIdReferences.push_back(PendingSchemaIdReference{normalizedValue, label});
        }
    };

    const auto runtimeSimpleTypeDerivesFrom = [&](const XmlSchemaSet::SimpleTypeRule& derivedType,
        const XmlSchemaSet::SimpleTypeRule& baseType,
        bool& usesRestriction) {
        if (!derivedType.namedTypeName.empty()
            && derivedType.namedTypeName == baseType.namedTypeName
            && derivedType.namedTypeNamespaceUri == baseType.namedTypeNamespaceUri) {
            return true;
        }
        if (derivedType.namedTypeName.empty()
            && baseType.namedTypeName.empty()
            && derivedType.derivationMethod == XmlSchemaSet::SimpleTypeRule::DerivationMethod::None
            && baseType.derivationMethod == XmlSchemaSet::SimpleTypeRule::DerivationMethod::None
            && derivedType.variety == baseType.variety) {
            bool builtinUsesRestriction = false;
            if (BuiltinSimpleTypeDerivesFrom(derivedType.baseType, baseType.baseType, builtinUsesRestriction)) {
                usesRestriction = usesRestriction || builtinUsesRestriction;
                return true;
            }
        }

        const XmlSchemaSet::SimpleTypeRule* current = &derivedType;
        std::unordered_set<std::string> visited;
        while (true) {
            if (current->derivationMethod == XmlSchemaSet::SimpleTypeRule::DerivationMethod::Restriction) {
                usesRestriction = true;
            }
            if (!baseType.namedTypeName.empty()
                && current->derivationBaseName == baseType.namedTypeName
                && current->derivationBaseNamespaceUri == baseType.namedTypeNamespaceUri) {
                return true;
            }
            if (baseType.namedTypeName.empty()
                && current->derivationMethod == XmlSchemaSet::SimpleTypeRule::DerivationMethod::Restriction) {
                bool builtinUsesRestriction = false;
                if (BuiltinSimpleTypeDerivesFrom(current->baseType, baseType.baseType, builtinUsesRestriction)) {
                    usesRestriction = usesRestriction || builtinUsesRestriction;
                    return true;
                }
            }
            if (current->derivationBaseName.empty()) {
                return false;
            }

            const std::string currentKey = current->derivationBaseNamespaceUri + "\n" + current->derivationBaseName;
            if (!visited.insert(currentKey).second) {
                return false;
            }

            current = schemas.FindSimpleTypeRule(current->derivationBaseName, current->derivationBaseNamespaceUri);
            if (current == nullptr) {
                return false;
            }
            if (!baseType.namedTypeName.empty()
                && current->namedTypeName == baseType.namedTypeName
                && current->namedTypeNamespaceUri == baseType.namedTypeNamespaceUri) {
                return true;
            }
        }
    };

    const auto runtimeComplexTypeDerivesFrom = [&](const XmlSchemaSet::ComplexTypeRule& derivedType,
        const XmlSchemaSet::ComplexTypeRule& baseType,
        bool& usesRestriction,
        bool& usesExtension) {
        if (baseType.namedTypeName == "anyType"
            && baseType.namedTypeNamespaceUri == "http://www.w3.org/2001/XMLSchema") {
            return true;
        }
        if (!derivedType.namedTypeName.empty()
            && derivedType.namedTypeName == baseType.namedTypeName
            && derivedType.namedTypeNamespaceUri == baseType.namedTypeNamespaceUri) {
            return true;
        }

        const XmlSchemaSet::ComplexTypeRule* current = &derivedType;
        std::unordered_set<std::string> visited;
        while (true) {
            if (current->derivationMethod == XmlSchemaSet::ComplexTypeRule::DerivationMethod::Restriction) {
                usesRestriction = true;
            } else if (current->derivationMethod == XmlSchemaSet::ComplexTypeRule::DerivationMethod::Extension) {
                usesExtension = true;
            }
            if (current->derivationBaseName == baseType.namedTypeName
                && current->derivationBaseNamespaceUri == baseType.namedTypeNamespaceUri) {
                return true;
            }
            if (current->derivationBaseName.empty()) {
                return false;
            }

            const std::string currentKey = current->derivationBaseNamespaceUri + "\n" + current->derivationBaseName;
            if (!visited.insert(currentKey).second) {
                return false;
            }

            current = schemas.FindComplexTypeRule(current->derivationBaseName, current->derivationBaseNamespaceUri);
            if (current == nullptr) {
                return false;
            }
            if (!baseType.namedTypeName.empty()
                && current->namedTypeName == baseType.namedTypeName
                && current->namedTypeNamespaceUri == baseType.namedTypeNamespaceUri) {
                return true;
            }
        }
    };

    const auto runtimeElementCanSubstituteForHead = [&](const XmlSchemaSet::ElementRule& actualRule,
        const XmlSchemaSet::ElementRule& declaration) {
        bool usesRestriction = false;
        bool usesExtension = false;
        if (declaration.declaredComplexType.has_value()) {
            if (!actualRule.declaredComplexType.has_value()) {
                return false;
            }
            if (!runtimeComplexTypeDerivesFrom(*actualRule.declaredComplexType, *declaration.declaredComplexType, usesRestriction, usesExtension)) {
                return false;
            }
        } else if (declaration.declaredSimpleType.has_value()) {
            if (!actualRule.declaredSimpleType.has_value()) {
                return false;
            }
            if (!runtimeSimpleTypeDerivesFrom(*actualRule.declaredSimpleType, *declaration.declaredSimpleType, usesRestriction)) {
                return false;
            }
        }

        if ((usesRestriction && declaration.blockRestriction)
            || (usesExtension && declaration.blockExtension)) {
            return false;
        }
        return true;
    };

    const auto elementMatchesDeclaration = [&](const SchemaObservedChildElement& element, const XmlSchemaSet::ElementRule& declaration) {
        const auto* actualRule = schemas.FindElementRule(element.localName, element.namespaceUri);
        if (actualRule == nullptr) {
            return false;
        }
        if (actualRule->name == declaration.name && actualRule->namespaceUri == declaration.namespaceUri) {
            return true;
        }
        if (declaration.blockSubstitution) {
            return false;
        }

        std::string currentHeadName = actualRule->substitutionGroupHeadName;
        std::string currentHeadNamespaceUri = actualRule->substitutionGroupHeadNamespaceUri;
        std::unordered_set<std::string> visitedHeads;
        while (!currentHeadName.empty()) {
            const std::string headKey = currentHeadNamespaceUri + "\n" + currentHeadName;
            if (!visitedHeads.insert(headKey).second) {
                break;
            }
            if (currentHeadName == declaration.name && currentHeadNamespaceUri == declaration.namespaceUri) {
                return runtimeElementCanSubstituteForHead(*actualRule, declaration);
            }
            const auto* headRule = schemas.FindElementRule(currentHeadName, currentHeadNamespaceUri);
            if (headRule == nullptr) {
                break;
            }
            currentHeadName = headRule->substitutionGroupHeadName;
            currentHeadNamespaceUri = headRule->substitutionGroupHeadNamespaceUri;
        }
        return false;
    };

    std::string rootNamespaceUri;
    const auto wildcardNamespaceTokenMatches = [&](const std::string& token, const std::string& namespaceUri) {
        if (token.empty() || token == "##any") {
            return true;
        }
        if (token == "##other") {
            return namespaceUri != rootNamespaceUri;
        }
        if (token == "##targetNamespace") {
            return namespaceUri == rootNamespaceUri;
        }
        if (token == "##local") {
            return namespaceUri.empty();
        }
        return namespaceUri == token;
    };

    const auto matchesWildcardNamespace = [&](const std::string& namespaceConstraint, const std::string& namespaceUri) {
        if (namespaceConstraint.empty() || namespaceConstraint == "##any") {
            return true;
        }
        std::istringstream tokens(namespaceConstraint);
        std::string token;
        while (tokens >> token) {
            if (wildcardNamespaceTokenMatches(token, namespaceUri)) {
                return true;
            }
        }
        return false;
    };

    const auto wildcardAllowsValidationSkip = [](const std::string& processContents) {
        return processContents == "skip";
    };

    const auto wildcardAllowsUndeclared = [](const std::string& processContents) {
        return processContents == "lax" || processContents == "skip";
    };

    const auto resolveBuiltinSimpleType = [](const std::string& qualifiedName) -> std::optional<XmlSchemaSet::SimpleTypeRule> {
        const auto descriptor = ResolveBuiltinSimpleTypeDescriptor(qualifiedName);
        if (!descriptor.has_value()) {
            return std::nullopt;
        }

        XmlSchemaSet::SimpleTypeRule rule;
        rule.baseType = descriptor->baseType;
        rule.whiteSpace = descriptor->whiteSpace;
        if (descriptor->variety == BuiltinSimpleTypeDescriptor::Variety::List) {
            rule.variety = XmlSchemaSet::SimpleTypeRule::Variety::List;

            XmlSchemaSet::SimpleTypeRule itemType;
            itemType.baseType = descriptor->itemType;
            itemType.whiteSpace = descriptor->itemWhiteSpace;
            rule.itemType = std::make_shared<XmlSchemaSet::SimpleTypeRule>(itemType);
        }
        return rule;
    };

    const auto resolveBuiltinComplexType = [](const std::string& qualifiedName) -> std::optional<XmlSchemaSet::ComplexTypeRule> {
        if (qualifiedName != "xs:anyType") {
            return std::nullopt;
        }

        XmlSchemaSet::ComplexTypeRule rule;
        rule.namedTypeName = "anyType";
        rule.namedTypeNamespaceUri = "http://www.w3.org/2001/XMLSchema";
        rule.allowsText = true;
        rule.anyAttributeAllowed = true;
        rule.anyAttributeNamespaceConstraint = "##any";
        rule.anyAttributeProcessContents = "lax";

        XmlSchemaSet::Particle particle;
        particle.kind = XmlSchemaSet::Particle::Kind::Any;
        particle.namespaceUri = "##any";
        particle.processContents = "lax";
        particle.minOccurs = 0;
        particle.maxOccurs = std::numeric_limits<std::size_t>::max();
        rule.particle = particle;
        return rule;
    };

    const auto applyComplexTypeToElementRule = [](XmlSchemaSet::ElementRule& elementRule, const XmlSchemaSet::ComplexTypeRule& complexTypeRule) {
        elementRule.allowsText = complexTypeRule.allowsText;
        elementRule.textType = complexTypeRule.textType;
        elementRule.contentModel = complexTypeRule.contentModel;
        elementRule.attributes = complexTypeRule.attributes;
        elementRule.children = complexTypeRule.children;
        elementRule.particle = complexTypeRule.particle;
        elementRule.anyAttributeAllowed = complexTypeRule.anyAttributeAllowed;
        elementRule.anyAttributeNamespaceConstraint = complexTypeRule.anyAttributeNamespaceConstraint;
        elementRule.anyAttributeProcessContents = complexTypeRule.anyAttributeProcessContents;
    };

    const auto applySimpleTypeToElementRule = [](XmlSchemaSet::ElementRule& elementRule, const XmlSchemaSet::SimpleTypeRule& simpleTypeRule) {
        elementRule.declaredSimpleType = simpleTypeRule;
        elementRule.declaredComplexType.reset();
        elementRule.allowsText = true;
        elementRule.textType = simpleTypeRule;
        elementRule.contentModel = XmlSchemaSet::ContentModel::Empty;
        elementRule.attributes.clear();
        elementRule.children.clear();
        elementRule.particle.reset();
        elementRule.anyAttributeAllowed = false;
        elementRule.anyAttributeNamespaceConstraint = "##any";
        elementRule.anyAttributeProcessContents = "strict";
    };

    const auto parseSchemaInstanceTypeName = [&](const SchemaValidationFrame& frame, const std::string& lexicalValue) {
        const auto [prefix, localName] = SplitQualifiedName(lexicalValue);
        try {
            if (prefix.empty()) {
                (void)XmlConvert::VerifyNCName(localName);
                return std::pair<std::string, std::string>{localName, std::string{}};
            }

            (void)XmlConvert::VerifyNCName(prefix);
            (void)XmlConvert::VerifyNCName(localName);
            const std::string namespaceUri = LookupNamespaceUriOnSchemaFrame(frame, prefix);
            if (namespaceUri.empty()) {
                throw XmlException("undefined QName prefix");
            }
            return std::pair<std::string, std::string>{localName, namespaceUri};
        } catch (const XmlException&) {
            throw XmlException(
                "Schema validation failed: xsi:type on element '" + frame.name + "' must be a valid xs:QName value");
        }
    };

    const auto resolveEffectiveElementRule = [&](const SchemaValidationFrame& frame, const XmlSchemaSet::ElementRule& declaration) {
        XmlSchemaSet::ElementRule effectiveRule = declaration;
        const auto* typeAttribute = FindObservedSchemaAttribute(frame.observedAttributes, "type", kSchemaInstanceNamespace);
        if (typeAttribute == nullptr) {
            return effectiveRule;
        }

        const auto [typeLocalName, typeNamespaceUri] = parseSchemaInstanceTypeName(frame, typeAttribute->value);
        const std::string typeDisplayName = typeAttribute->value;

        if (const auto builtinType = resolveBuiltinSimpleType(typeNamespaceUri == "http://www.w3.org/2001/XMLSchema"
                ? "xs:" + typeLocalName
                : std::string{}); builtinType.has_value()) {
            if (declaration.declaredComplexType.has_value()) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }

            bool usesRestriction = false;
            if (declaration.declaredSimpleType.has_value()
                && !runtimeSimpleTypeDerivesFrom(*builtinType, *declaration.declaredSimpleType, usesRestriction)) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }
            if (usesRestriction && declaration.blockRestriction) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' uses blocked restriction derivation");
            }

            applySimpleTypeToElementRule(effectiveRule, *builtinType);
            return effectiveRule;
        }

        if (const auto builtinComplexType = resolveBuiltinComplexType(typeNamespaceUri == "http://www.w3.org/2001/XMLSchema"
                ? "xs:" + typeLocalName
                : std::string{}); builtinComplexType.has_value()) {
            if (declaration.declaredSimpleType.has_value()) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }

            bool usesRestriction = false;
            bool usesExtension = false;
            if (declaration.declaredComplexType.has_value()
                && !runtimeComplexTypeDerivesFrom(*builtinComplexType, *declaration.declaredComplexType, usesRestriction, usesExtension)) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }

            effectiveRule.declaredSimpleType.reset();
            effectiveRule.declaredComplexType = *builtinComplexType;
            applyComplexTypeToElementRule(effectiveRule, *builtinComplexType);
            return effectiveRule;
        }

        if (const auto* namedSimpleType = schemas.FindSimpleTypeRule(typeLocalName, typeNamespaceUri); namedSimpleType != nullptr) {
            if (declaration.declaredComplexType.has_value()) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }

            bool usesRestriction = false;
            if (declaration.declaredSimpleType.has_value()
                && !runtimeSimpleTypeDerivesFrom(*namedSimpleType, *declaration.declaredSimpleType, usesRestriction)) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }
            if (usesRestriction && declaration.blockRestriction) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' uses blocked restriction derivation");
            }

            applySimpleTypeToElementRule(effectiveRule, *namedSimpleType);
            return effectiveRule;
        }

        if (const auto* namedComplexType = schemas.FindComplexTypeRule(typeLocalName, typeNamespaceUri); namedComplexType != nullptr) {
            if (declaration.declaredSimpleType.has_value()) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }

            bool usesRestriction = false;
            bool usesExtension = false;
            if (declaration.declaredComplexType.has_value()
                && !runtimeComplexTypeDerivesFrom(*namedComplexType, *declaration.declaredComplexType, usesRestriction, usesExtension)) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' must name a type derived from the declared element type");
            }
            const bool blockedByBaseType = declaration.declaredComplexType.has_value()
                && ((usesRestriction && declaration.declaredComplexType->blockRestriction)
                    || (usesExtension && declaration.declaredComplexType->blockExtension));
            if ((usesRestriction && declaration.blockRestriction)
                || (usesExtension && declaration.blockExtension)
                || blockedByBaseType) {
                throw XmlException(
                    "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
                    + "' uses blocked type derivation");
            }

            effectiveRule.declaredSimpleType.reset();
            effectiveRule.declaredComplexType = *namedComplexType;
            applyComplexTypeToElementRule(effectiveRule, *namedComplexType);
            return effectiveRule;
        }

        throw XmlException(
            "Schema validation failed: xsi:type '" + typeDisplayName + "' on element '" + frame.name
            + "' is not a supported schema type");
    };

    if (reader.NodeType() != XmlNodeType::Element) {
        while (reader.Read()) {
            if (reader.NodeType() == XmlNodeType::Element) {
                break;
            }
        }
    }

    if (reader.NodeType() == XmlNodeType::Element) {
        rootRuleNamespaceUri = reader.NamespaceURI();
        if (const auto* rootRule = schemas.FindElementRule(reader.LocalName(), reader.NamespaceURI()); rootRule != nullptr) {
            const SchemaValidationFrame rootFrame = BuildSchemaValidationFrame(reader);
            const XmlSchemaSet::ElementRule effectiveRootRule = resolveEffectiveElementRule(rootFrame, *rootRule);

            std::unordered_set<const XmlSchemaSet::ElementRule*> visitedElementRules;
            std::unordered_set<const XmlSchemaSet::ComplexTypeRule*> visitedComplexTypes;
            if (elementRuleRequiresDom(effectiveRootRule, visitedElementRules, visitedComplexTypes)) {
                if (const auto* stringSource = dynamic_cast<const StringXmlReaderInputSource*>(reader.inputSource_.get());
                    stringSource != nullptr && stringSource->Text() != nullptr) {
                    const auto document = XmlDocument::Parse(*stringSource->Text());
                    document->Validate(schemas);
                    return;
                }
                const auto document = BuildXmlDocumentFromReader(reader);
                document->Validate(schemas);
                return;
            }
        }
    }

    const auto validateLocalFrame = [&](const SchemaValidationFrame& frame) {
        const auto* rule = schemas.FindElementRule(frame.localName, frame.namespaceUri);
        if (rule == nullptr) {
            throw XmlException("Schema validation failed: no schema declaration found for element '" + frame.name + "'");
        }
        if (rootNamespaceUri.empty()) {
            rootNamespaceUri = frame.namespaceUri;
        }
        if (rule->isAbstract) {
            throw XmlException("Schema validation failed: element '" + frame.name + "' is abstract and cannot appear in the document");
        }

        const XmlSchemaSet::ElementRule effectiveRule = resolveEffectiveElementRule(frame, *rule);
        rule = &effectiveRule;

        const auto* nilAttribute = FindObservedSchemaAttribute(frame.observedAttributes, "nil", kSchemaInstanceNamespace);
        bool isNilled = false;
        if (nilAttribute != nullptr) {
            if (!TryParseXmlSchemaBoolean(nilAttribute->value, isNilled)) {
                throw XmlException(
                    "Schema validation failed: xsi:nil on element '" + frame.name + "' must be a valid xs:boolean value");
            }
            if (!rule->isNillable) {
                throw XmlException(
                    "Schema validation failed: element '" + frame.name + "' is not declared nillable and cannot use xsi:nil");
            }
        }

        for (const auto& attributeRule : rule->attributes) {
            const std::optional<std::string> effectiveAttributeValue =
                ResolveSchemaAttributeEffectiveValue(attributeRule, frame.name, frame.observedAttributes);

            if (effectiveAttributeValue.has_value() && attributeRule.type.has_value()) {
                ValidateSchemaSimpleValue(
                    *attributeRule.type,
                    *effectiveAttributeValue,
                    "attribute '" + attributeRule.name + "' on element '" + frame.name + "'",
                    frame.namespaceBindings,
                    reader.notationDeclarationNames_,
                    reader.unparsedEntityDeclarationNames_);
                recordIdentityTypedValue(*attributeRule.type, *effectiveAttributeValue,
                    "attribute '" + attributeRule.name + "' on element '" + frame.name + "'", frame.namespaceBindings);
            }
        }

        for (const auto& attribute : frame.observedAttributes) {
            if (attribute.name == "xmlns" || attribute.prefix == "xmlns" || attribute.prefix == "xml") {
                continue;
            }
            if (attribute.localName == "nil" && attribute.namespaceUri == kSchemaInstanceNamespace) {
                continue;
            }
            if (attribute.localName == "type" && attribute.namespaceUri == kSchemaInstanceNamespace) {
                continue;
            }
            const bool declared = std::any_of(rule->attributes.begin(), rule->attributes.end(), [&](const auto& attributeRule) {
                return attribute.localName == attributeRule.name && attribute.namespaceUri == attributeRule.namespaceUri;
            });
            const bool wildcardMatches = rule->anyAttributeAllowed
                && matchesWildcardNamespace(rule->anyAttributeNamespaceConstraint, attribute.namespaceUri);
            const bool allowedByWildcard = wildcardMatches
                && wildcardAllowsUndeclared(rule->anyAttributeProcessContents);
            if (!declared && !allowedByWildcard) {
                throw XmlException(
                    "Schema validation failed: attribute '" + attribute.name
                    + "' is not declared for element '" + frame.name + "'");
            }
        }

        const SchemaElementObservedContent& observedContent = frame.observedContent;
        const auto& elementChildren = observedContent.elementChildren;
        const bool hasSignificantText = observedContent.hasSignificantText;

        if (isNilled && (!elementChildren.empty() || hasSignificantText)) {
            throw XmlException(
                "Schema validation failed: element '" + frame.name + "' is nilled and must not contain text or child elements");
        }

        const std::optional<std::string> effectiveElementValue =
            ResolveSchemaElementEffectiveValue(*rule, frame.name, observedContent, isNilled);

        if (!isNilled) {
            if (rule->particle.has_value()) {
                const auto matches = CollectObservedParticleMatches(
                    *rule->particle,
                    elementChildren,
                    [&](const std::string& localName, const std::string& namespaceUri) {
                        return schemas.FindElementRule(localName, namespaceUri);
                    },
                    elementMatchesDeclaration,
                    matchesWildcardNamespace,
                    wildcardAllowsUndeclared,
                    [](const auto& particle) { return particle.kind == std::decay_t<decltype(particle)>::Kind::Element; },
                    [](const auto& particle) { return particle.kind == std::decay_t<decltype(particle)>::Kind::Any; },
                    [](const auto& particle) { return particle.kind == std::decay_t<decltype(particle)>::Kind::Sequence; },
                    [](const auto& particle) { return particle.kind == std::decay_t<decltype(particle)>::Kind::All; },
                    [](const auto& particle) -> const auto& { return particle.children; },
                    [](const auto& particle) -> const auto& { return particle.name; },
                    [](const auto& particle) -> const auto& { return particle.namespaceUri; },
                    [](const auto& particle) -> const auto& { return particle.processContents; },
                    [](const auto& particle) { return particle.minOccurs; },
                    [](const auto& particle) { return particle.maxOccurs; },
                    [](const auto& childElement) -> const auto& { return childElement.localName; },
                    [](const auto& childElement) -> const auto& { return childElement.namespaceUri; });
                if (std::find(matches.begin(), matches.end(), elementChildren.size()) == matches.end()) {
                    throw XmlException(
                        "Schema validation failed: child element structure under element '" + frame.name + "' does not satisfy the declared content model");
                }
            } else if (!rule->children.empty()) {
                if (rule->contentModel == XmlSchemaSet::ContentModel::Choice) {
                    std::optional<std::size_t> selectedChoiceIndex;
                    std::vector<std::size_t> counts(rule->children.size(), 0);
                    for (const auto& childElement : elementChildren) {
                        const auto match = std::find_if(rule->children.begin(), rule->children.end(), [&](const auto& childRule) {
                            const auto* declaredRule = schemas.FindElementRule(childRule.name, childRule.namespaceUri);
                            return declaredRule != nullptr && elementMatchesDeclaration(childElement, *declaredRule);
                        });
                        if (match == rule->children.end()) {
                            throw XmlException(
                                "Schema validation failed: unexpected child element '" + childElement.name
                                + "' under element '" + frame.name + "'");
                        }

                        const std::size_t choiceIndex = static_cast<std::size_t>(std::distance(rule->children.begin(), match));
                        if (selectedChoiceIndex.has_value() && selectedChoiceIndex.value() != choiceIndex) {
                            throw XmlException(
                                "Schema validation failed: element '" + frame.name + "' allows only one choice branch at a time");
                        }
                        selectedChoiceIndex = choiceIndex;

                        const auto& choiceRule = rule->children[choiceIndex];
                        ++counts[choiceIndex];
                        if (choiceRule.maxOccurs != std::numeric_limits<std::size_t>::max()
                            && counts[choiceIndex] > choiceRule.maxOccurs) {
                            throw XmlException(
                                "Schema validation failed: child element '" + choiceRule.name
                                + "' exceeded maxOccurs under element '" + frame.name + "'");
                        }
                    }

                    const auto requiredChoice = std::find_if(rule->children.begin(), rule->children.end(), [](const auto& childRule) {
                        return childRule.minOccurs > 0;
                    });
                    if (!selectedChoiceIndex.has_value()) {
                        if (requiredChoice != rule->children.end()) {
                            throw XmlException(
                                "Schema validation failed: element '" + frame.name + "' requires one of the declared choice elements");
                        }
                    } else {
                        const auto& selectedRule = rule->children[selectedChoiceIndex.value()];
                        if (counts[selectedChoiceIndex.value()] < selectedRule.minOccurs) {
                            throw XmlException(
                                "Schema validation failed: child element '" + selectedRule.name
                                + "' does not satisfy minOccurs under element '" + frame.name + "'");
                        }
                    }
                } else {
                    std::size_t childIndex = 0;
                    for (const auto& childRule : rule->children) {
                        std::size_t count = 0;
                        const auto* declaredRule = schemas.FindElementRule(childRule.name, childRule.namespaceUri);
                        if (declaredRule == nullptr) {
                            throw XmlException("Schema validation failed: no schema declaration found for element '" + childRule.name + "'");
                        }
                        while (childIndex < elementChildren.size()) {
                            const auto& childElement = elementChildren[childIndex];
                            if (!elementMatchesDeclaration(childElement, *declaredRule)) {
                                break;
                            }
                            ++count;
                            ++childIndex;
                            if (childRule.maxOccurs != std::numeric_limits<std::size_t>::max() && count == childRule.maxOccurs) {
                                break;
                            }
                        }
                        if (count < childRule.minOccurs) {
                            throw XmlException(
                                "Schema validation failed: expected child element '" + childRule.name
                                + "' under element '" + frame.name + "'");
                        }
                    }
                    if (childIndex != elementChildren.size()) {
                        throw XmlException(
                            "Schema validation failed: unexpected child element '" + elementChildren[childIndex].name
                            + "' under element '" + frame.name + "'");
                    }
                }
            } else if (!elementChildren.empty()) {
                throw XmlException(
                    "Schema validation failed: element '" + frame.name + "' does not allow child elements");
            }

            if (!rule->allowsText && hasSignificantText) {
                throw XmlException(
                    "Schema validation failed: element '" + frame.name + "' does not allow text content");
            }
            if (rule->textType.has_value() && effectiveElementValue.has_value()) {
                ValidateSchemaSimpleValue(
                    *rule->textType,
                    *effectiveElementValue,
                    "text content of element '" + frame.name + "'",
                    frame.namespaceBindings,
                    reader.notationDeclarationNames_,
                    reader.unparsedEntityDeclarationNames_);
                recordIdentityTypedValue(*rule->textType, *effectiveElementValue,
                    "text content of element '" + frame.name + "'", frame.namespaceBindings);
            }
        }
    };

    std::vector<SchemaValidationFrame> frameStack;
    const auto processCurrentReaderNode = [&]() {
        switch (reader.NodeType()) {
        case XmlNodeType::Element: {
            const auto parentBindings = frameStack.empty() ? std::vector<std::pair<std::string, std::string>>{} : frameStack.back().namespaceBindings;
            const auto* declaredRule = schemas.FindElementRule(reader.LocalName(), reader.NamespaceURI());
            SchemaValidationFrame frame = BuildSchemaValidationFrame(reader, parentBindings, false);
            std::shared_ptr<XmlSchemaSet::ElementRule> effectiveRule;
            if (declaredRule != nullptr) {
                effectiveRule = std::make_shared<XmlSchemaSet::ElementRule>(resolveEffectiveElementRule(frame, *declaredRule));
            }
            const bool captureIdentitySnapshot = !identityScopeStack.empty()
                || (effectiveRule != nullptr && !effectiveRule->identityConstraints.empty());
            if (captureIdentitySnapshot) {
                frame.identitySnapshot = std::make_shared<SchemaIdentityNodeSnapshot>();
                frame.identitySnapshot->name = frame.name;
                frame.identitySnapshot->localName = frame.localName;
                frame.identitySnapshot->namespaceUri = frame.namespaceUri;
                frame.identitySnapshot->attributes = frame.observedAttributes;
            }
            frameStack.push_back(std::move(frame));
            if (effectiveRule != nullptr && !effectiveRule->identityConstraints.empty()) {
                std::unordered_set<std::string> declaredConstraintKeys;
                for (const auto& constraint : effectiveRule->identityConstraints) {
                    if (constraint.kind != XmlSchemaSet::ElementRule::IdentityConstraint::Kind::KeyRef) {
                        declaredConstraintKeys.insert(BuildIdentityConstraintLookupKey(constraint.name, constraint.namespaceUri));
                    }
                }
                identityScopeStack.push_back(ReaderIdentityScope{
                    frameStack.back().depth,
                    std::move(effectiveRule),
                    frameStack.back().identitySnapshot,
                    {},
                    {},
                    std::move(declaredConstraintKeys),
                    {}});
            }
            if (frameStack.back().isEmptyElement) {
                validateLocalFrame(frameStack.back());
                finalizeReaderIdentityScope(frameStack.back());
                SchemaValidationFrame completed = std::move(frameStack.back());
                frameStack.pop_back();
                if (!frameStack.empty()) {
                    AppendObservedSchemaChildFrame(frameStack.back(), completed);
                }
            }
            break;
        }
        case XmlNodeType::Text:
        case XmlNodeType::CDATA:
        case XmlNodeType::Whitespace:
        case XmlNodeType::SignificantWhitespace:
            if (!frameStack.empty()) {
                AppendSchemaTextToFrame(frameStack.back(), reader.Value());
            }
            break;
        case XmlNodeType::EndElement:
            if (frameStack.empty()) {
                throw XmlException("Schema validation failed: unexpected end element while streaming validation");
            }
            validateLocalFrame(frameStack.back());
            finalizeReaderIdentityScope(frameStack.back());
            {
                SchemaValidationFrame completed = std::move(frameStack.back());
                frameStack.pop_back();
                if (!frameStack.empty()) {
                    AppendObservedSchemaChildFrame(frameStack.back(), completed);
                }
            }
            break;
        default:
            break;
        }
    };

    if (reader.GetReadState() == ReadState::Interactive && reader.NodeType() != XmlNodeType::None) {
        processCurrentReaderNode();
    }

    while (reader.Read()) {
        processCurrentReaderNode();
    }

    if (!frameStack.empty()) {
        throw XmlException("Schema validation failed: unexpected end of input during streaming validation");
    }
    if (!identityScopeStack.empty()) {
        throw XmlException("Schema validation failed: unexpected end of input during identity constraint validation");
    }

    ValidatePendingSchemaIdReferences(identityState);
}

void ValidateXmlReaderInputAgainstSchemas(const std::string& xml, const XmlReaderSettings& settings) {
    if (settings.Validation != ValidationType::Schema) {
        return;
    }
    if (settings.Conformance != ConformanceLevel::Document) {
        throw XmlException("Schema validation is only supported for document conformance mode");
    }
    if (settings.Schemas == nullptr || settings.Schemas->Count() == 0) {
        throw XmlException("Schema validation requires XmlReaderSettings.Schemas to contain at least one schema");
    }

    XmlReaderSettings validationSettings = settings;
    validationSettings.Validation = ValidationType::None;
    auto reader = XmlReader::Create(xml, validationSettings);
    XmlDocument validationDocument;
    validationDocument.ValidateReaderAgainstSchemas(reader, *settings.Schemas);
}

void ValidateXmlReaderInputAgainstSchemas(XmlReader& reader, const XmlReaderSettings& settings) {
    if (settings.Validation != ValidationType::Schema) {
        return;
    }
    if (settings.Conformance != ConformanceLevel::Document) {
        throw XmlException("Schema validation is only supported for document conformance mode");
    }
    if (settings.Schemas == nullptr || settings.Schemas->Count() == 0) {
        throw XmlException("Schema validation requires XmlReaderSettings.Schemas to contain at least one schema");
    }

    XmlDocument validationDocument;
    validationDocument.ValidateReaderAgainstSchemas(reader, *settings.Schemas);
}

XmlWriter::XmlWriter(XmlWriterSettings settings) : settings_(std::move(settings)) {
    fragmentRoot_ = document_.CreateDocumentFragment();
}

XmlWriter::XmlWriter(std::ostream& stream, XmlWriterSettings settings)
    : settings_(std::move(settings)), directOutputStream_(&stream) {
    fragmentRoot_ = document_.CreateDocumentFragment();
}

bool XmlWriter::UsesDirectOutput() const noexcept {
    return directOutputStream_ != nullptr;
}

WriteState XmlWriter::GetWriteState() const noexcept {
    if (documentClosed_) return WriteState::Closed;
    if (currentAttributeName_.has_value()) return WriteState::Attribute;
    if (UsesDirectOutput()) {
        if (!directElementStack_.empty()) {
            if (directElementStack_.back().startTagOpen) return WriteState::Element;
            return WriteState::Content;
        }
        if (startDocumentWritten_ || directHasTopLevelContent_) return WriteState::Prolog;
        return WriteState::Start;
    }
    if (!elementStack_.empty()) {
        if (!startTagOpenStack_.empty() && startTagOpenStack_.back()) return WriteState::Element;
        return WriteState::Content;
    }
    if (startDocumentWritten_ || !document_.ChildNodes().empty()) return WriteState::Prolog;
    return WriteState::Start;
}

std::string XmlWriter::LookupPrefix(const std::string& namespaceUri) const {
    if (UsesDirectOutput()) {
        return LookupDirectNamespacePrefix(namespaceUri);
    }
    return LookupNamespacePrefix(namespaceUri);
}

void XmlWriter::EnsureDocumentOpen(const std::string& operation) const {
    if (documentClosed_) {
        throw XmlException("Cannot " + operation + " after the XML document has been closed");
    }
}

void XmlWriter::EnsureOpenElement(const std::string& operation) const {
    EnsureDocumentOpen(operation);
    const bool hasOpenElement = UsesDirectOutput() ? !directElementStack_.empty() : !elementStack_.empty();
    if (!hasOpenElement) {
        throw XmlException("Cannot " + operation + " outside of an element");
    }
}

void XmlWriter::EnsureOpenStartElement(const std::string& operation) const {
    EnsureOpenElement(operation);
    const bool startTagOpen = UsesDirectOutput()
        ? directElementStack_.back().startTagOpen
        : startTagOpenStack_.back();
    if (!startTagOpen) {
        throw XmlException("Cannot " + operation + " after the current element start tag has been closed");
    }
}

void XmlWriter::EnsureNoOpenAttribute(const std::string& operation) const {
    if (currentAttributeName_.has_value()) {
        throw XmlException("Cannot " + operation + " while an attribute is still open");
    }
}

void XmlWriter::EnsureOutputReady() const {
    EnsureNoOpenAttribute("produce XML output");
    const bool hasOpenElements = UsesDirectOutput() ? !directElementStack_.empty() : !elementStack_.empty();
    if (hasOpenElements) {
        throw XmlException("Cannot produce XML output while elements are still open");
    }
    if (settings_.Conformance == ConformanceLevel::Fragment) {
        return;
    }
    const bool hasRootElement = UsesDirectOutput()
        ? directHasRootElement_
        : document_.DocumentElement() != nullptr;
    if (!hasRootElement) {
        throw XmlException("Cannot produce XML output without a root element");
    }
}

void XmlWriter::MarkCurrentElementContentStarted() {
    if (UsesDirectOutput()) {
        if (!directElementStack_.empty()) {
            directElementStack_.back().startTagOpen = false;
        }
        return;
    }
    if (!startTagOpenStack_.empty()) {
        startTagOpenStack_.back() = false;
    }
}

std::string XmlWriter::LookupNamespacePrefix(const std::string& namespaceUri) const {
    if (namespaceUri.empty()) {
        return {};
    }

    if (namespaceUri == "http://www.w3.org/XML/1998/namespace") {
        return "xml";
    }
    if (elementStack_.empty()) {
        return {};
    }

    for (const XmlElement* current = elementStack_.back().get(); current != nullptr; ) {
        for (const auto& attribute : current->Attributes()) {
            if (!IsNamespaceDeclarationName(attribute->Name())) {
                continue;
            }
            if (attribute->Value() == namespaceUri) {
                return NamespaceDeclarationPrefix(attribute->Name());
            }
        }

        const XmlNode* parent = current->ParentNode();
        current = parent != nullptr && parent->NodeType() == XmlNodeType::Element
            ? static_cast<const XmlElement*>(parent)
            : nullptr;
    }

    return {};
}

void XmlWriter::WriteDirectTopLevelSeparatorIfNeeded() {
    if (settings_.Indent && directHasTopLevelContent_) {
        *directOutputStream_ << settings_.NewLineChars;
    }
}

void XmlWriter::EnsureDirectCurrentStartTagClosed() {
    if (directElementStack_.empty() || !directElementStack_.back().startTagOpen) {
        return;
    }
    *directOutputStream_ << '>';
    directElementStack_.back().startTagOpen = false;
}

void XmlWriter::PrepareDirectParentForChild(bool textLikeChild) {
    if (directElementStack_.empty()) {
        return;
    }

    auto& parent = directElementStack_.back();
    EnsureDirectCurrentStartTagClosed();
    if (!textLikeChild && settings_.Indent && !parent.hasTextLikeChild) {
        *directOutputStream_ << settings_.NewLineChars;
        std::string indentation;
        AppendIndent(indentation, static_cast<int>(directElementStack_.size()), settings_);
        *directOutputStream_ << indentation;
    }
    parent.hasAnyChild = true;
    if (textLikeChild) {
        parent.hasTextLikeChild = true;
    } else {
        parent.hasNonTextChild = true;
    }
}

void XmlWriter::WriteDirectAttribute(const std::string& name, const std::string& value) {
    *directOutputStream_ << ' ' << name << "=\"" << EscapeAttribute(value, settings_) << '"';
}

bool XmlWriter::HasDirectNamespaceBinding(const std::string& prefix, const std::string& namespaceUri) const {
    if (namespaceUri.empty()) {
        return prefix.empty();
    }

    if (prefix == "xml") {
        return namespaceUri == "http://www.w3.org/XML/1998/namespace";
    }

    for (auto it = directElementStack_.rbegin(); it != directElementStack_.rend(); ++it) {
        const auto found = it->namespaceDeclarations.find(prefix);
        if (found != it->namespaceDeclarations.end()) {
            return found->second == namespaceUri;
        }
    }

    return false;
}

std::string XmlWriter::LookupDirectNamespacePrefix(const std::string& namespaceUri) const {
    if (namespaceUri.empty()) {
        return {};
    }
    if (namespaceUri == "http://www.w3.org/XML/1998/namespace") {
        return "xml";
    }

    for (auto it = directElementStack_.rbegin(); it != directElementStack_.rend(); ++it) {
        for (const auto& [prefix, uri] : it->namespaceDeclarations) {
            if (uri == namespaceUri) {
                return prefix;
            }
        }
    }

    return {};
}

void XmlWriter::DeclareDirectNamespaceIfNeeded(const std::string& prefix, const std::string& namespaceUri) {
    if (namespaceUri.empty() || directElementStack_.empty() || HasDirectNamespaceBinding(prefix, namespaceUri)) {
        return;
    }

    auto& current = directElementStack_.back();
    const std::string attributeName = prefix.empty() ? "xmlns" : "xmlns:" + prefix;
    WriteDirectAttribute(attributeName, namespaceUri);
    current.namespaceDeclarations[prefix] = namespaceUri;
}

void XmlWriter::WriteDirectTopLevelNode(const XmlNode& node) {
    if (settings_.Conformance == ConformanceLevel::Fragment) {
        WriteDirectTopLevelSeparatorIfNeeded();
        SerializeNodeToStream(node, settings_, *directOutputStream_, 0);
        directHasTopLevelContent_ = true;
        return;
    }

    switch (node.NodeType()) {
    case XmlNodeType::XmlDeclaration:
        if (directHasTopLevelContent_) {
            throw XmlException("XML declaration must be the first node in the document");
        }
        break;
    case XmlNodeType::DocumentType:
        if (directHasDocumentType_) {
            throw XmlException("XML document can only contain a single DOCTYPE declaration");
        }
        if (directHasRootElement_) {
            throw XmlException("DOCTYPE must appear before the root element");
        }
        break;
    case XmlNodeType::Element:
        if (directHasRootElement_) {
            throw XmlException("The XML document already has a root element");
        }
        break;
    default:
        break;
    }

    WriteDirectTopLevelSeparatorIfNeeded();
    SerializeNodeToStream(node, settings_, *directOutputStream_, 0);
    directHasTopLevelContent_ = true;
    if (node.NodeType() == XmlNodeType::DocumentType) {
        directHasDocumentType_ = true;
    } else if (node.NodeType() == XmlNodeType::Element) {
        directHasRootElement_ = true;
    }
}

void XmlWriter::WriteStartDocument(const std::string& version, const std::string& encoding, const std::string& standalone) {
    EnsureDocumentOpen("write the XML declaration");
    EnsureNoOpenAttribute("write the XML declaration");
    if (settings_.Conformance == ConformanceLevel::Fragment) {
        throw XmlException("XML declaration is only allowed in document conformance mode");
    }
    if (UsesDirectOutput()) {
        if (startDocumentWritten_) {
            throw XmlException("XML declaration has already been written");
        }
        if (!directElementStack_.empty()) {
            throw XmlException("Cannot write XML declaration inside an element");
        }
        if (directHasTopLevelContent_) {
            throw XmlException("XML declaration must be the first node in the document");
        }

        const std::string effectiveEncoding = encoding.empty() ? settings_.Encoding : encoding;
        *directOutputStream_ << "<?xml version=\"" << EscapeAttribute(version, settings_) << '\"';
        if (!effectiveEncoding.empty()) {
            *directOutputStream_ << " encoding=\"" << EscapeAttribute(effectiveEncoding, settings_) << '\"';
        }
        if (!standalone.empty()) {
            *directOutputStream_ << " standalone=\"" << EscapeAttribute(standalone, settings_) << '\"';
        }
        *directOutputStream_ << "?>";
        startDocumentWritten_ = true;
        directHasTopLevelContent_ = true;
        return;
    }
    if (document_.Declaration() != nullptr) {
        throw XmlException("XML declaration has already been written");
    }
    if (!elementStack_.empty()) {
        throw XmlException("Cannot write XML declaration inside an element");
    }
    if (!document_.ChildNodes().empty()) {
        throw XmlException("XML declaration must be the first node in the document");
    }

    const std::string effectiveEncoding = encoding.empty() ? settings_.Encoding : encoding;
    document_.AppendChild(document_.CreateXmlDeclaration(version, effectiveEncoding, standalone));
    startDocumentWritten_ = true;
}

void XmlWriter::WriteDocType(
    const std::string& name,
    const std::string& publicId,
    const std::string& systemId,
    const std::string& internalSubset) {
    EnsureDocumentOpen("write a document type declaration");
    EnsureNoOpenAttribute("write a document type declaration");
    if (settings_.Conformance == ConformanceLevel::Fragment) {
        throw XmlException("DOCTYPE is only allowed in document conformance mode");
    }
    if (UsesDirectOutput()) {
        if (!directElementStack_.empty()) {
            throw XmlException("Cannot write DOCTYPE inside an element");
        }
        if (directHasRootElement_) {
            throw XmlException("DOCTYPE must appear before the root element");
        }
        if (directHasDocumentType_) {
            throw XmlException("XML document can only contain a single DOCTYPE declaration");
        }

        WriteDirectTopLevelSeparatorIfNeeded();
        *directOutputStream_ << "<!DOCTYPE " << name;
        if (!publicId.empty()) {
            *directOutputStream_ << " PUBLIC \"" << publicId << "\" \"" << systemId << "\"";
        } else if (!systemId.empty()) {
            *directOutputStream_ << " SYSTEM \"" << systemId << "\"";
        }
        if (!internalSubset.empty()) {
            *directOutputStream_ << " [" << internalSubset << ']';
        }
        *directOutputStream_ << '>';
        directHasDocumentType_ = true;
        directHasTopLevelContent_ = true;
        return;
    }
    if (!elementStack_.empty()) {
        throw XmlException("Cannot write DOCTYPE inside an element");
    }
    if (document_.DocumentElement() != nullptr) {
        throw XmlException("DOCTYPE must appear before the root element");
    }

    document_.AppendChild(document_.CreateDocumentType(name, publicId, systemId, internalSubset));
}

void XmlWriter::WriteStartElement(const std::string& name) {
    EnsureDocumentOpen("write a start element");
    EnsureNoOpenAttribute("write a start element");
    if (UsesDirectOutput()) {
        if (directElementStack_.empty()) {
            if (settings_.Conformance != ConformanceLevel::Fragment && directHasRootElement_) {
                throw XmlException("The XML document already has a root element");
            }
            WriteDirectTopLevelSeparatorIfNeeded();
            if (settings_.Conformance != ConformanceLevel::Fragment) {
                directHasRootElement_ = true;
            }
            directHasTopLevelContent_ = true;
        } else {
            PrepareDirectParentForChild(false);
        }

        directElementStack_.push_back(DirectElementState{});
        directElementStack_.back().name = name;
        *directOutputStream_ << '<' << name;
        return;
    }
    if (settings_.Conformance != ConformanceLevel::Fragment
        && elementStack_.empty() && document_.DocumentElement() != nullptr) {
        throw XmlException("The XML document already has a root element");
    }

    auto element = document_.CreateElement(name);
    if (!elementStack_.empty()) {
        MarkCurrentElementContentStarted();
    }
    if (elementStack_.empty()) {
        AppendDocumentLevelNode(element);
    } else {
        elementStack_.back()->AppendChild(element);
    }
    elementStack_.push_back(element);
    startTagOpenStack_.push_back(true);
}

void XmlWriter::WriteStartElement(const std::string& prefix, const std::string& localName, const std::string& namespaceUri) {
    WriteStartElement(ComposeQualifiedName(prefix, localName));
    if (UsesDirectOutput()) {
        if (!namespaceUri.empty()) {
            DeclareDirectNamespaceIfNeeded(prefix, namespaceUri);
        }
        return;
    }
    if (!namespaceUri.empty()) {
        const std::string xmlnsName = prefix.empty() ? "xmlns" : "xmlns:" + prefix;
        if (!elementStack_.back()->HasAttribute(xmlnsName)) {
            elementStack_.back()->SetAttribute(xmlnsName, namespaceUri);
        }
    }
}

void XmlWriter::WriteStartAttribute(const std::string& name) {
    EnsureOpenStartElement("write an attribute");
    EnsureNoOpenAttribute("write an attribute");
    currentAttributeName_ = name;
    currentAttributeValue_.clear();
}

void XmlWriter::WriteStartAttribute(
    const std::string& prefix,
    const std::string& localName,
    const std::string& namespaceUri) {
    WriteStartAttribute(ComposeQualifiedName(prefix, localName));

    if (UsesDirectOutput()) {
        if (!prefix.empty() && !namespaceUri.empty()) {
            DeclareDirectNamespaceIfNeeded(prefix, namespaceUri);
        }
        return;
    }

    if (!prefix.empty() && !namespaceUri.empty()) {
        const std::string xmlnsName = "xmlns:" + prefix;
        if (!elementStack_.back()->HasAttribute(xmlnsName)) {
            elementStack_.back()->SetAttribute(xmlnsName, namespaceUri);
        }
    }
}

void XmlWriter::WriteEndAttribute() {
    EnsureDocumentOpen("end an attribute");
    if (!currentAttributeName_.has_value()) {
        throw XmlException("Cannot end an attribute when no attribute is open");
    }

    if (UsesDirectOutput()) {
        WriteDirectAttribute(*currentAttributeName_, currentAttributeValue_);
        currentAttributeName_.reset();
        currentAttributeValue_.clear();
        return;
    }

    elementStack_.back()->SetAttribute(*currentAttributeName_, currentAttributeValue_);
    currentAttributeName_.reset();
    currentAttributeValue_.clear();
}

void XmlWriter::WriteAttributeString(const std::string& name, const std::string& value) {
    WriteStartAttribute(name);
    WriteString(value);
    WriteEndAttribute();
}

void XmlWriter::WriteAttributeString(
    const std::string& prefix,
    const std::string& localName,
    const std::string& namespaceUri,
    const std::string& value) {
    WriteStartAttribute(prefix, localName, namespaceUri);
    WriteString(value);
    WriteEndAttribute();
}

void XmlWriter::WriteAttributes(const XmlAttributeCollection& attributes) {
    EnsureOpenStartElement("write attributes");
    EnsureNoOpenAttribute("write attributes");
    for (const auto& attribute : attributes) {
        if (attribute != nullptr) {
            WriteAttributeString(attribute->Name(), attribute->Value());
        }
    }
}

void XmlWriter::WriteAttributes(const XmlElement& element) {
    EnsureOpenStartElement("write attributes");
    EnsureNoOpenAttribute("write attributes");
    for (const auto& attribute : element.attributes_) {
        if (attribute != nullptr) {
            WriteAttributeString(attribute->Name(), attribute->Value());
        }
    }
    for (const auto& pendingAttribute : element.pendingLoadAttributes_) {
        WriteAttributeString(
            std::string(element.PendingLoadAttributeNameView(pendingAttribute)),
            std::string(element.PendingLoadAttributeValueView(pendingAttribute)));
    }
}

void XmlWriter::WriteCharEntity(unsigned int codePoint) {
    EnsureNoOpenAttribute("write a character entity");
    EnsureOpenElement("write a character entity");
    if (codePoint == 0 || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
        throw XmlException("WriteCharEntity requires a valid Unicode scalar value");
    }

    std::ostringstream name;
    name << "#x" << std::uppercase << std::hex << codePoint;
    if (UsesDirectOutput()) {
        PrepareDirectParentForChild(true);
        *directOutputStream_ << '&' << name.str() << ';';
        return;
    }
    MarkCurrentElementContentStarted();
    elementStack_.back()->AppendChild(document_.CreateEntityReference(name.str()));
}

void XmlWriter::WriteEntityRef(const std::string& name) {
    EnsureNoOpenAttribute("write an entity reference");
    EnsureOpenElement("write an entity reference");
    if (name.empty()) {
        throw XmlException("WriteEntityRef requires an entity name");
    }

    if (UsesDirectOutput()) {
        PrepareDirectParentForChild(true);
        *directOutputStream_ << '&' << name << ';';
        return;
    }

    MarkCurrentElementContentStarted();
    elementStack_.back()->AppendChild(document_.CreateEntityReference(name));
}

void XmlWriter::WriteString(const std::string& text) {
    if (currentAttributeName_.has_value()) {
        currentAttributeValue_ += text;
        return;
    }

    EnsureOpenElement("write text");

    if (UsesDirectOutput()) {
        PrepareDirectParentForChild(true);
        *directOutputStream_ << EscapeText(text, settings_);
        return;
    }

    MarkCurrentElementContentStarted();
    elementStack_.back()->AppendChild(document_.CreateTextNode(text));
}

void XmlWriter::WriteValue(const std::string& value) {
    WriteString(value);
}

void XmlWriter::WriteValue(bool value) {
    WriteString(value ? "true" : "false");
}

void XmlWriter::WriteValue(int value) {
    WriteString(std::to_string(value));
}

void XmlWriter::WriteValue(double value) {
    std::ostringstream stream;
    stream << value;
    WriteString(stream.str());
}

void XmlWriter::WriteName(const std::string& name) {
    if (!IsValidXmlQualifiedName(name)) {
        throw XmlException("WriteName requires a valid XML name");
    }
    WriteString(name);
}

void XmlWriter::WriteQualifiedName(const std::string& localName, const std::string& namespaceUri) {
    if (!IsValidXmlNameToken(localName)) {
        throw XmlException("WriteQualifiedName requires a valid XML local name");
    }

    if (namespaceUri.empty()) {
        WriteName(localName);
        return;
    }

    const auto prefix = UsesDirectOutput()
        ? LookupDirectNamespacePrefix(namespaceUri)
        : LookupNamespacePrefix(namespaceUri);
    if (!prefix.empty()) {
        WriteName(ComposeQualifiedName(prefix, localName));
        return;
    }

    if (!UsesDirectOutput() && !elementStack_.empty() && LookupNamespaceUriOnElement(elementStack_.back().get(), {}) == namespaceUri) {
        WriteName(localName);
        return;
    }

    if (UsesDirectOutput() && HasDirectNamespaceBinding({}, namespaceUri)) {
        WriteName(localName);
        return;
    }

    throw XmlException("WriteQualifiedName requires a namespace prefix bound to URI: " + namespaceUri);
}

void XmlWriter::WriteWhitespace(const std::string& whitespace) {
    EnsureDocumentOpen("write whitespace");
    EnsureNoOpenAttribute("write whitespace");
    if (UsesDirectOutput()) {
        if (directElementStack_.empty()) {
            WriteDirectTopLevelSeparatorIfNeeded();
            *directOutputStream_ << EscapeText(whitespace, settings_);
            directHasTopLevelContent_ = true;
        } else {
            PrepareDirectParentForChild(true);
            *directOutputStream_ << EscapeText(whitespace, settings_);
        }
        return;
    }
    auto node = document_.CreateWhitespace(whitespace);
    if (elementStack_.empty()) {
        AppendDocumentLevelNode(node);
    } else {
        MarkCurrentElementContentStarted();
        elementStack_.back()->AppendChild(node);
    }
}

void XmlWriter::WriteSignificantWhitespace(const std::string& whitespace) {
    EnsureDocumentOpen("write significant whitespace");
    EnsureNoOpenAttribute("write significant whitespace");
    if (UsesDirectOutput()) {
        if (directElementStack_.empty()) {
            WriteDirectTopLevelSeparatorIfNeeded();
            *directOutputStream_ << EscapeText(whitespace, settings_);
            directHasTopLevelContent_ = true;
        } else {
            PrepareDirectParentForChild(true);
            *directOutputStream_ << EscapeText(whitespace, settings_);
        }
        return;
    }
    auto node = document_.CreateSignificantWhitespace(whitespace);
    if (elementStack_.empty()) {
        AppendDocumentLevelNode(node);
    } else {
        MarkCurrentElementContentStarted();
        elementStack_.back()->AppendChild(node);
    }
}

void XmlWriter::WriteCData(const std::string& text) {
    EnsureNoOpenAttribute("write CDATA");
    EnsureOpenElement("write CDATA");

    if (UsesDirectOutput()) {
        PrepareDirectParentForChild(true);
        *directOutputStream_ << "<![CDATA[" << text << "]]>";
        return;
    }

    MarkCurrentElementContentStarted();
    elementStack_.back()->AppendChild(document_.CreateCDataSection(text));
}

void XmlWriter::WriteComment(const std::string& text) {
    EnsureDocumentOpen("write a comment");
    EnsureNoOpenAttribute("write a comment");
    if (UsesDirectOutput()) {
        if (directElementStack_.empty()) {
            WriteDirectTopLevelSeparatorIfNeeded();
            *directOutputStream_ << "<!--" << text << "-->";
            directHasTopLevelContent_ = true;
        } else {
            PrepareDirectParentForChild(false);
            *directOutputStream_ << "<!--" << text << "-->";
        }
        return;
    }
    auto comment = document_.CreateComment(text);
    if (elementStack_.empty()) {
        AppendDocumentLevelNode(comment);
    } else {
        MarkCurrentElementContentStarted();
        elementStack_.back()->AppendChild(comment);
    }
}

void XmlWriter::WriteProcessingInstruction(const std::string& name, const std::string& text) {
    EnsureDocumentOpen("write a processing instruction");
    EnsureNoOpenAttribute("write a processing instruction");
    if (UsesDirectOutput()) {
        if (directElementStack_.empty()) {
            WriteDirectTopLevelSeparatorIfNeeded();
            *directOutputStream_ << "<?" << name;
            if (!text.empty()) {
                *directOutputStream_ << ' ' << text;
            }
            *directOutputStream_ << "?>";
            directHasTopLevelContent_ = true;
        } else {
            PrepareDirectParentForChild(false);
            *directOutputStream_ << "<?" << name;
            if (!text.empty()) {
                *directOutputStream_ << ' ' << text;
            }
            *directOutputStream_ << "?>";
        }
        return;
    }
    auto instruction = document_.CreateProcessingInstruction(name, text);
    if (elementStack_.empty()) {
        AppendDocumentLevelNode(instruction);
    } else {
        MarkCurrentElementContentStarted();
        elementStack_.back()->AppendChild(instruction);
    }
}

void XmlWriter::WriteRaw(const std::string& xml) {
    EnsureDocumentOpen("write raw XML");
    EnsureNoOpenAttribute("write raw XML");
    if (xml.empty()) {
        return;
    }

    if (UsesDirectOutput()) {
        if (!directElementStack_.empty()) {
            XmlDocument fragmentDocument;
            auto fragment = fragmentDocument.CreateDocumentFragment();
            fragment->SetInnerXml(xml);
            for (const auto& child : fragment->ChildNodes()) {
                if (child != nullptr) {
                    WriteNode(*child);
                }
            }
            return;
        }

        try {
            XmlDocument scratch;
            scratch.LoadXml(xml);
            for (const auto& child : scratch.ChildNodes()) {
                if (child != nullptr) {
                    WriteNode(*child);
                }
            }
            return;
        } catch (const XmlException&) {
            XmlDocument fragmentDocument;
            auto fragment = fragmentDocument.CreateDocumentFragment();
            fragment->SetInnerXml(xml);
            for (const auto& child : fragment->ChildNodes()) {
                if (child != nullptr) {
                    WriteNode(*child);
                }
            }
            return;
        }
    }

    if (!elementStack_.empty()) {
        auto fragment = document_.CreateDocumentFragment();
        fragment->SetInnerXml(xml);
        MarkCurrentElementContentStarted();
        elementStack_.back()->AppendChild(fragment);
        return;
    }

    try {
        XmlDocument scratch;
        scratch.SetPreserveWhitespace(document_.PreserveWhitespace());
        scratch.LoadXml(xml);
        for (const auto& child : scratch.ChildNodes()) {
            if (child != nullptr) {
                AppendDocumentLevelNode(document_.ImportNode(*child, true));
            }
        }
        return;
    } catch (const XmlException&) {
        auto fragment = document_.CreateDocumentFragment();
        fragment->SetInnerXml(xml);
        AppendDocumentLevelNode(fragment);
    }
}

void XmlWriter::WriteBase64(const unsigned char* data, std::size_t length) {
    EnsureDocumentOpen("write base64 content");
    EnsureNoOpenAttribute("write base64 content");
    if (data == nullptr && length != 0) {
        throw XmlException("WriteBase64 requires a non-null buffer when length is non-zero");
    }
    WriteString(EncodeBase64(data, length));
}

void XmlWriter::WriteElementString(const std::string& name, const std::string& value) {
    EnsureDocumentOpen("write an element string");
    EnsureNoOpenAttribute("write an element string");
    WriteStartElement(name);
    WriteString(value);
    WriteEndElement();
}

void XmlWriter::WriteElementString(const std::string& prefix, const std::string& localName, const std::string& namespaceUri, const std::string& value) {
    EnsureDocumentOpen("write an element string");
    EnsureNoOpenAttribute("write an element string");
    WriteStartElement(prefix, localName, namespaceUri);
    WriteString(value);
    WriteEndElement();
}

void XmlWriter::WriteNode(const XmlNode& node) {
    EnsureDocumentOpen("write a node");
    EnsureNoOpenAttribute("write a node");

    if (node.NodeType() == XmlNodeType::Document || node.NodeType() == XmlNodeType::DocumentFragment) {
        for (const auto& child : node.ChildNodes()) {
            if (child != nullptr) {
                WriteNode(*child);
            }
        }
        return;
    }

    if (node.NodeType() == XmlNodeType::Attribute) {
        EnsureOpenStartElement("write an attribute node");
        if (UsesDirectOutput()) {
            WriteAttributeString(node.Name(), node.Value());
            return;
        }
        elementStack_.back()->SetAttribute(node.Name(), node.Value());
        return;
    }

    if (UsesDirectOutput()) {
        if (directElementStack_.empty()) {
            if (node.NodeType() == XmlNodeType::XmlDeclaration || node.NodeType() == XmlNodeType::DocumentType || node.NodeType() == XmlNodeType::Element
                || node.NodeType() == XmlNodeType::Comment || node.NodeType() == XmlNodeType::ProcessingInstruction
                || node.NodeType() == XmlNodeType::Whitespace || node.NodeType() == XmlNodeType::SignificantWhitespace
                || node.NodeType() == XmlNodeType::Text || node.NodeType() == XmlNodeType::CDATA || node.NodeType() == XmlNodeType::EntityReference) {
                WriteDirectTopLevelNode(node);
                return;
            }
            throw XmlException("Unsupported node type for serialization");
        }

        if (node.NodeType() == XmlNodeType::XmlDeclaration || node.NodeType() == XmlNodeType::DocumentType) {
            throw XmlException("Cannot write document-level nodes inside an element");
        }

        const bool textLike = node.NodeType() == XmlNodeType::Text
            || node.NodeType() == XmlNodeType::Whitespace
            || node.NodeType() == XmlNodeType::SignificantWhitespace
            || node.NodeType() == XmlNodeType::CDATA
            || node.NodeType() == XmlNodeType::EntityReference;
        PrepareDirectParentForChild(textLike);
        SerializeNodeToStream(node, settings_, *directOutputStream_, static_cast<int>(directElementStack_.size()));
        return;
    }

    auto imported = document_.ImportNode(node, true);
    if (elementStack_.empty()) {
        AppendDocumentLevelNode(imported);
        return;
    }

    if (node.NodeType() == XmlNodeType::XmlDeclaration || node.NodeType() == XmlNodeType::DocumentType) {
        throw XmlException("Cannot write document-level nodes inside an element");
    }

    MarkCurrentElementContentStarted();
    elementStack_.back()->AppendChild(imported);
}

void XmlWriter::WriteNode(XmlReader& reader, bool defattr) {
    EnsureDocumentOpen("write a node");
    EnsureNoOpenAttribute("write a node");

    if (reader.GetReadState() == ReadState::Initial) {
        reader.Read();
    }

    int startDepth = reader.Depth();
    do {
        switch (reader.NodeType()) {
        case XmlNodeType::Element: {
            WriteStartElement(reader.Name());
            if (defattr && reader.MoveToFirstAttribute()) {
                do {
                    WriteAttributeString(reader.Name(), reader.Value());
                } while (reader.MoveToNextAttribute());
                reader.MoveToElement();
            }
            if (reader.IsEmptyElement()) {
                WriteEndElement();
            }
            break;
        }
        case XmlNodeType::Text:
            WriteString(reader.Value());
            break;
        case XmlNodeType::CDATA:
            WriteCData(reader.Value());
            break;
        case XmlNodeType::Comment:
            WriteComment(reader.Value());
            break;
        case XmlNodeType::ProcessingInstruction:
            WriteProcessingInstruction(reader.Name(), reader.Value());
            break;
        case XmlNodeType::Whitespace:
            WriteWhitespace(reader.Value());
            break;
        case XmlNodeType::SignificantWhitespace:
            WriteSignificantWhitespace(reader.Value());
            break;
        case XmlNodeType::EntityReference:
            WriteEntityRef(reader.Name());
            break;
        case XmlNodeType::EndElement:
            WriteFullEndElement();
            break;
        case XmlNodeType::XmlDeclaration: {
            std::string version = reader.GetAttribute("version");
            std::string encoding = reader.GetAttribute("encoding");
            std::string standalone = reader.GetAttribute("standalone");
            WriteStartDocument(
                version.empty() ? "1.0" : version,
                encoding,
                standalone);
            break;
        }
        case XmlNodeType::DocumentType:
            // DocumentType cannot be written via WriteNode from reader inside elements
            break;
        default:
            break;
        }
    } while (reader.Read() && reader.Depth() > startDepth);

    // If we stopped at the EndElement that matches startDepth, close the element
    if (reader.NodeType() == XmlNodeType::EndElement && reader.Depth() == startDepth) {
        WriteFullEndElement();
        reader.Read(); // advance past the end element
    }
}

void XmlWriter::AppendDocumentLevelNode(const std::shared_ptr<XmlNode>& node) {
    if (node == nullptr) {
        return;
    }

    if (node->NodeType() == XmlNodeType::DocumentFragment) {
        for (const auto& child : node->ChildNodes()) {
            if (child != nullptr) {
                AppendDocumentLevelNode(document_.ImportNode(*child, true));
            }
        }
        return;
    }

    if (settings_.Conformance == ConformanceLevel::Fragment) {
        if (node->NodeType() == XmlNodeType::XmlDeclaration) {
            throw XmlException("XML declaration is only allowed in document conformance mode");
        }
        if (node->NodeType() == XmlNodeType::DocumentType) {
            throw XmlException("DOCTYPE is only allowed in document conformance mode");
        }
        fragmentRoot_->AppendChild(node);
        return;
    }

    if (node->NodeType() == XmlNodeType::XmlDeclaration && !document_.ChildNodes().empty()) {
        throw XmlException("XML declaration must be the first node in the document");
    }

    if (node->NodeType() == XmlNodeType::DocumentType && document_.DocumentElement() != nullptr) {
        throw XmlException("DOCTYPE must appear before the root element");
    }

    document_.AppendChild(node);
}

void XmlWriter::WriteEndElement() {
    EnsureNoOpenAttribute("close an element");
    EnsureOpenElement("close an element");

    if (UsesDirectOutput()) {
        auto& current = directElementStack_.back();
        if (current.startTagOpen && !current.forceFullEnd) {
            *directOutputStream_ << "/>";
            directElementStack_.pop_back();
            return;
        }

        if (current.startTagOpen) {
            *directOutputStream_ << '>';
            current.startTagOpen = false;
        }
        if (current.hasNonTextChild && !current.hasTextLikeChild && settings_.Indent) {
            *directOutputStream_ << settings_.NewLineChars;
            std::string indentation;
            AppendIndent(indentation, static_cast<int>(directElementStack_.size()) - 1, settings_);
            *directOutputStream_ << indentation;
        }
        *directOutputStream_ << "</" << current.name << ">";
        directElementStack_.pop_back();
        return;
    }

    elementStack_.pop_back();
    startTagOpenStack_.pop_back();
}

void XmlWriter::WriteFullEndElement() {
    EnsureNoOpenAttribute("close an element");
    EnsureOpenElement("close an element");

    if (UsesDirectOutput()) {
        directElementStack_.back().forceFullEnd = true;
        WriteEndElement();
        return;
    }

    elementStack_.back()->writeFullEndElement_ = true;
    elementStack_.pop_back();
    startTagOpenStack_.pop_back();
}

void XmlWriter::WriteEndDocument() {
    EnsureDocumentOpen("end the document");
    EnsureNoOpenAttribute("end the document");
    while (UsesDirectOutput() ? !directElementStack_.empty() : !elementStack_.empty()) {
        WriteEndElement();
    }
    documentClosed_ = true;
    if (UsesDirectOutput()) {
        directOutputStream_->flush();
    }
}

void XmlWriter::Flush() const {
    EnsureDocumentOpen("flush the writer");
    EnsureNoOpenAttribute("flush the writer");
    if (UsesDirectOutput()) {
        directOutputStream_->flush();
    }
}

void XmlWriter::Close() {
    EnsureDocumentOpen("close the writer");
    EnsureNoOpenAttribute("close the writer");
    WriteEndDocument();
}

std::string XmlWriter::GetString() const {
    EnsureOutputReady();
    if (UsesDirectOutput()) {
        const auto* stringStream = dynamic_cast<const std::ostringstream*>(directOutputStream_);
        if (stringStream == nullptr) {
            throw XmlException("GetString is only available for in-memory writers");
        }
        return stringStream->str();
    }
    return settings_.Conformance == ConformanceLevel::Fragment
        ? fragmentRoot_->InnerXml(settings_)
        : document_.ToString(settings_);
}

void XmlWriter::Save(std::ostream& stream) const {
    EnsureOutputReady();
    if (UsesDirectOutput()) {
        if (directOutputStream_ != &stream) {
            throw XmlException("Save is not available for stream-backed writers bound to a different output stream");
        }
        directOutputStream_->flush();
        return;
    }
    if (settings_.Conformance == ConformanceLevel::Fragment) {
        WriteToStream(*fragmentRoot_, stream, settings_);
        return;
    }
    WriteToStream(document_, stream, settings_);
}

void XmlWriter::Save(const std::string& path) const {
    EnsureOutputReady();
    if (UsesDirectOutput()) {
        throw XmlException("Save(path) is not available for stream-backed writers");
    }
    std::ofstream stream(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw XmlException("Failed to write XML file: " + path);
    }
    Save(stream);
}

std::string XmlWriter::WriteToString(const XmlNode& node, const XmlWriterSettings& settings) {
    std::string output;
    SerializeNode(node, settings, output, 0);
    return output;
}

void XmlWriter::WriteToStream(const XmlNode& node, std::ostream& stream, const XmlWriterSettings& settings) {
    SerializeNodeToStream(node, settings, stream, 0);
}

void XmlWriter::WriteToFile(const XmlNode& node, const std::string& path, const XmlWriterSettings& settings) {
    std::ofstream stream(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw XmlException("Failed to write XML file: " + path);
    }

    WriteToStream(node, stream, settings);
}

// ============================================================================
// XmlConvert
// ============================================================================

std::string XmlConvert::EncodeName(const std::string& name) {
    if (name.empty()) {
        return {};
    }

    std::string encoded;
    encoded.reserve(name.size());

    for (std::size_t index = 0; index < name.size(); ++index) {
        const char ch = name[index];
        if (index == 0 && !IsStartNameChar(ch)) {
            std::ostringstream hex;
            hex << "_x" << std::uppercase << std::setfill('0') << std::setw(4)
                << std::hex << static_cast<unsigned>(static_cast<unsigned char>(ch)) << "_";
            encoded += hex.str();
        } else if (!IsNameChar(ch)) {
            std::ostringstream hex;
            hex << "_x" << std::uppercase << std::setfill('0') << std::setw(4)
                << std::hex << static_cast<unsigned>(static_cast<unsigned char>(ch)) << "_";
            encoded += hex.str();
        } else {
            encoded.push_back(ch);
        }
    }

    return encoded;
}

std::string XmlConvert::DecodeName(const std::string& name) {
    if (name.empty()) {
        return {};
    }

    std::string decoded;
    decoded.reserve(name.size());

    for (std::size_t index = 0; index < name.size(); ++index) {
        if (index + 6 < name.size() && name[index] == '_' && name[index + 1] == 'x'
            && name[index + 6] == '_') {
            const std::string hex = name.substr(index + 2, 4);
            bool validHex = hex.size() == 4;
            for (char h : hex) {
                if (!std::isxdigit(static_cast<unsigned char>(h))) {
                    validHex = false;
                    break;
                }
            }
            if (validHex) {
                unsigned int charCode = 0;
                for (char h : hex) {
                    charCode <<= 4;
                    if (h >= '0' && h <= '9') {
                        charCode |= static_cast<unsigned int>(h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        charCode |= static_cast<unsigned int>(10 + h - 'a');
                    } else if (h >= 'A' && h <= 'F') {
                        charCode |= static_cast<unsigned int>(10 + h - 'A');
                    }
                }
                decoded.push_back(static_cast<char>(charCode));
                index += 6;
                continue;
            }
        }
        decoded.push_back(name[index]);
    }

    return decoded;
}

std::string XmlConvert::EncodeLocalName(const std::string& name) {
    if (name.empty()) {
        return {};
    }

    std::string encoded;
    encoded.reserve(name.size());

    for (std::size_t index = 0; index < name.size(); ++index) {
        const char ch = name[index];
        if (ch == ':') {
            encoded += "_x003A_";
        } else if (index == 0 && !IsStartNameChar(ch)) {
            std::ostringstream hex;
            hex << "_x" << std::uppercase << std::setfill('0') << std::setw(4)
                << std::hex << static_cast<unsigned>(static_cast<unsigned char>(ch)) << "_";
            encoded += hex.str();
        } else if (!IsNameChar(ch)) {
            std::ostringstream hex;
            hex << "_x" << std::uppercase << std::setfill('0') << std::setw(4)
                << std::hex << static_cast<unsigned>(static_cast<unsigned char>(ch)) << "_";
            encoded += hex.str();
        } else {
            encoded.push_back(ch);
        }
    }

    return encoded;
}

std::string XmlConvert::EncodeNmToken(const std::string& name) {
    if (name.empty()) {
        return {};
    }

    std::string encoded;
    encoded.reserve(name.size());

    for (const char ch : name) {
        if (!IsNameChar(ch)) {
            std::ostringstream hex;
            hex << "_x" << std::uppercase << std::setfill('0') << std::setw(4)
                << std::hex << static_cast<unsigned>(static_cast<unsigned char>(ch)) << "_";
            encoded += hex.str();
        } else {
            encoded.push_back(ch);
        }
    }

    return encoded;
}

bool XmlConvert::IsXmlChar(char ch) {
    const auto uch = static_cast<unsigned char>(ch);
    return uch == 0x09 || uch == 0x0A || uch == 0x0D || uch >= 0x20;
}

bool XmlConvert::IsStartNameChar(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == ':';
}

bool XmlConvert::IsNCNameStartChar(char ch) {
    // NCName start char = letter or '_' (no colon)
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool XmlConvert::IsNameChar(char ch) {
    return IsStartNameChar(ch) || std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '-' || ch == '.';
}

std::string XmlConvert::VerifyName(const std::string& name) {
    if (name.empty()) {
        throw XmlException("The empty string is not a valid XML name");
    }

    if (!IsStartNameChar(name[0])) {
        throw XmlException("'" + name + "' is not a valid XML name");
    }

    for (std::size_t index = 1; index < name.size(); ++index) {
        if (!IsNameChar(name[index])) {
            throw XmlException("'" + name + "' is not a valid XML name");
        }
    }

    return name;
}

bool XmlConvert::IsNCNameChar(char ch) {
    // NCNameChar = NameChar minus ':'
    return IsNameChar(ch) && ch != ':';
}

bool XmlConvert::IsWhitespaceChar(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

std::string XmlConvert::VerifyNCName(const std::string& name) {
    if (name.empty()) {
        throw XmlException("The empty string is not a valid NCName");
    }
    // First char: letter or underscore (no colon, no digit)
    const char first = name[0];
    if (!std::isalpha(static_cast<unsigned char>(first)) && first != '_') {
        throw XmlException("'" + name + "' is not a valid NCName");
    }
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (!IsNCNameChar(name[i])) {
            throw XmlException("'" + name + "' is not a valid NCName");
        }
    }
    return name;
}

std::string XmlConvert::VerifyNmToken(const std::string& name) {
    if (name.empty()) {
        throw XmlException("The empty string is not a valid NMTOKEN");
    }

    for (const char ch : name) {
        if (!IsNameChar(ch)) {
            throw XmlException("'" + name + "' is not a valid NMTOKEN");
        }
    }

    return name;
}

std::string XmlConvert::VerifyXmlChars(const std::string& content) {
    for (std::size_t index = 0; index < content.size(); ++index) {
        if (!IsXmlChar(content[index])) {
            throw XmlException(
                "Invalid XML character at position " + std::to_string(index));
        }
    }
    return content;
}

std::string XmlConvert::ToString(bool value) {
    return value ? "true" : "false";
}

std::string XmlConvert::ToString(int value) {
    return std::to_string(value);
}

std::string XmlConvert::ToString(long long value) {
    return std::to_string(value);
}

std::string XmlConvert::ToString(double value) {
    if (std::isnan(value)) {
        return "NaN";
    }
    if (std::isinf(value)) {
        return value > 0 ? "INF" : "-INF";
    }

    std::ostringstream stream;
    stream << std::setprecision(17) << value;
    return stream.str();
}

std::string XmlConvert::ToString(float value) {
    if (std::isnan(value)) {
        return "NaN";
    }
    if (std::isinf(value)) {
        return value > 0 ? "INF" : "-INF";
    }

    std::ostringstream stream;
    stream << std::setprecision(9) << value;
    return stream.str();
}

bool XmlConvert::ToBoolean(const std::string& value) {
    if (value == "true" || value == "1") {
        return true;
    }
    if (value == "false" || value == "0") {
        return false;
    }
    throw XmlException("'" + value + "' is not a valid boolean value");
}

int XmlConvert::ToInt32(const std::string& value) {
    try {
        std::size_t pos = 0;
        const int result = std::stoi(value, &pos);
        if (pos != value.size()) {
            throw XmlException("'" + value + "' is not a valid Int32 value");
        }
        return result;
    } catch (const std::invalid_argument&) {
        throw XmlException("'" + value + "' is not a valid Int32 value");
    } catch (const std::out_of_range&) {
        throw XmlException("'" + value + "' is out of range for Int32");
    }
}

long long XmlConvert::ToInt64(const std::string& value) {
    try {
        std::size_t pos = 0;
        const long long result = std::stoll(value, &pos);
        if (pos != value.size()) {
            throw XmlException("'" + value + "' is not a valid Int64 value");
        }
        return result;
    } catch (const std::invalid_argument&) {
        throw XmlException("'" + value + "' is not a valid Int64 value");
    } catch (const std::out_of_range&) {
        throw XmlException("'" + value + "' is out of range for Int64");
    }
}

double XmlConvert::ToDouble(const std::string& value) {
    if (value == "NaN") {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (value == "INF" || value == "Infinity") {
        return std::numeric_limits<double>::infinity();
    }
    if (value == "-INF" || value == "-Infinity") {
        return -std::numeric_limits<double>::infinity();
    }
    try {
        std::size_t pos = 0;
        const double result = std::stod(value, &pos);
        if (pos != value.size()) {
            throw XmlException("'" + value + "' is not a valid Double value");
        }
        return result;
    } catch (const std::invalid_argument&) {
        throw XmlException("'" + value + "' is not a valid Double value");
    } catch (const std::out_of_range&) {
        throw XmlException("'" + value + "' is out of range for Double");
    }
}

float XmlConvert::ToSingle(const std::string& value) {
    if (value == "NaN") {
        return std::numeric_limits<float>::quiet_NaN();
    }
    if (value == "INF" || value == "Infinity") {
        return std::numeric_limits<float>::infinity();
    }
    if (value == "-INF" || value == "-Infinity") {
        return -std::numeric_limits<float>::infinity();
    }
    try {
        std::size_t pos = 0;
        const float result = std::stof(value, &pos);
        if (pos != value.size()) {
            throw XmlException("'" + value + "' is not a valid Single value");
        }
        return result;
    } catch (const std::invalid_argument&) {
        throw XmlException("'" + value + "' is not a valid Single value");
    } catch (const std::out_of_range&) {
        throw XmlException("'" + value + "' is out of range for Single");
    }
}

void XmlSchemaSet::AddXml(const std::string& xml) {
    static const std::string kSchemaNamespace = "http://www.w3.org/2001/XMLSchema";

    const auto document = XmlDocument::Parse(xml);
    const auto root = document->DocumentElement();
    if (root == nullptr || root->LocalName() != "schema" || root->NamespaceURI() != kSchemaNamespace) {
        throw XmlException("XmlSchemaSet::AddXml requires an XML Schema document");
    }

    const std::string targetNamespace = root->GetAttribute("targetNamespace");

    const auto makeQualifiedRuleKey = [](const std::string& localName, const std::string& namespaceUri) {
        return namespaceUri + "\n" + localName;
    };

    std::unordered_map<std::string, const XmlElement*> declaredSimpleTypes;
    std::unordered_map<std::string, const XmlElement*> declaredComplexTypes;
    std::unordered_map<std::string, const XmlElement*> declaredElements;
    std::unordered_map<std::string, const XmlElement*> declaredGroups;
    std::unordered_map<std::string, const XmlElement*> declaredAttributes;
    std::unordered_map<std::string, const XmlElement*> declaredAttributeGroups;
    std::unordered_set<std::string> resolvingSimpleTypes;
    std::unordered_set<std::string> resolvingComplexTypes;
    std::unordered_set<std::string> resolvingElements;
    std::unordered_set<std::string> resolvedLocalSimpleTypes;
    std::unordered_set<std::string> resolvedLocalComplexTypes;
    std::unordered_set<std::string> resolvedLocalElements;
    std::unordered_set<std::string> resolvingGroups;
    std::unordered_set<std::string> resolvingAttributes;
    std::unordered_set<std::string> resolvingAttributeGroups;
    std::unordered_map<std::string, Particle> resolvedGroups;
    std::unordered_map<std::string, AttributeUse> resolvedAttributes;
    std::unordered_map<std::string, std::vector<AttributeUse>> resolvedAttributeGroups;
    std::unordered_map<std::string, bool> resolvedAttributeGroupAnyAllowed;
    std::unordered_map<std::string, std::string> resolvedAttributeGroupAnyNamespace;
    std::unordered_map<std::string, std::string> resolvedAttributeGroupAnyProcessContents;

    for (const auto& child : root->ChildNodes()) {
        if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
            continue;
        }
        const auto* childElement = static_cast<const XmlElement*>(child.get());
        if (childElement->NamespaceURI() != kSchemaNamespace) {
            continue;
        }

        if (childElement->LocalName() == "simpleType") {
            const std::string name = childElement->GetAttribute("name");
            if (name.empty()) {
                throw XmlException("Top-level XML Schema simpleType declarations require a name");
            }
            declaredSimpleTypes.emplace(makeQualifiedRuleKey(name, targetNamespace), childElement);
        } else if (childElement->LocalName() == "complexType") {
            const std::string name = childElement->GetAttribute("name");
            if (name.empty()) {
                throw XmlException("Top-level XML Schema complexType declarations require a name");
            }
            declaredComplexTypes.emplace(makeQualifiedRuleKey(name, targetNamespace), childElement);
        } else if (childElement->LocalName() == "element") {
            const std::string name = childElement->GetAttribute("name");
            if (name.empty()) {
                throw XmlException("Top-level XML Schema element declarations require a name");
            }
            declaredElements.emplace(makeQualifiedRuleKey(name, targetNamespace), childElement);
        } else if (childElement->LocalName() == "group") {
            const std::string name = childElement->GetAttribute("name");
            if (name.empty()) {
                throw XmlException("Top-level XML Schema group declarations require a name");
            }
            declaredGroups.emplace(makeQualifiedRuleKey(name, targetNamespace), childElement);
        } else if (childElement->LocalName() == "attribute") {
            const std::string name = childElement->GetAttribute("name");
            if (name.empty()) {
                throw XmlException("Top-level XML Schema attribute declarations require a name");
            }
            declaredAttributes.emplace(makeQualifiedRuleKey(name, targetNamespace), childElement);
        } else if (childElement->LocalName() == "attributeGroup") {
            const std::string name = childElement->GetAttribute("name");
            if (name.empty()) {
                throw XmlException("Top-level XML Schema attributeGroup declarations require a name");
            }
            declaredAttributeGroups.emplace(makeQualifiedRuleKey(name, targetNamespace), childElement);
        }
    }

    auto parseOccurs = [](const std::string& value, std::size_t defaultValue) {
        if (value.empty()) {
            return defaultValue;
        }
        if (value == "unbounded") {
            return std::numeric_limits<std::size_t>::max();
        }
        return static_cast<std::size_t>(std::stoull(value));
    };

    const auto parseProcessContentsValue = [](const XmlElement& element) {
        std::string_view processContents;
        if (!TryGetAttributeValueViewInternal(element, "processContents", processContents) || processContents.empty()) {
            return std::string("strict");
        }
        if (processContents == "strict" || processContents == "lax" || processContents == "skip") {
            return std::string(processContents);
        }

        throw XmlException("XML Schema wildcard processContents must be one of strict, lax, or skip");
    };

    const auto parseSchemaBooleanAttribute = [](const XmlElement& element, const std::string& attributeName) {
        std::string_view value;
        if (!TryGetAttributeValueViewInternal(element, attributeName, value) || value.empty()) {
            return false;
        }
        if (value == "true" || value == "1") {
            return true;
        }
        if (value == "false" || value == "0") {
            return false;
        }

        throw XmlException("XML Schema attribute '" + attributeName + "' must be one of true, false, 1, or 0");
    };

    const auto parseFormValue = [](const std::string& value, const std::string& label) {
        if (value.empty()) {
            return value;
        }
        if (value == "qualified" || value == "unqualified") {
            return value;
        }

        throw XmlException("XML Schema " + label + " must be 'qualified' or 'unqualified'");
    };

    const auto parseAttributeUseValue = [](const XmlElement& element) {
        std::string_view use;
        if (!TryGetAttributeValueViewInternal(element, "use", use) || use.empty() || use == "optional") {
            return std::string("optional");
        }
        if (use == "required") {
            return std::string("required");
        }

        throw XmlException("XML Schema attribute use must be 'optional' or 'required'");
    };

    const auto parseAnnotation = [&](const XmlElement& annotatedElement) {
        XmlSchemaSet::Annotation annotation;
        for (const auto& child : annotatedElement.ChildNodes()) {
            if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                continue;
            }
            const auto* childElement = static_cast<const XmlElement*>(child.get());
            if (childElement->NamespaceURI() != kSchemaNamespace || childElement->LocalName() != "annotation") {
                continue;
            }

            for (const auto& annotationChild : childElement->ChildNodes()) {
                if (annotationChild == nullptr || annotationChild->NodeType() != XmlNodeType::Element) {
                    continue;
                }
                const auto* annotationChildElement = static_cast<const XmlElement*>(annotationChild.get());
                if (annotationChildElement->NamespaceURI() != kSchemaNamespace) {
                    continue;
                }

                XmlSchemaSet::AnnotationEntry entry;
                entry.Source = annotationChildElement->GetAttribute("source");
                entry.Language = annotationChildElement->GetAttribute("xml:lang");
                entry.Content = annotationChildElement->InnerXml();

                if (annotationChildElement->LocalName() == "appinfo") {
                    annotation.AppInfo.push_back(std::move(entry));
                } else if (annotationChildElement->LocalName() == "documentation") {
                    annotation.Documentation.push_back(std::move(entry));
                }
            }
        }
        return annotation;
    };

    const std::string schemaFinalDefault = root->GetAttribute("finalDefault");
    const std::string schemaBlockDefault = root->GetAttribute("blockDefault");
    const std::string schemaElementFormDefault = parseFormValue(root->GetAttribute("elementFormDefault"), "elementFormDefault");
    const std::string schemaAttributeFormDefault = parseFormValue(root->GetAttribute("attributeFormDefault"), "attributeFormDefault");

    const auto validateParticleOccursRange = [](std::size_t minOccurs, std::size_t maxOccurs) {
        if (maxOccurs != std::numeric_limits<std::size_t>::max() && minOccurs > maxOccurs) {
            throw XmlException("XML Schema particles cannot declare minOccurs greater than maxOccurs");
        }
    };

    const auto upsertRule = [this](ElementRule rule) {
        auto found = std::find_if(elements_.begin(), elements_.end(), [&](const auto& existing) {
            return existing.name == rule.name && existing.namespaceUri == rule.namespaceUri;
        });
        if (found == elements_.end()) {
            elements_.push_back(std::move(rule));
        } else {
            *found = std::move(rule);
        }
    };

    const auto upsertSimpleType = [this](NamedSimpleTypeRule typeRule) {
        auto found = std::find_if(simpleTypes_.begin(), simpleTypes_.end(), [&](const auto& existing) {
            return existing.name == typeRule.name && existing.namespaceUri == typeRule.namespaceUri;
        });
        if (found == simpleTypes_.end()) {
            simpleTypes_.push_back(std::move(typeRule));
        } else {
            *found = std::move(typeRule);
        }
    };

    const auto upsertComplexType = [this](NamedComplexTypeRule typeRule) {
        auto found = std::find_if(complexTypes_.begin(), complexTypes_.end(), [&](const auto& existing) {
            return existing.name == typeRule.name && existing.namespaceUri == typeRule.namespaceUri;
        });
        if (found == complexTypes_.end()) {
            complexTypes_.push_back(std::move(typeRule));
        } else {
            *found = std::move(typeRule);
        }
    };

    const auto upsertAttribute = [this](NamedAttributeRule attributeRule) {
        auto found = std::find_if(attributes_.begin(), attributes_.end(), [&](const auto& existing) {
            return existing.name == attributeRule.name && existing.namespaceUri == attributeRule.namespaceUri;
        });
        if (found == attributes_.end()) {
            attributes_.push_back(std::move(attributeRule));
        } else {
            *found = std::move(attributeRule);
        }
    };

    const auto upsertGroup = [this](NamedGroupRule groupRule) {
        auto found = std::find_if(groupRules_.begin(), groupRules_.end(), [&](const auto& existing) {
            return existing.name == groupRule.name && existing.namespaceUri == groupRule.namespaceUri;
        });
        if (found == groupRules_.end()) {
            groupRules_.push_back(std::move(groupRule));
        } else {
            *found = std::move(groupRule);
        }
    };

    const auto upsertAttributeGroup = [this](NamedAttributeGroupRule attributeGroupRule) {
        auto found = std::find_if(attributeGroupRules_.begin(), attributeGroupRules_.end(), [&](const auto& existing) {
            return existing.name == attributeGroupRule.name && existing.namespaceUri == attributeGroupRule.namespaceUri;
        });
        if (found == attributeGroupRules_.end()) {
            attributeGroupRules_.push_back(std::move(attributeGroupRule));
        } else {
            *found = std::move(attributeGroupRule);
        }
    };

    const auto upsertSchemaAnnotation = [this](NamedSchemaAnnotation schemaAnnotation) {
        auto found = std::find_if(schemaAnnotations_.begin(), schemaAnnotations_.end(), [&](const auto& existing) {
            return existing.namespaceUri == schemaAnnotation.namespaceUri;
        });
        if (found == schemaAnnotations_.end()) {
            schemaAnnotations_.push_back(std::move(schemaAnnotation));
        } else {
            *found = std::move(schemaAnnotation);
        }
    };

    const auto upsertGroupAnnotation = [this](NamedGroupAnnotation groupAnnotation) {
        auto found = std::find_if(groups_.begin(), groups_.end(), [&](const auto& existing) {
            return existing.name == groupAnnotation.name && existing.namespaceUri == groupAnnotation.namespaceUri;
        });
        if (found == groups_.end()) {
            groups_.push_back(std::move(groupAnnotation));
        } else {
            *found = std::move(groupAnnotation);
        }
    };

    const auto upsertAttributeGroupAnnotation = [this](NamedAttributeGroupAnnotation groupAnnotation) {
        auto found = std::find_if(attributeGroups_.begin(), attributeGroups_.end(), [&](const auto& existing) {
            return existing.name == groupAnnotation.name && existing.namespaceUri == groupAnnotation.namespaceUri;
        });
        if (found == attributeGroups_.end()) {
            attributeGroups_.push_back(std::move(groupAnnotation));
        } else {
            *found = std::move(groupAnnotation);
        }
    };

    const auto upsertIdentityConstraint = [this](ElementRule::IdentityConstraint constraint) {
        auto found = std::find_if(identityConstraints_.begin(), identityConstraints_.end(), [&](const auto& existing) {
            return existing.name == constraint.name && existing.namespaceUri == constraint.namespaceUri;
        });
        if (found != identityConstraints_.end()) {
            throw XmlException("XML Schema identity constraint '" + constraint.name + "' is declared more than once");
        }
        identityConstraints_.push_back(std::move(constraint));
    };

    const auto resolveTypeName = [&](const XmlElement& context, const std::string& qualifiedName) {
        std::pair<std::string, std::string> resolved;
        if (qualifiedName.empty()) {
            return resolved;
        }

        const auto separator = qualifiedName.find(':');
        if (separator == std::string::npos) {
            resolved.first = qualifiedName;
            resolved.second = targetNamespace;
            return resolved;
        }

        const std::string prefix = qualifiedName.substr(0, separator);
        resolved.first = qualifiedName.substr(separator + 1);
        resolved.second = context.GetNamespaceOfPrefix(prefix);
        return resolved;
    };

    upsertSchemaAnnotation(NamedSchemaAnnotation{targetNamespace, parseAnnotation(*root)});

    const auto resolveBuiltinSimpleType = [](const std::string& qualifiedName) -> std::optional<SimpleTypeRule> {
        const auto descriptor = ResolveBuiltinSimpleTypeDescriptor(qualifiedName);
        if (!descriptor.has_value()) {
            return std::nullopt;
        }

        SimpleTypeRule rule;
        rule.baseType = descriptor->baseType;
        rule.whiteSpace = descriptor->whiteSpace;
        if (descriptor->variety == BuiltinSimpleTypeDescriptor::Variety::List) {
            rule.variety = SimpleTypeRule::Variety::List;

            SimpleTypeRule itemType;
            itemType.baseType = descriptor->itemType;
            itemType.whiteSpace = descriptor->itemWhiteSpace;
            rule.itemType = std::make_shared<SimpleTypeRule>(itemType);
        }
        return rule;
    };

    const auto resolveBuiltinComplexType = [](const std::string& qualifiedName) -> std::optional<ComplexTypeRule> {
        if (qualifiedName != "xs:anyType") {
            return std::nullopt;
        }

        ComplexTypeRule rule;
        rule.namedTypeName = "anyType";
        rule.namedTypeNamespaceUri = "http://www.w3.org/2001/XMLSchema";
        rule.allowsText = true;
        rule.anyAttributeAllowed = true;
        rule.anyAttributeNamespaceConstraint = "##any";
        rule.anyAttributeProcessContents = "lax";

        Particle particle;
        particle.kind = Particle::Kind::Any;
        particle.namespaceUri = "##any";
        particle.processContents = "lax";
        particle.minOccurs = 0;
        particle.maxOccurs = std::numeric_limits<std::size_t>::max();
        rule.particle = particle;
        return rule;
    };

    const auto containsDerivationToken = [](const std::string& value, const std::string& token) {
        if (value == "#all") {
            return true;
        }

        std::istringstream stream(value);
        std::string current;
        while (stream >> current) {
            if (current == token) {
                return true;
            }
        }
        return false;
    };

    const auto effectiveDerivationControlValue = [](const std::string& localValue, const std::string& schemaDefaultValue) {
        return localValue.empty() ? schemaDefaultValue : localValue;
    };

    const auto wildcardNamespaceTokenMatches = [&](const std::string& token, const std::string& namespaceUri) {
        if (token.empty() || token == "##any") {
            return true;
        }
        if (token == "##other") {
            return namespaceUri != targetNamespace;
        }
        if (token == "##targetNamespace") {
            return namespaceUri == targetNamespace;
        }
        if (token == "##local") {
            return namespaceUri.empty();
        }
        return namespaceUri == token;
    };

    const auto wildcardTokenIsSubsetOfConstraint = [&](const std::string& token, const std::string& baseConstraint) {
        const std::string normalizedBase = baseConstraint.empty() ? "##any" : baseConstraint;
        if (normalizedBase == "##any") {
            return true;
        }
        if (token == "##any") {
            return normalizedBase == "##any";
        }
        if (token == "##other") {
            std::istringstream baseTokens(normalizedBase);
            std::string baseToken;
            while (baseTokens >> baseToken) {
                if (baseToken == "##other" || baseToken == "##any") {
                    return true;
                }
            }
            return false;
        }
        const std::string namespaceUri = token == "##targetNamespace" ? targetNamespace : (token == "##local" ? std::string{} : token);
        std::istringstream baseTokens(normalizedBase);
        std::string baseToken;
        while (baseTokens >> baseToken) {
            if (wildcardNamespaceTokenMatches(baseToken, namespaceUri)) {
                return true;
            }
        }
        return false;
    };

    std::function<SimpleTypeRule(const std::string&, const std::string&, const std::string&)> ensureSimpleTypeResolved;
    std::function<ComplexTypeRule(const std::string&, const std::string&, const std::string&)> ensureComplexTypeResolved;
    std::function<ElementRule(const std::string&, const std::string&, const std::string&)> ensureElementResolved;
    std::function<Particle(const std::string&, const std::string&, const std::string&)> ensureGroupResolved;
    std::function<AttributeUse(const std::string&, const std::string&, const std::string&)> ensureAttributeResolved;
    std::function<std::vector<AttributeUse>(const std::string&, const std::string&, const std::string&)> ensureAttributeGroupResolved;
    std::function<bool(const std::string&, const std::string&)> matchesWildcardNamespace;

    const auto ensureSimpleTypeFinalAllowsDerivation = [&](const SimpleTypeRule& baseRule,
        const std::string& qualifiedName,
        const std::string& derivationMethod,
        const std::string& description) {
        const bool blocked = (derivationMethod == "restriction" && baseRule.finalRestriction)
            || (derivationMethod == "list" && baseRule.finalList)
            || (derivationMethod == "union" && baseRule.finalUnion);
        if (!blocked) {
            return;
        }

        throw XmlException(
            "XML Schema " + description + " base '" + qualifiedName + "' blocks "
            + derivationMethod + " derivation via simpleType final");
    };

    const auto resolveSimpleTypeReference = [&](const XmlElement& context, const std::string& qualifiedName) -> SimpleTypeRule {
        if (const auto builtinType = resolveBuiltinSimpleType(qualifiedName); builtinType.has_value()) {
            return *builtinType;
        }

        const auto [localTypeName, typeNamespaceUri] = resolveTypeName(context, qualifiedName);
        return ensureSimpleTypeResolved(localTypeName, typeNamespaceUri, qualifiedName);
    };

    const auto resolveElementDeclarationNamespace = [&](const XmlElement& element, bool isGlobalDeclaration) {
        if (isGlobalDeclaration) {
            return targetNamespace;
        }

        const std::string form = parseFormValue(element.GetAttribute("form"), "element form");
        if (form.empty() && schemaElementFormDefault.empty()) {
            return targetNamespace;
        }
        const std::string effectiveForm = form.empty() ? schemaElementFormDefault : form;
        return effectiveForm == "qualified" ? targetNamespace : std::string{};
    };

    const auto resolveAttributeDeclarationNamespace = [&](const XmlElement& attribute, bool isGlobalDeclaration) {
        if (isGlobalDeclaration) {
            return targetNamespace;
        }

        const std::string form = parseFormValue(attribute.GetAttribute("form"), "attribute form");
        const std::string effectiveForm = form.empty() ? schemaAttributeFormDefault : form;
        return effectiveForm == "qualified" ? targetNamespace : std::string{};
    };

    const auto isSimpleTypeFacetElement = [&](const XmlElement& facetElement) {
        if (facetElement.NamespaceURI() != kSchemaNamespace) {
            return false;
        }

        const std::string& localName = facetElement.LocalName();
        return localName == "enumeration"
            || localName == "whiteSpace"
            || localName == "pattern"
            || localName == "length"
            || localName == "minLength"
            || localName == "maxLength"
            || localName == "minInclusive"
            || localName == "maxInclusive"
            || localName == "minExclusive"
            || localName == "maxExclusive"
            || localName == "totalDigits"
            || localName == "fractionDigits";
    };

    const auto applyFacetToSimpleType = [&](SimpleTypeRule& rule, const XmlElement& facetElement) {
        const std::string& localName = facetElement.LocalName();
        if (localName == "enumeration") {
            rule.enumerationValues.push_back(facetElement.GetAttribute("value"));
        } else if (localName == "whiteSpace") {
            rule.whiteSpace = facetElement.GetAttribute("value");
        } else if (localName == "pattern") {
            rule.pattern = facetElement.GetAttribute("value");
        } else if (localName == "length") {
            rule.length = static_cast<std::size_t>(std::stoull(facetElement.GetAttribute("value")));
        } else if (localName == "minLength") {
            rule.minLength = static_cast<std::size_t>(std::stoull(facetElement.GetAttribute("value")));
        } else if (localName == "maxLength") {
            rule.maxLength = static_cast<std::size_t>(std::stoull(facetElement.GetAttribute("value")));
        } else if (localName == "minInclusive") {
            rule.minInclusive = facetElement.GetAttribute("value");
        } else if (localName == "maxInclusive") {
            rule.maxInclusive = facetElement.GetAttribute("value");
        } else if (localName == "minExclusive") {
            rule.minExclusive = facetElement.GetAttribute("value");
        } else if (localName == "maxExclusive") {
            rule.maxExclusive = facetElement.GetAttribute("value");
        } else if (localName == "totalDigits") {
            rule.totalDigits = static_cast<std::size_t>(std::stoull(facetElement.GetAttribute("value")));
        } else if (localName == "fractionDigits") {
            rule.fractionDigits = static_cast<std::size_t>(std::stoull(facetElement.GetAttribute("value")));
        }
    };

    std::function<SimpleTypeRule(const XmlElement&)> parseSimpleType;
    std::function<ComplexTypeRule(const XmlElement&)> parseComplexType;
    std::function<ElementRule(const XmlElement&, bool)> parseElementRule;
    std::function<AttributeUse(const XmlElement&, bool)> parseAttributeUse;
    std::function<std::vector<AttributeUse>(const XmlElement&, bool&, std::string&, std::string&)> parseAttributeGroup;
    std::function<ElementRule::IdentityConstraint(const XmlElement&)> parseIdentityConstraint;
    std::function<void(const SimpleTypeRule&, const SimpleTypeRule&, const std::string&)> validateDerivedSimpleTypeRestriction;
    const auto tryCompileIdentityConstraintXPath = [](const std::string& xpath,
                                                      const std::vector<std::pair<std::string, std::string>>& namespaceBindings,
                                                      const bool allowAttributeTerminal)
        -> std::optional<ElementRule::IdentityConstraint::CompiledPath> {
        using CompiledPath = ElementRule::IdentityConstraint::CompiledPath;
        using CompiledPathStep = ElementRule::IdentityConstraint::CompiledPathStep;

        const auto lookupNamespaceUriInBindings = [&](const std::string& prefix) {
            for (auto it = namespaceBindings.rbegin(); it != namespaceBindings.rend(); ++it) {
                if (it->first == prefix) {
                    return it->second;
                }
            }
            return std::string{};
        };

        const auto trim = [](std::string_view text) {
            std::size_t start = 0;
            while (start < text.size() && IsWhitespace(text[start])) {
                ++start;
            }

            std::size_t end = text.size();
            while (end > start && IsWhitespace(text[end - 1])) {
                --end;
            }

            return std::string(text.substr(start, end - start));
        };

        const auto parseQualifiedName = [&](const std::string& qualifiedName,
                                            std::string& localName,
                                            std::string& namespaceUri,
                                            const bool allowWildcard) {
            const auto [prefix, parsedLocalName] = SplitQualifiedName(qualifiedName);
            if (parsedLocalName.empty()) {
                return false;
            }
            if ((!allowWildcard || parsedLocalName != "*") && parsedLocalName.find('*') != std::string::npos) {
                return false;
            }

            try {
                if (!prefix.empty()) {
                    (void)XmlConvert::VerifyNCName(prefix);
                }
                if (parsedLocalName != "*") {
                    (void)XmlConvert::VerifyNCName(parsedLocalName);
                }
            } catch (const XmlException&) {
                return false;
            }

            localName = parsedLocalName;
            namespaceUri.clear();
            if (!prefix.empty()) {
                namespaceUri = lookupNamespaceUriInBindings(prefix);
                if (namespaceUri.empty()) {
                    return false;
                }
            }
            return true;
        };

        if (xpath.empty()) {
            return std::nullopt;
        }

        CompiledPath compiledPath;
        std::size_t position = 0;
        bool nextStepIsDescendant = false;
        if (xpath.size() >= 2 && xpath[0] == '/' && xpath[1] == '/') {
            nextStepIsDescendant = true;
            position = 2;
            if (position >= xpath.size()) {
                return std::nullopt;
            }
        } else if (!xpath.empty() && xpath[0] == '/') {
            return std::nullopt;
        }
        while (position <= xpath.size()) {
            std::size_t segmentEnd = position;
            while (segmentEnd < xpath.size() && xpath[segmentEnd] != '/') {
                ++segmentEnd;
            }
            const std::string segment = xpath.substr(position, segmentEnd - position);
            if (segment.empty()) {
                return std::nullopt;
            }

            const bool isLastSegment = segmentEnd == xpath.size();
            if (segment == ".") {
                if (nextStepIsDescendant) {
                    return std::nullopt;
                }
                compiledPath.steps.push_back(CompiledPathStep{CompiledPathStep::Kind::Self, {}, {}});
            } else {
                CompiledPathStep step;
                std::string qualifiedName = segment;
                if (!segment.empty() && segment.front() == '@') {
                    if (!allowAttributeTerminal || !isLastSegment || segment.size() == 1) {
                        return std::nullopt;
                    }
                    step.kind = CompiledPathStep::Kind::Attribute;
                    qualifiedName = segment.substr(1);
                } else {
                    step.kind = nextStepIsDescendant
                        ? CompiledPathStep::Kind::DescendantElement
                        : CompiledPathStep::Kind::Element;

                    const auto predicateStart = segment.find('[');
                    if (predicateStart != std::string::npos) {
                        if (segment.back() != ']'
                            || segment.find('[', predicateStart + 1) != std::string::npos
                            || segment.find(']', predicateStart + 1) != segment.size() - 1) {
                            return std::nullopt;
                        }

                        qualifiedName = segment.substr(0, predicateStart);
                        const std::string predicate = trim(std::string_view(segment).substr(predicateStart + 1, segment.size() - predicateStart - 2));
                        const std::size_t equalsPosition = predicate.find('=');
                        if (qualifiedName.empty()
                            || equalsPosition == std::string::npos
                            || predicate.find('=', equalsPosition + 1) != std::string::npos) {
                            return std::nullopt;
                        }

                        const std::string left = trim(std::string_view(predicate).substr(0, equalsPosition));
                        const std::string right = trim(std::string_view(predicate).substr(equalsPosition + 1));
                        if (left.size() <= 1 || left.front() != '@' || right.size() < 2) {
                            return std::nullopt;
                        }

                        const char quote = right.front();
                        if ((quote != '\'' && quote != '"') || right.back() != quote) {
                            return std::nullopt;
                        }

                        if (!parseQualifiedName(left.substr(1), step.predicateAttributeLocalName, step.predicateAttributeNamespaceUri, false)) {
                            return std::nullopt;
                        }
                        step.predicateAttributeValue = right.substr(1, right.size() - 2);
                    }
                }

                if (qualifiedName.find_first_of("()|") != std::string::npos
                    || qualifiedName.find("::") != std::string::npos) {
                    return std::nullopt;
                }

                if (!parseQualifiedName(qualifiedName, step.localName, step.namespaceUri, true)) {
                    return std::nullopt;
                }

                compiledPath.steps.push_back(std::move(step));
            }

            nextStepIsDescendant = false;
            if (segmentEnd == xpath.size()) {
                break;
            }

            if (xpath[segmentEnd] != '/') {
                return std::nullopt;
            }

            std::size_t nextPosition = segmentEnd + 1;
            if (nextPosition < xpath.size() && xpath[nextPosition] == '/') {
                nextStepIsDescendant = true;
                ++nextPosition;
            }
            if (nextPosition >= xpath.size()) {
                return std::nullopt;
            }
            position = nextPosition;
        }

        return compiledPath.steps.empty() ? std::nullopt : std::optional<CompiledPath>(std::move(compiledPath));
    };

    parseSimpleType = [&](const XmlElement& simpleTypeElement) -> SimpleTypeRule {
        const std::string effectiveFinal = effectiveDerivationControlValue(simpleTypeElement.GetAttribute("final"), schemaFinalDefault);
        const bool finalRestriction = containsDerivationToken(effectiveFinal, "restriction");
        const bool finalList = containsDerivationToken(effectiveFinal, "list");
        const bool finalUnion = containsDerivationToken(effectiveFinal, "union");
        const auto applySimpleTypeFinalFlags = [&](SimpleTypeRule& target) {
            target.finalRestriction = finalRestriction;
            target.finalList = finalList;
            target.finalUnion = finalUnion;
        };

        const XmlSchemaSet::Annotation declarationAnnotation = parseAnnotation(simpleTypeElement);
        SimpleTypeRule rule;
        applySimpleTypeFinalFlags(rule);
        rule.annotation = declarationAnnotation;
    rule.namedTypeName.clear();
    rule.namedTypeNamespaceUri.clear();
    rule.derivationMethod = SimpleTypeRule::DerivationMethod::None;
    rule.derivationBaseName.clear();
    rule.derivationBaseNamespaceUri.clear();
        for (const auto& child : simpleTypeElement.ChildNodes()) {
            if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                continue;
            }
            const auto* childElement = static_cast<const XmlElement*>(child.get());
            if (childElement->NamespaceURI() != kSchemaNamespace) {
                continue;
            }

            if (childElement->LocalName() == "restriction") {
                const std::string base = childElement->GetAttribute("base");
                std::optional<SimpleTypeRule> restrictionBaseRule;
                std::string restrictionBaseLabel;
                if (!base.empty()) {
                    if (const auto builtinType = resolveBuiltinSimpleType(base); builtinType.has_value()) {
                        rule = *builtinType;
                        restrictionBaseRule = *builtinType;
                        restrictionBaseLabel = base;
                        applySimpleTypeFinalFlags(rule);
                        rule.namedTypeName.clear();
                        rule.namedTypeNamespaceUri.clear();
                        rule.derivationMethod = SimpleTypeRule::DerivationMethod::Restriction;
                        rule.derivationBaseName.clear();
                        rule.derivationBaseNamespaceUri.clear();
                    } else {
                        const auto [baseLocalName, baseNamespaceUri] = resolveTypeName(*childElement, base);
                        const SimpleTypeRule baseRule = resolveSimpleTypeReference(*childElement, base);
                        ensureSimpleTypeFinalAllowsDerivation(baseRule, base, "restriction", "simpleType restriction");
                        rule = baseRule;
                        restrictionBaseRule = baseRule;
                        restrictionBaseLabel = base;
                        applySimpleTypeFinalFlags(rule);
                        rule.namedTypeName.clear();
                        rule.namedTypeNamespaceUri.clear();
                        rule.derivationMethod = SimpleTypeRule::DerivationMethod::Restriction;
                        rule.derivationBaseName = baseLocalName;
                        rule.derivationBaseNamespaceUri = baseNamespaceUri;
                    }
                }
                for (const auto& restrictionChild : childElement->ChildNodes()) {
                    if (restrictionChild == nullptr || restrictionChild->NodeType() != XmlNodeType::Element) {
                        continue;
                    }
                    const auto* facet = static_cast<const XmlElement*>(restrictionChild.get());
                    if (facet->LocalName() == "simpleType" && facet->NamespaceURI() == kSchemaNamespace) {
                        rule = parseSimpleType(*facet);
                        restrictionBaseRule = rule;
                        restrictionBaseLabel = "inline simpleType";
                        applySimpleTypeFinalFlags(rule);
                    } else if (isSimpleTypeFacetElement(*facet)) {
                        applyFacetToSimpleType(rule, *facet);
                    }
                }

                if (restrictionBaseRule.has_value()) {
                    validateDerivedSimpleTypeRestriction(*restrictionBaseRule, rule,
                        "simpleType restriction base '" + restrictionBaseLabel + "'");
                }
                rule.annotation = declarationAnnotation;
                return rule;
            }

            if (childElement->LocalName() == "list") {
                rule.variety = SimpleTypeRule::Variety::List;
                rule.baseType = "xs:string";
                rule.whiteSpace = "collapse";
                rule.namedTypeName.clear();
                rule.namedTypeNamespaceUri.clear();
                rule.derivationMethod = SimpleTypeRule::DerivationMethod::List;
                rule.derivationBaseName.clear();
                rule.derivationBaseNamespaceUri.clear();

                const std::string itemType = childElement->GetAttribute("itemType");
                if (!itemType.empty()) {
                    const SimpleTypeRule itemTypeRule = resolveSimpleTypeReference(*childElement, itemType);
                    ensureSimpleTypeFinalAllowsDerivation(itemTypeRule, itemType, "list", "list simpleType");
                    rule.itemType = std::make_shared<SimpleTypeRule>(itemTypeRule);
                }

                for (const auto& listChild : childElement->ChildNodes()) {
                    if (listChild == nullptr || listChild->NodeType() != XmlNodeType::Element) {
                        continue;
                    }
                    const auto* listChildElement = static_cast<const XmlElement*>(listChild.get());
                    if (listChildElement->LocalName() == "simpleType" && listChildElement->NamespaceURI() == kSchemaNamespace) {
                        rule.itemType = std::make_shared<SimpleTypeRule>(parseSimpleType(*listChildElement));
                    }
                }

                if (rule.itemType == nullptr) {
                    throw XmlException("XML Schema list simpleType requires an itemType");
                }
                rule.annotation = declarationAnnotation;
                return rule;
            }

            if (childElement->LocalName() == "union") {
                rule.variety = SimpleTypeRule::Variety::Union;
                rule.baseType = "xs:string";
                rule.namedTypeName.clear();
                rule.namedTypeNamespaceUri.clear();
                rule.derivationMethod = SimpleTypeRule::DerivationMethod::Union;
                rule.derivationBaseName.clear();
                rule.derivationBaseNamespaceUri.clear();

                const std::string memberTypes = childElement->GetAttribute("memberTypes");
                if (!memberTypes.empty()) {
                    std::istringstream members(memberTypes);
                    std::string memberTypeName;
                    while (members >> memberTypeName) {
                        const SimpleTypeRule memberTypeRule = resolveSimpleTypeReference(*childElement, memberTypeName);
                        ensureSimpleTypeFinalAllowsDerivation(memberTypeRule, memberTypeName, "union", "union simpleType");
                        rule.memberTypes.push_back(memberTypeRule);
                    }
                }

                for (const auto& unionChild : childElement->ChildNodes()) {
                    if (unionChild == nullptr || unionChild->NodeType() != XmlNodeType::Element) {
                        continue;
                    }
                    const auto* unionChildElement = static_cast<const XmlElement*>(unionChild.get());
                    if (unionChildElement->LocalName() == "simpleType" && unionChildElement->NamespaceURI() == kSchemaNamespace) {
                        rule.memberTypes.push_back(parseSimpleType(*unionChildElement));
                    }
                }

                if (rule.memberTypes.empty()) {
                    throw XmlException("XML Schema union simpleType requires at least one member type");
                }
                rule.annotation = declarationAnnotation;
                return rule;
            }
        }
        rule.annotation = declarationAnnotation;
        return rule;
    };

    parseIdentityConstraint = [&](const XmlElement& constraintElement) -> ElementRule::IdentityConstraint {
        ElementRule::IdentityConstraint constraint;
        if (constraintElement.LocalName() == "key") {
            constraint.kind = ElementRule::IdentityConstraint::Kind::Key;
        } else if (constraintElement.LocalName() == "unique") {
            constraint.kind = ElementRule::IdentityConstraint::Kind::Unique;
        } else {
            constraint.kind = ElementRule::IdentityConstraint::Kind::KeyRef;
        }

        constraint.name = constraintElement.GetAttribute("name");
        constraint.namespaceUri = targetNamespace;
        if (constraint.name.empty()) {
            throw XmlException("XML Schema identity constraints require a name");
        }

        constraint.annotation = parseAnnotation(constraintElement);

        const auto namespaceBindings = CollectInScopeNamespaceBindings(constraintElement);
        constraint.namespaceBindings = namespaceBindings;
        constraint.compiledFieldPaths.clear();

        if (constraint.kind == ElementRule::IdentityConstraint::Kind::KeyRef) {
            const std::string refer = constraintElement.GetAttribute("refer");
            if (refer.empty()) {
                throw XmlException("XML Schema keyref constraints require a refer attribute");
            }
            const auto [referName, referNamespaceUri] = resolveTypeName(constraintElement, refer);
            constraint.referName = referName;
            constraint.referNamespaceUri = referNamespaceUri;
        }

        for (const auto& child : constraintElement.ChildNodes()) {
            if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                continue;
            }
            const auto* childElement = static_cast<const XmlElement*>(child.get());
            if (childElement->NamespaceURI() != kSchemaNamespace) {
                continue;
            }
            if (childElement->LocalName() == "selector") {
                if (!constraint.selectorXPath.empty()) {
                    throw XmlException("XML Schema identity constraints can only declare a single selector");
                }
                constraint.selectorXPath = childElement->GetAttribute("xpath");
            } else if (childElement->LocalName() == "field") {
                constraint.fieldXPaths.push_back(childElement->GetAttribute("xpath"));
            }
        }

        if (constraint.selectorXPath.empty()) {
            throw XmlException("XML Schema identity constraints require a selector xpath");
        }
        constraint.compiledSelectorPath = tryCompileIdentityConstraintXPath(
            constraint.selectorXPath,
            constraint.namespaceBindings,
            false);
        if (constraint.fieldXPaths.empty()) {
            throw XmlException("XML Schema identity constraints require at least one field xpath");
        }

        constraint.compiledFieldPaths.reserve(constraint.fieldXPaths.size());
        for (const auto& fieldXPath : constraint.fieldXPaths) {
            if (fieldXPath.empty()) {
                throw XmlException("XML Schema identity constraint field xpath cannot be empty");
            }
            constraint.compiledFieldPaths.push_back(tryCompileIdentityConstraintXPath(
                fieldXPath,
                constraint.namespaceBindings,
                true));
        }

        upsertIdentityConstraint(constraint);
        return constraint;
    };

    ensureSimpleTypeResolved = [&](const std::string& localTypeName, const std::string& typeNamespaceUri, const std::string& qualifiedName) -> SimpleTypeRule {
        const std::string ruleKey = makeQualifiedRuleKey(localTypeName, typeNamespaceUri);
        const auto declaration = declaredSimpleTypes.find(ruleKey);
        if (declaration == declaredSimpleTypes.end()) {
            if (const auto* namedType = FindSimpleTypeRule(localTypeName, typeNamespaceUri)) {
                return *namedType;
            }
            throw XmlException("XML Schema simpleType '" + qualifiedName + "' is not supported");
        }

        if (resolvedLocalSimpleTypes.find(ruleKey) != resolvedLocalSimpleTypes.end()) {
            if (const auto* namedType = FindSimpleTypeRule(localTypeName, typeNamespaceUri)) {
                return *namedType;
            }
        }

        if (!resolvingSimpleTypes.insert(ruleKey).second) {
            throw XmlException("XML Schema simpleType '" + qualifiedName + "' contains a circular type reference");
        }

        SimpleTypeRule rule;
        try {
            rule = parseSimpleType(*declaration->second);
        } catch (...) {
            resolvingSimpleTypes.erase(ruleKey);
            throw;
        }
        resolvingSimpleTypes.erase(ruleKey);

        rule.namedTypeName = localTypeName;
        rule.namedTypeNamespaceUri = typeNamespaceUri;

        upsertSimpleType(NamedSimpleTypeRule{localTypeName, typeNamespaceUri, rule});
        resolvedLocalSimpleTypes.insert(ruleKey);
        return rule;
    };

    const auto applyComplexType = [](ElementRule& elementRule, const ComplexTypeRule& complexTypeRule) {
        elementRule.allowsText = complexTypeRule.allowsText;
        elementRule.textType = complexTypeRule.textType;
        elementRule.contentModel = complexTypeRule.contentModel;
        elementRule.attributes = complexTypeRule.attributes;
        elementRule.children = complexTypeRule.children;
        elementRule.particle = complexTypeRule.particle;
        elementRule.anyAttributeAllowed = complexTypeRule.anyAttributeAllowed;
        elementRule.anyAttributeNamespaceConstraint = complexTypeRule.anyAttributeNamespaceConstraint;
        elementRule.anyAttributeProcessContents = complexTypeRule.anyAttributeProcessContents;
    };

    std::function<Particle(const XmlElement&)> parseParticle;
    const auto appendAttributes = [](std::vector<AttributeUse>& destination, const std::vector<AttributeUse>& source) {
        destination.insert(destination.end(), source.begin(), source.end());
    };
    const auto mergeAnyAttribute = [](bool& destinationAllowed, std::string& destinationConstraint, std::string& destinationProcessContents,
                                      bool sourceAllowed, const std::string& sourceConstraint, const std::string& sourceProcessContents) {
        if (!sourceAllowed) {
            return;
        }
        destinationAllowed = true;
        destinationConstraint = sourceConstraint;
        destinationProcessContents = sourceProcessContents;
    };

    matchesWildcardNamespace = [&](const std::string& namespaceConstraint, const std::string& namespaceUri) {
        if (namespaceConstraint.empty() || namespaceConstraint == "##any") {
            return true;
        }
        std::istringstream tokens(namespaceConstraint);
        std::string token;
        while (tokens >> token) {
            if (wildcardNamespaceTokenMatches(token, namespaceUri)) {
                return true;
            }
        }
        return false;
    };

    const auto makeFlatParticleFromRule = [&](const ComplexTypeRule& rule) -> std::optional<Particle> {
        if (rule.particle.has_value()) {
            return rule.particle;
        }
        if (rule.children.empty()) {
            return std::nullopt;
        }

        Particle particle;
        particle.kind = rule.contentModel == ContentModel::Choice ? Particle::Kind::Choice : Particle::Kind::Sequence;
        for (const auto& childRule : rule.children) {
            Particle childParticle;
            childParticle.kind = Particle::Kind::Element;
            childParticle.name = childRule.name;
            childParticle.namespaceUri = childRule.namespaceUri;
            childParticle.elementSimpleType = childRule.declaredSimpleType;
            if (childRule.declaredComplexType) {
                childParticle.elementComplexType = std::make_shared<ComplexTypeRule>(*childRule.declaredComplexType);
            } else if (const auto* declaredRule = FindElementRule(childRule.name, childRule.namespaceUri)) {
                childParticle.elementSimpleType = declaredRule->declaredSimpleType;
                if (declaredRule->declaredComplexType.has_value()) {
                    childParticle.elementComplexType = std::make_shared<ComplexTypeRule>(*declaredRule->declaredComplexType);
                }
                childParticle.elementIsNillable = declaredRule->isNillable;
                childParticle.elementDefaultValue = declaredRule->defaultValue;
                childParticle.elementFixedValue = declaredRule->fixedValue;
                childParticle.elementBlockRestriction = declaredRule->blockRestriction;
                childParticle.elementBlockExtension = declaredRule->blockExtension;
                childParticle.elementFinalRestriction = declaredRule->finalRestriction;
                childParticle.elementFinalExtension = declaredRule->finalExtension;
            }
            childParticle.elementIsNillable = childRule.isNillable;
            childParticle.elementDefaultValue = childRule.defaultValue;
            childParticle.elementFixedValue = childRule.fixedValue;
            childParticle.elementBlockRestriction = childRule.blockRestriction;
            childParticle.elementBlockExtension = childRule.blockExtension;
            childParticle.elementFinalRestriction = childRule.finalRestriction;
            childParticle.elementFinalExtension = childRule.finalExtension;
            childParticle.minOccurs = childRule.minOccurs;
            childParticle.maxOccurs = childRule.maxOccurs;
            particle.children.push_back(childParticle);
        }
        return particle;
    };

    const auto applyParticleToRule = [](ComplexTypeRule& rule, const Particle& particle) {
        const bool isFlatParticle = particle.minOccurs == 1
            && particle.maxOccurs == 1
            && particle.kind != Particle::Kind::All
            && std::all_of(particle.children.begin(), particle.children.end(), [](const auto& childParticle) {
                return childParticle.kind == Particle::Kind::Element;
            });

        if (isFlatParticle) {
            rule.particle.reset();
            rule.contentModel = particle.kind == Particle::Kind::Choice ? ContentModel::Choice : ContentModel::Sequence;
            rule.children.clear();
            for (const auto& childParticle : particle.children) {
                ChildUse childUse;
                childUse.name = childParticle.name;
                childUse.namespaceUri = childParticle.namespaceUri;
                childUse.declaredSimpleType = childParticle.elementSimpleType;
                if (childParticle.elementComplexType) {
                    childUse.declaredComplexType = std::make_shared<ComplexTypeRule>(*childParticle.elementComplexType);
                }
                childUse.isNillable = childParticle.elementIsNillable;
                childUse.defaultValue = childParticle.elementDefaultValue;
                childUse.fixedValue = childParticle.elementFixedValue;
                childUse.blockRestriction = childParticle.elementBlockRestriction;
                childUse.blockExtension = childParticle.elementBlockExtension;
                childUse.finalRestriction = childParticle.elementFinalRestriction;
                childUse.finalExtension = childParticle.elementFinalExtension;
                childUse.minOccurs = childParticle.minOccurs;
                childUse.maxOccurs = childParticle.maxOccurs;
                rule.children.push_back(childUse);
            }
            return;
        }

        rule.particle = particle;
        rule.children.clear();
        rule.contentModel = ContentModel::Empty;
    };

    const auto upsertAttributeUse = [](std::vector<AttributeUse>& attributes, const AttributeUse& attributeUse) {
        const auto existing = std::find_if(attributes.begin(), attributes.end(), [&](const auto& current) {
            return current.name == attributeUse.name && current.namespaceUri == attributeUse.namespaceUri;
        });
        if (existing != attributes.end()) {
            *existing = attributeUse;
        } else {
            attributes.push_back(attributeUse);
        }
    };

    const auto eraseAttributeUse = [](std::vector<AttributeUse>& attributes, const std::string& name, const std::string& namespaceUri) {
        attributes.erase(std::remove_if(attributes.begin(), attributes.end(), [&](const auto& current) {
            return current.name == name && current.namespaceUri == namespaceUri;
        }), attributes.end());
    };

    const auto simpleTypeBaseFamily = [](const SimpleTypeRule& rule) {
        if (rule.variety == SimpleTypeRule::Variety::List) {
            return std::string("list");
        }
        if (rule.variety == SimpleTypeRule::Variety::Union) {
            return std::string("union");
        }
        return rule.baseType.empty() ? std::string("xs:string") : rule.baseType;
    };

    const auto whitespaceStrength = [](const std::optional<std::string>& value) {
        if (!value.has_value() || *value == "preserve") {
            return 0;
        }
        if (*value == "replace") {
            return 1;
        }
        if (*value == "collapse") {
            return 2;
        }
        return -1;
    };

    const auto compareFacetNumbers = [](const std::string& baseType, const std::string& left, const std::string& right) {
        if (IsPracticalIntegerBuiltinType(baseType)) {
            return ComparePracticalIntegerValues(ParsePracticalIntegerOrThrow(left), ParsePracticalIntegerOrThrow(right));
        }
        if (baseType == "xs:decimal") {
            return ComparePracticalDecimalValues(ParsePracticalDecimalOrThrow(left), ParsePracticalDecimalOrThrow(right));
        }
        if (baseType == "xs:float") {
            const float leftValue = XmlConvert::ToSingle(left);
            const float rightValue = XmlConvert::ToSingle(right);
            if (leftValue < rightValue) {
                return -1;
            }
            if (leftValue > rightValue) {
                return 1;
            }
            return 0;
        }

        const double leftValue = XmlConvert::ToDouble(left);
        const double rightValue = XmlConvert::ToDouble(right);
        if (leftValue < rightValue) {
            return -1;
        }
        if (leftValue > rightValue) {
            return 1;
        }
        return 0;
    };

    validateDerivedSimpleTypeRestriction = [&](const SimpleTypeRule& baseRule, const SimpleTypeRule& derivedRule, const std::string& label) {
        struct RestrictionTemporalValue {
            long long primary = 0;
            int secondary = 0;
            int fractionalNanoseconds = 0;
        };

        const auto compareRestrictionTemporalValues = [](const RestrictionTemporalValue& left, const RestrictionTemporalValue& right) {
            if (left.primary != right.primary) {
                return left.primary < right.primary ? -1 : 1;
            }
            if (left.secondary != right.secondary) {
                return left.secondary < right.secondary ? -1 : 1;
            }
            if (left.fractionalNanoseconds != right.fractionalNanoseconds) {
                return left.fractionalNanoseconds < right.fractionalNanoseconds ? -1 : 1;
            }
            return 0;
        };

        const auto normalizeRestrictionTemporalValue = [&](const std::string& baseType, const std::string& lexicalValue) -> RestrictionTemporalValue {
            const auto parseFixedWidthNumber = [&](const std::string& text, const std::size_t start, const std::size_t length) {
                if (start + length > text.size()) {
                    throw XmlException("invalid temporal lexical form");
                }
                int value = 0;
                for (std::size_t index = 0; index < length; ++index) {
                    const char ch = text[start + index];
                    if (!std::isdigit(static_cast<unsigned char>(ch))) {
                        throw XmlException("invalid temporal lexical form");
                    }
                    value = value * 10 + (ch - '0');
                }
                return value;
            };

            const auto parseTimezoneOffsetMinutes = [&](const std::string& text, const std::size_t start) {
                if (start >= text.size()) {
                    return 0;
                }
                if (text[start] == 'Z') {
                    if (start + 1 != text.size()) {
                        throw XmlException("invalid temporal lexical form");
                    }
                    return 0;
                }
                if ((text[start] != '+' && text[start] != '-') || start + 6 != text.size() || text[start + 3] != ':') {
                    throw XmlException("invalid temporal lexical form");
                }
                const int hours = parseFixedWidthNumber(text, start + 1, 2);
                const int minutes = parseFixedWidthNumber(text, start + 4, 2);
                if (hours > 14 || minutes > 59 || (hours == 14 && minutes != 0)) {
                    throw XmlException("invalid temporal lexical form");
                }
                const int totalMinutes = hours * 60 + minutes;
                return text[start] == '-' ? -totalMinutes : totalMinutes;
            };

            const auto parseFractionalNanoseconds = [&](const std::string& digits) {
                if (digits.empty()) {
                    throw XmlException("invalid temporal lexical form");
                }
                int value = 0;
                for (std::size_t index = 0; index < 9; ++index) {
                    value *= 10;
                    if (index < digits.size()) {
                        const char ch = digits[index];
                        if (!std::isdigit(static_cast<unsigned char>(ch))) {
                            throw XmlException("invalid temporal lexical form");
                        }
                        value += ch - '0';
                    }
                }
                for (std::size_t index = 9; index < digits.size(); ++index) {
                    if (!std::isdigit(static_cast<unsigned char>(digits[index]))) {
                        throw XmlException("invalid temporal lexical form");
                    }
                }
                return value;
            };

            const auto isLeapYear = [](const int year) {
                return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
            };

            const auto daysInMonth = [&](const int year, const int month) {
                static const int kDaysInMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
                if (month < 1 || month > 12) {
                    return 0;
                }
                if (month == 2 && isLeapYear(year)) {
                    return 29;
                }
                return kDaysInMonth[month - 1];
            };

            const auto daysFromCivil = [](int year, const unsigned month, const unsigned day) -> long long {
                year -= month <= 2 ? 1 : 0;
                const long long era = static_cast<long long>(year >= 0 ? year : year - 399) / 400LL;
                const unsigned yoe = static_cast<unsigned>(year - static_cast<int>(era * 400LL));
                const unsigned shiftedMonth = month > 2 ? month - 3 : month + 9;
                const unsigned doy = (153 * shiftedMonth + 2) / 5 + day - 1;
                const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
                return era * 146097LL + static_cast<long long>(doe) - 719468LL;
            };

            auto normalizeDateTimeComponents = [](long long& dayValue, int& secondsOfDay, const int offsetMinutes) {
                secondsOfDay -= offsetMinutes * 60;
                while (secondsOfDay < 0) {
                    secondsOfDay += 24 * 60 * 60;
                    --dayValue;
                }
                while (secondsOfDay >= 24 * 60 * 60) {
                    secondsOfDay -= 24 * 60 * 60;
                    ++dayValue;
                }
            };

            if (baseType == "xs:date") {
                if (lexicalValue.size() < 10 || lexicalValue[4] != '-' || lexicalValue[7] != '-') {
                    throw XmlException("invalid temporal lexical form");
                }
                const int year = parseFixedWidthNumber(lexicalValue, 0, 4);
                const int month = parseFixedWidthNumber(lexicalValue, 5, 2);
                const int day = parseFixedWidthNumber(lexicalValue, 8, 2);
                if (day < 1 || day > daysInMonth(year, month)) {
                    throw XmlException("invalid temporal lexical form");
                }
                const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, 10);
                long long absoluteDay = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
                int secondsOfDay = 0;
                normalizeDateTimeComponents(absoluteDay, secondsOfDay, timezoneOffsetMinutes);
                return RestrictionTemporalValue{ absoluteDay, secondsOfDay, 0 };
            }

            if (baseType == "xs:gYear") {
                if (lexicalValue.size() < 4) {
                    throw XmlException("invalid temporal lexical form");
                }
                const int year = parseFixedWidthNumber(lexicalValue, 0, 4);
                const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, 4);
                long long absoluteDay = daysFromCivil(year, 1, 1);
                int secondsOfDay = 0;
                normalizeDateTimeComponents(absoluteDay, secondsOfDay, timezoneOffsetMinutes);
                return RestrictionTemporalValue{ absoluteDay, secondsOfDay, 0 };
            }

            if (baseType == "xs:gYearMonth") {
                if (lexicalValue.size() < 7 || lexicalValue[4] != '-') {
                    throw XmlException("invalid temporal lexical form");
                }
                const int year = parseFixedWidthNumber(lexicalValue, 0, 4);
                const int month = parseFixedWidthNumber(lexicalValue, 5, 2);
                if (month < 1 || month > 12) {
                    throw XmlException("invalid temporal lexical form");
                }
                const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, 7);
                long long absoluteDay = daysFromCivil(year, static_cast<unsigned>(month), 1);
                int secondsOfDay = 0;
                normalizeDateTimeComponents(absoluteDay, secondsOfDay, timezoneOffsetMinutes);
                return RestrictionTemporalValue{ absoluteDay, secondsOfDay, 0 };
            }

            if (baseType == "xs:gMonth") {
                if (lexicalValue.size() < 4 || lexicalValue[0] != '-' || lexicalValue[1] != '-') {
                    throw XmlException("invalid temporal lexical form");
                }
                const int month = parseFixedWidthNumber(lexicalValue, 2, 2);
                if (month < 1 || month > 12) {
                    throw XmlException("invalid temporal lexical form");
                }
                const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, 4);
                long long absoluteDay = daysFromCivil(2000, static_cast<unsigned>(month), 1);
                int secondsOfDay = 0;
                normalizeDateTimeComponents(absoluteDay, secondsOfDay, timezoneOffsetMinutes);
                return RestrictionTemporalValue{ absoluteDay, secondsOfDay, 0 };
            }

            if (baseType == "xs:gDay") {
                if (lexicalValue.size() < 5 || lexicalValue[0] != '-' || lexicalValue[1] != '-' || lexicalValue[2] != '-') {
                    throw XmlException("invalid temporal lexical form");
                }
                const int day = parseFixedWidthNumber(lexicalValue, 3, 2);
                if (day < 1 || day > 31) {
                    throw XmlException("invalid temporal lexical form");
                }
                const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, 5);
                long long absoluteDay = daysFromCivil(2000, 1, static_cast<unsigned>(day));
                int secondsOfDay = 0;
                normalizeDateTimeComponents(absoluteDay, secondsOfDay, timezoneOffsetMinutes);
                return RestrictionTemporalValue{ absoluteDay, secondsOfDay, 0 };
            }

            if (baseType == "xs:gMonthDay") {
                if (lexicalValue.size() < 7 || lexicalValue[0] != '-' || lexicalValue[1] != '-' || lexicalValue[4] != '-') {
                    throw XmlException("invalid temporal lexical form");
                }
                const int month = parseFixedWidthNumber(lexicalValue, 2, 2);
                const int day = parseFixedWidthNumber(lexicalValue, 5, 2);
                if (day < 1 || day > daysInMonth(2000, month)) {
                    throw XmlException("invalid temporal lexical form");
                }
                const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, 7);
                long long absoluteDay = daysFromCivil(2000, static_cast<unsigned>(month), static_cast<unsigned>(day));
                int secondsOfDay = 0;
                normalizeDateTimeComponents(absoluteDay, secondsOfDay, timezoneOffsetMinutes);
                return RestrictionTemporalValue{ absoluteDay, secondsOfDay, 0 };
            }

            if (baseType == "xs:time") {
                if (lexicalValue.size() < 8 || lexicalValue[2] != ':' || lexicalValue[5] != ':') {
                    throw XmlException("invalid temporal lexical form");
                }
                const int hour = parseFixedWidthNumber(lexicalValue, 0, 2);
                const int minute = parseFixedWidthNumber(lexicalValue, 3, 2);
                const int second = parseFixedWidthNumber(lexicalValue, 6, 2);
                if (hour > 23 || minute > 59 || second > 59) {
                    throw XmlException("invalid temporal lexical form");
                }
                std::size_t timezoneStart = 8;
                int fractionalNanoseconds = 0;
                if (timezoneStart < lexicalValue.size() && lexicalValue[timezoneStart] == '.') {
                    const std::size_t fractionStart = timezoneStart + 1;
                    std::size_t fractionEnd = fractionStart;
                    while (fractionEnd < lexicalValue.size() && std::isdigit(static_cast<unsigned char>(lexicalValue[fractionEnd]))) {
                        ++fractionEnd;
                    }
                    fractionalNanoseconds = parseFractionalNanoseconds(lexicalValue.substr(fractionStart, fractionEnd - fractionStart));
                    timezoneStart = fractionEnd;
                }
                const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, timezoneStart);
                int secondsOfDay = hour * 3600 + minute * 60 + second - timezoneOffsetMinutes * 60;
                secondsOfDay %= 24 * 60 * 60;
                if (secondsOfDay < 0) {
                    secondsOfDay += 24 * 60 * 60;
                }
                return RestrictionTemporalValue{ 0, secondsOfDay, fractionalNanoseconds };
            }

            if (baseType == "xs:dateTime") {
                if (lexicalValue.size() < 19 || lexicalValue[4] != '-' || lexicalValue[7] != '-' || lexicalValue[10] != 'T'
                    || lexicalValue[13] != ':' || lexicalValue[16] != ':') {
                    throw XmlException("invalid temporal lexical form");
                }
                const int year = parseFixedWidthNumber(lexicalValue, 0, 4);
                const int month = parseFixedWidthNumber(lexicalValue, 5, 2);
                const int day = parseFixedWidthNumber(lexicalValue, 8, 2);
                const int hour = parseFixedWidthNumber(lexicalValue, 11, 2);
                const int minute = parseFixedWidthNumber(lexicalValue, 14, 2);
                const int second = parseFixedWidthNumber(lexicalValue, 17, 2);
                if (day < 1 || day > daysInMonth(year, month) || hour > 23 || minute > 59 || second > 59) {
                    throw XmlException("invalid temporal lexical form");
                }
                std::size_t timezoneStart = 19;
                int fractionalNanoseconds = 0;
                if (timezoneStart < lexicalValue.size() && lexicalValue[timezoneStart] == '.') {
                    const std::size_t fractionStart = timezoneStart + 1;
                    std::size_t fractionEnd = fractionStart;
                    while (fractionEnd < lexicalValue.size() && std::isdigit(static_cast<unsigned char>(lexicalValue[fractionEnd]))) {
                        ++fractionEnd;
                    }
                    fractionalNanoseconds = parseFractionalNanoseconds(lexicalValue.substr(fractionStart, fractionEnd - fractionStart));
                    timezoneStart = fractionEnd;
                }
                const int timezoneOffsetMinutes = parseTimezoneOffsetMinutes(lexicalValue, timezoneStart);
                long long absoluteDay = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
                int secondsOfDay = hour * 3600 + minute * 60 + second;
                normalizeDateTimeComponents(absoluteDay, secondsOfDay, timezoneOffsetMinutes);
                return RestrictionTemporalValue{ absoluteDay, secondsOfDay, fractionalNanoseconds };
            }

            throw XmlException("unsupported temporal base type");
        };

        if (baseRule.variety != derivedRule.variety) {
            throw XmlException("XML Schema " + label + " must preserve the base simpleType variety when using restriction");
        }

        const std::string baseFamily = simpleTypeBaseFamily(baseRule);
        const std::string derivedFamily = simpleTypeBaseFamily(derivedRule);
        if (baseFamily != derivedFamily) {
            throw XmlException("XML Schema " + label + " cannot change the base simpleType primitive when using restriction");
        }

        if (!baseRule.enumerationValues.empty()) {
            if (derivedRule.enumerationValues.empty()) {
                throw XmlException("XML Schema " + label + " cannot remove enumeration constraints from the base simpleType");
            }
            const bool subset = std::all_of(derivedRule.enumerationValues.begin(), derivedRule.enumerationValues.end(), [&](const auto& value) {
                return std::find(baseRule.enumerationValues.begin(), baseRule.enumerationValues.end(), value) != baseRule.enumerationValues.end();
            });
            if (!subset) {
                throw XmlException("XML Schema " + label + " cannot add enumeration values outside the base simpleType");
            }
        }

        if (baseRule.pattern.has_value() && derivedRule.pattern != baseRule.pattern) {
            throw XmlException("XML Schema " + label + " cannot change the base pattern facet");
        }

        if (whitespaceStrength(derivedRule.whiteSpace) < whitespaceStrength(baseRule.whiteSpace)) {
            throw XmlException("XML Schema " + label + " cannot weaken the base whiteSpace facet");
        }

        if (baseRule.length.has_value() && derivedRule.length != baseRule.length) {
            throw XmlException("XML Schema " + label + " cannot change an exact length facet from the base simpleType");
        }
        if (baseRule.minLength.has_value() && (!derivedRule.minLength.has_value() || *derivedRule.minLength < *baseRule.minLength)) {
            throw XmlException("XML Schema " + label + " cannot relax the base minLength facet");
        }
        if (baseRule.maxLength.has_value() && (!derivedRule.maxLength.has_value() || *derivedRule.maxLength > *baseRule.maxLength)) {
            throw XmlException("XML Schema " + label + " cannot relax the base maxLength facet");
        }
        if (baseRule.totalDigits.has_value() && (!derivedRule.totalDigits.has_value() || *derivedRule.totalDigits > *baseRule.totalDigits)) {
            throw XmlException("XML Schema " + label + " cannot relax the base totalDigits facet");
        }
        if (baseRule.fractionDigits.has_value() && (!derivedRule.fractionDigits.has_value() || *derivedRule.fractionDigits > *baseRule.fractionDigits)) {
            throw XmlException("XML Schema " + label + " cannot relax the base fractionDigits facet");
        }

        const bool numericFamily = IsPracticalIntegerBuiltinType(baseFamily) || baseFamily == "xs:decimal" || baseFamily == "xs:double" || baseFamily == "xs:float";
        if (numericFamily) {
            if (baseRule.minInclusive.has_value()
                && (!derivedRule.minInclusive.has_value() || compareFacetNumbers(baseFamily, *derivedRule.minInclusive, *baseRule.minInclusive) < 0)) {
                throw XmlException("XML Schema " + label + " cannot relax the base minInclusive facet");
            }
            if (baseRule.minExclusive.has_value()
                && (!derivedRule.minExclusive.has_value() || compareFacetNumbers(baseFamily, *derivedRule.minExclusive, *baseRule.minExclusive) < 0)) {
                throw XmlException("XML Schema " + label + " cannot relax the base minExclusive facet");
            }
            if (baseRule.maxInclusive.has_value()
                && (!derivedRule.maxInclusive.has_value() || compareFacetNumbers(baseFamily, *derivedRule.maxInclusive, *baseRule.maxInclusive) > 0)) {
                throw XmlException("XML Schema " + label + " cannot relax the base maxInclusive facet");
            }
            if (baseRule.maxExclusive.has_value()
                && (!derivedRule.maxExclusive.has_value() || compareFacetNumbers(baseFamily, *derivedRule.maxExclusive, *baseRule.maxExclusive) > 0)) {
                throw XmlException("XML Schema " + label + " cannot relax the base maxExclusive facet");
            }
        }

        const bool durationFamily = baseFamily == "xs:duration";
        if (durationFamily) {
            if (baseRule.minInclusive.has_value()
                && (!derivedRule.minInclusive.has_value() || ComparePracticalDurationValues(ParsePracticalDurationOrThrow(*derivedRule.minInclusive), ParsePracticalDurationOrThrow(*baseRule.minInclusive)) < 0)) {
                throw XmlException("XML Schema " + label + " cannot relax the base minInclusive facet");
            }
            if (baseRule.minExclusive.has_value()
                && (!derivedRule.minExclusive.has_value() || ComparePracticalDurationValues(ParsePracticalDurationOrThrow(*derivedRule.minExclusive), ParsePracticalDurationOrThrow(*baseRule.minExclusive)) < 0)) {
                throw XmlException("XML Schema " + label + " cannot relax the base minExclusive facet");
            }
            if (baseRule.maxInclusive.has_value()
                && (!derivedRule.maxInclusive.has_value() || ComparePracticalDurationValues(ParsePracticalDurationOrThrow(*derivedRule.maxInclusive), ParsePracticalDurationOrThrow(*baseRule.maxInclusive)) > 0)) {
                throw XmlException("XML Schema " + label + " cannot relax the base maxInclusive facet");
            }
            if (baseRule.maxExclusive.has_value()
                && (!derivedRule.maxExclusive.has_value() || ComparePracticalDurationValues(ParsePracticalDurationOrThrow(*derivedRule.maxExclusive), ParsePracticalDurationOrThrow(*baseRule.maxExclusive)) > 0)) {
                throw XmlException("XML Schema " + label + " cannot relax the base maxExclusive facet");
            }
        }

        const bool temporalFamily = baseFamily == "xs:date" || baseFamily == "xs:time" || baseFamily == "xs:dateTime"
            || baseFamily == "xs:gYear" || baseFamily == "xs:gYearMonth"
            || baseFamily == "xs:gMonth" || baseFamily == "xs:gDay" || baseFamily == "xs:gMonthDay";
        if (temporalFamily) {
            if (baseRule.minInclusive.has_value()
                && (!derivedRule.minInclusive.has_value() || compareRestrictionTemporalValues(normalizeRestrictionTemporalValue(baseFamily, *derivedRule.minInclusive), normalizeRestrictionTemporalValue(baseFamily, *baseRule.minInclusive)) < 0)) {
                throw XmlException("XML Schema " + label + " cannot relax the base minInclusive facet");
            }
            if (baseRule.minExclusive.has_value()
                && (!derivedRule.minExclusive.has_value() || compareRestrictionTemporalValues(normalizeRestrictionTemporalValue(baseFamily, *derivedRule.minExclusive), normalizeRestrictionTemporalValue(baseFamily, *baseRule.minExclusive)) < 0)) {
                throw XmlException("XML Schema " + label + " cannot relax the base minExclusive facet");
            }
            if (baseRule.maxInclusive.has_value()
                && (!derivedRule.maxInclusive.has_value() || compareRestrictionTemporalValues(normalizeRestrictionTemporalValue(baseFamily, *derivedRule.maxInclusive), normalizeRestrictionTemporalValue(baseFamily, *baseRule.maxInclusive)) > 0)) {
                throw XmlException("XML Schema " + label + " cannot relax the base maxInclusive facet");
            }
            if (baseRule.maxExclusive.has_value()
                && (!derivedRule.maxExclusive.has_value() || compareRestrictionTemporalValues(normalizeRestrictionTemporalValue(baseFamily, *derivedRule.maxExclusive), normalizeRestrictionTemporalValue(baseFamily, *baseRule.maxExclusive)) > 0)) {
                throw XmlException("XML Schema " + label + " cannot relax the base maxExclusive facet");
            }
        }
    };

    const auto validateAttributeRestrictionLegality = [&](const std::vector<AttributeUse>& baseAttributes, const std::vector<AttributeUse>& restrictedAttributes,
                                                          bool baseAnyAttributeAllowed, const std::string& baseAnyAttributeNamespaceConstraint,
                                                          bool allowNewAttributes, const std::string& label) {
        for (const auto& restrictedAttribute : restrictedAttributes) {
            const auto baseIt = std::find_if(baseAttributes.begin(), baseAttributes.end(), [&](const auto& baseAttribute) {
                return baseAttribute.name == restrictedAttribute.name && baseAttribute.namespaceUri == restrictedAttribute.namespaceUri;
            });
            if (baseIt == baseAttributes.end()) {
                if (allowNewAttributes
                    || (baseAnyAttributeAllowed && matchesWildcardNamespace(baseAnyAttributeNamespaceConstraint, restrictedAttribute.namespaceUri))) {
                    continue;
                }
                throw XmlException("XML Schema " + label + " cannot add attribute '" + restrictedAttribute.name + "' that is absent from the base type");
            }
            if (baseIt->required && !restrictedAttribute.required) {
                throw XmlException("XML Schema " + label + " cannot relax required attribute '" + restrictedAttribute.name + "'");
            }
            if (baseIt->fixedValue.has_value()) {
                if (!restrictedAttribute.fixedValue.has_value()) {
                    throw XmlException("XML Schema " + label + " cannot remove fixed value from inherited attribute '" + restrictedAttribute.name + "'");
                }
                if (*restrictedAttribute.fixedValue != *baseIt->fixedValue) {
                    throw XmlException("XML Schema " + label + " cannot change fixed value of inherited attribute '" + restrictedAttribute.name + "'");
                }
            }
            if (baseIt->defaultValue.has_value()) {
                if (restrictedAttribute.fixedValue.has_value()) {
                    if (*restrictedAttribute.fixedValue != *baseIt->defaultValue) {
                        throw XmlException("XML Schema " + label + " cannot change default value of inherited attribute '" + restrictedAttribute.name + "'");
                    }
                } else {
                    if (!restrictedAttribute.defaultValue.has_value()) {
                        throw XmlException("XML Schema " + label + " cannot remove default value from inherited attribute '" + restrictedAttribute.name + "'");
                    }
                    if (*restrictedAttribute.defaultValue != *baseIt->defaultValue) {
                        throw XmlException("XML Schema " + label + " cannot change default value of inherited attribute '" + restrictedAttribute.name + "'");
                    }
                }
            }
            if (baseIt->type.has_value()) {
                if (!restrictedAttribute.type.has_value()) {
                    throw XmlException("XML Schema " + label + " must preserve the type of inherited attribute '" + restrictedAttribute.name + "'");
                }
                validateDerivedSimpleTypeRestriction(*baseIt->type, *restrictedAttribute.type,
                    label + " attribute '" + restrictedAttribute.name + "'");
            }
        }

        for (const auto& baseAttribute : baseAttributes) {
            if (!baseAttribute.required) {
                continue;
            }
            const bool stillPresent = std::any_of(restrictedAttributes.begin(), restrictedAttributes.end(), [&](const auto& restrictedAttribute) {
                return restrictedAttribute.name == baseAttribute.name
                    && restrictedAttribute.namespaceUri == baseAttribute.namespaceUri
                    && restrictedAttribute.required;
            });
            if (!stillPresent) {
                throw XmlException("XML Schema " + label + " cannot remove required attribute '" + baseAttribute.name + "'");
            }
        }
    };

    const auto processContentsStrength = [](const std::string& processContents) {
        if (processContents == "skip") {
            return 0;
        }
        if (processContents == "lax") {
            return 1;
        }
        return 2;
    };

    const auto wildcardConstraintIsSubsetOf = [&](const std::string& derivedConstraint, const std::string& baseConstraint) {
        const std::string normalizedDerived = derivedConstraint.empty() ? "##any" : derivedConstraint;
        const std::string normalizedBase = baseConstraint.empty() ? "##any" : baseConstraint;
        if (normalizedBase == "##any") {
            return true;
        }
        if (normalizedDerived == normalizedBase) {
            return true;
        }
        std::istringstream derivedTokens(normalizedDerived);
        std::string derivedToken;
        while (derivedTokens >> derivedToken) {
            if (!wildcardTokenIsSubsetOfConstraint(derivedToken, normalizedBase)) {
                return false;
            }
        }
        return true;
    };

    const auto validateAnyAttributeRestrictionLegality = [&](bool baseAllowed, const std::string& baseConstraint, const std::string& baseProcessContents,
                                                            bool derivedAllowed, const std::string& derivedConstraint, const std::string& derivedProcessContents,
                                                            bool allowNewWildcard, const std::string& label) {
        if (!derivedAllowed) {
            return;
        }
        if (!baseAllowed) {
            if (allowNewWildcard) {
                return;
            }
            throw XmlException("XML Schema " + label + " cannot add an anyAttribute wildcard that is absent from the base type");
        }
        if (!wildcardConstraintIsSubsetOf(derivedConstraint, baseConstraint)) {
            throw XmlException("XML Schema " + label + " cannot widen the base anyAttribute namespace constraint");
        }
        if (processContentsStrength(derivedProcessContents) < processContentsStrength(baseProcessContents)) {
            throw XmlException("XML Schema " + label + " cannot weaken the base anyAttribute processContents");
        }
    };

    std::function<void(const Particle&, const Particle&, const std::string&)> validateDerivedParticleRestriction;
    std::function<bool(const Particle&, const Particle&)> particleShapesCanCorrespond;
    std::function<void(const ComplexTypeRule&, const ComplexTypeRule&, const std::string&)> validateAnonymousComplexTypeRestriction;
    std::function<bool(const ComplexTypeRule&, const ComplexTypeRule&)> anonymousComplexTypesAreEquivalent;

    const auto unwrapEquivalentParticle = [](const Particle& particle) -> const Particle* {
        const Particle* current = &particle;
        while (current->minOccurs == 1
            && current->maxOccurs == 1
            && (current->kind == Particle::Kind::Sequence || current->kind == Particle::Kind::Choice)
            && current->children.size() == 1) {
            current = &current->children.front();
        }
        return current;
    };

    particleShapesCanCorrespond = [&](const Particle& baseParticle, const Particle& derivedParticle) {
        const Particle& normalizedBase = *unwrapEquivalentParticle(baseParticle);
        const Particle& normalizedDerived = *unwrapEquivalentParticle(derivedParticle);

        if (normalizedBase.kind == Particle::Kind::Choice) {
            return std::any_of(normalizedBase.children.begin(), normalizedBase.children.end(), [&](const auto& baseChild) {
                return particleShapesCanCorrespond(baseChild, derivedParticle);
            });
        }
        if (normalizedBase.kind != normalizedDerived.kind) {
            return false;
        }
        if (normalizedBase.kind == Particle::Kind::Element) {
            return normalizedBase.name == normalizedDerived.name && normalizedBase.namespaceUri == normalizedDerived.namespaceUri;
        }
        return true;
    };

    const auto choiceBranchCanBeReused = [&](const Particle& particle) {
        return unwrapEquivalentParticle(particle)->kind == Particle::Kind::Choice;
    };

    std::function<bool(const SimpleTypeRule&, const SimpleTypeRule&, bool&)> simpleTypeDerivesFrom;
    std::function<bool(const ComplexTypeRule&, const ComplexTypeRule&, bool&, bool&)> complexTypeDerivesFrom;

    const auto elementParticleTypeCanRestrictBase = [&](const Particle& baseParticle,
        const Particle& derivedParticle,
        bool& usesRestriction,
        bool& usesExtension,
        std::string& detailError) {
        usesRestriction = false;
        usesExtension = false;
        detailError.clear();
        if (baseParticle.elementComplexType) {
            if (!derivedParticle.elementComplexType) {
                return false;
            }
            if (baseParticle.elementComplexType->namedTypeName.empty()) {
                try {
                    validateAnonymousComplexTypeRestriction(*baseParticle.elementComplexType, *derivedParticle.elementComplexType,
                        "child element '" + baseParticle.name + "'");
                } catch (const XmlException& exception) {
                    detailError = exception.what();
                    return false;
                }
                usesRestriction = !anonymousComplexTypesAreEquivalent(*baseParticle.elementComplexType, *derivedParticle.elementComplexType);
                return true;
            }
            if (!complexTypeDerivesFrom(*derivedParticle.elementComplexType, *baseParticle.elementComplexType, usesRestriction, usesExtension)) {
                return false;
            }
            return !usesExtension;
        }
        if (baseParticle.elementSimpleType.has_value()) {
            if (!derivedParticle.elementSimpleType.has_value()) {
                return false;
            }
            return simpleTypeDerivesFrom(*derivedParticle.elementSimpleType, *baseParticle.elementSimpleType, usesRestriction);
        }
        return true;
    };

    const auto elementParticleDeclarationCanRestrictBase = [&](const Particle& baseParticle,
        const Particle& derivedParticle,
        std::string& error) {
        if (derivedParticle.elementIsNillable && !baseParticle.elementIsNillable) {
            error = "XML Schema child element restriction cannot make a base child element nillable";
            return false;
        }

        if (baseParticle.elementFixedValue.has_value()) {
            if (!derivedParticle.elementFixedValue.has_value()) {
                error = "XML Schema child element restriction cannot remove the base child element fixed value";
                return false;
            }
            if (*derivedParticle.elementFixedValue != *baseParticle.elementFixedValue) {
                error = "XML Schema child element restriction cannot change the base child element fixed value";
                return false;
            }
        }

        if (baseParticle.elementDefaultValue.has_value()) {
            if (derivedParticle.elementFixedValue.has_value()) {
                if (*derivedParticle.elementFixedValue != *baseParticle.elementDefaultValue) {
                    error = "XML Schema child element restriction cannot change the base child element default value";
                    return false;
                }
            } else {
                if (!derivedParticle.elementDefaultValue.has_value()) {
                    error = "XML Schema child element restriction cannot remove the base child element default value";
                    return false;
                }
                if (*derivedParticle.elementDefaultValue != *baseParticle.elementDefaultValue) {
                    error = "XML Schema child element restriction cannot change the base child element default value";
                    return false;
                }
            }
        }

        return true;
    };

    validateDerivedParticleRestriction = [&](const Particle& baseParticle, const Particle& derivedParticle, const std::string& label) {
        const Particle* normalizedBase = &baseParticle;
        const Particle* normalizedDerived = &derivedParticle;
        if (baseParticle.kind != derivedParticle.kind) {
            normalizedBase = unwrapEquivalentParticle(baseParticle);
            normalizedDerived = unwrapEquivalentParticle(derivedParticle);
        }

        if (normalizedBase->kind == Particle::Kind::Choice && normalizedDerived->kind != Particle::Kind::Choice) {
            std::string lastChoiceSelectionError;
            bool foundShapeCandidate = false;
            for (std::size_t index = 0; index < normalizedBase->children.size(); ++index) {
                if (!particleShapesCanCorrespond(normalizedBase->children[index], *normalizedDerived)) {
                    continue;
                }
                foundShapeCandidate = true;
                try {
                    if (normalizedDerived->minOccurs < normalizedBase->minOccurs) {
                        throw XmlException("XML Schema " + label + " cannot relax the base minOccurs");
                    }
                    if (normalizedDerived->maxOccurs > normalizedBase->maxOccurs) {
                        throw XmlException("XML Schema " + label + " cannot relax the base maxOccurs");
                    }
                    validateDerivedParticleRestriction(normalizedBase->children[index], *normalizedDerived, label + " choice selection");
                    return;
                } catch (const XmlException& exception) {
                    lastChoiceSelectionError = exception.what();
                }
            }
            if (!foundShapeCandidate) {
                throw XmlException("XML Schema " + label + " cannot select a branch outside the base choice");
            }
            if (!lastChoiceSelectionError.empty()) {
                throw XmlException(lastChoiceSelectionError);
            }
            throw XmlException("XML Schema " + label + " cannot select a branch outside the base choice");
        }

        if (normalizedBase->kind == Particle::Kind::Any) {
            if (normalizedDerived->minOccurs < normalizedBase->minOccurs) {
                throw XmlException("XML Schema " + label + " cannot relax the base minOccurs");
            }
            if (normalizedDerived->maxOccurs > normalizedBase->maxOccurs) {
                throw XmlException("XML Schema " + label + " cannot relax the base maxOccurs");
            }
            if (normalizedDerived->kind == Particle::Kind::Any) {
                if (!wildcardConstraintIsSubsetOf(normalizedDerived->namespaceUri, normalizedBase->namespaceUri)) {
                    throw XmlException("XML Schema " + label + " cannot widen the base wildcard namespace constraint");
                }
                if (processContentsStrength(normalizedDerived->processContents) < processContentsStrength(normalizedBase->processContents)) {
                    throw XmlException("XML Schema " + label + " cannot weaken the base wildcard processContents");
                }
            }
            return;
        }

        if (normalizedBase->kind != normalizedDerived->kind) {
            throw XmlException("XML Schema " + label + " must preserve the base particle kind when using restriction");
        }
        if (normalizedDerived->minOccurs < normalizedBase->minOccurs) {
            throw XmlException("XML Schema " + label + " cannot relax the base minOccurs");
        }
        if (normalizedDerived->maxOccurs > normalizedBase->maxOccurs) {
            throw XmlException("XML Schema " + label + " cannot relax the base maxOccurs");
        }

        if (normalizedBase->kind == Particle::Kind::Element) {
            if (normalizedBase->name != normalizedDerived->name || normalizedBase->namespaceUri != normalizedDerived->namespaceUri) {
                throw XmlException("XML Schema " + label + " cannot replace a base child element with a different element");
            }
            bool usesRestriction = false;
            bool usesExtension = false;
            std::string elementTypeError;
            if (!elementParticleTypeCanRestrictBase(*normalizedBase, *normalizedDerived, usesRestriction, usesExtension, elementTypeError)) {
                if (!elementTypeError.empty()) {
                    throw XmlException(elementTypeError);
                }
                throw XmlException("XML Schema " + label + " cannot replace a base child element with an incompatible element type");
            }
            if (usesRestriction) {
                if (normalizedBase->elementBlockRestriction) {
                    throw XmlException("XML Schema " + label + " cannot use a child element type restriction because the base element declaration blocks restriction derivation");
                }
                if (normalizedBase->elementFinalRestriction) {
                    throw XmlException("XML Schema " + label + " cannot use a child element type restriction because the base element declaration final blocks restriction derivation");
                }
            }
            if (usesExtension) {
                if (normalizedBase->elementBlockExtension) {
                    throw XmlException("XML Schema " + label + " cannot use a child element type extension because the base element declaration blocks extension derivation");
                }
                if (normalizedBase->elementFinalExtension) {
                    throw XmlException("XML Schema " + label + " cannot use a child element type extension because the base element declaration final blocks extension derivation");
                }
            }
            std::string declarationRestrictionError;
            if (!elementParticleDeclarationCanRestrictBase(*normalizedBase, *normalizedDerived, declarationRestrictionError)) {
                throw XmlException(declarationRestrictionError);
            }
            return;
        }

        if (normalizedBase->kind == Particle::Kind::Choice) {
            std::vector<bool> used(normalizedBase->children.size(), false);
            std::string lastChoiceError;
            std::function<bool(std::size_t, std::vector<bool>&)> tryMatchChoiceBranches;

            tryMatchChoiceBranches = [&](std::size_t derivedIndex, std::vector<bool>& currentUsed) {
                if (derivedIndex >= normalizedDerived->children.size()) {
                    return true;
                }

                const Particle& currentDerivedChild = normalizedDerived->children[derivedIndex];
                bool foundShapeCandidate = false;
                for (std::size_t baseIndex = 0; baseIndex < normalizedBase->children.size(); ++baseIndex) {
                    if ((currentUsed[baseIndex] && !choiceBranchCanBeReused(normalizedBase->children[baseIndex]))
                        || !particleShapesCanCorrespond(normalizedBase->children[baseIndex], currentDerivedChild)) {
                        continue;
                    }

                    foundShapeCandidate = true;
                    std::vector<bool> nextUsed = currentUsed;
                    if (!choiceBranchCanBeReused(normalizedBase->children[baseIndex])) {
                        nextUsed[baseIndex] = true;
                    }

                    try {
                        validateDerivedParticleRestriction(
                            normalizedBase->children[baseIndex],
                            currentDerivedChild,
                            label + " choice branch");
                    } catch (const XmlException& exception) {
                        lastChoiceError = exception.what();
                        continue;
                    }

                    if (tryMatchChoiceBranches(derivedIndex + 1, nextUsed)) {
                        currentUsed = std::move(nextUsed);
                        return true;
                    }
                }

                if (!foundShapeCandidate) {
                    lastChoiceError = "XML Schema " + label + " cannot add a new choice branch that is absent from the base type";
                }
                return false;
            };

            if (!tryMatchChoiceBranches(0, used)) {
                if (!lastChoiceError.empty()) {
                    throw XmlException(lastChoiceError);
                }
                throw XmlException("XML Schema " + label + " cannot add a new choice branch that is absent from the base type");
            }
            return;
        }

        if (normalizedBase->kind == Particle::Kind::All) {
            std::vector<bool> used(normalizedBase->children.size(), false);
            std::string lastAllError;
            std::function<bool(std::size_t, std::vector<bool>&)> tryMatchAllParticles;

            tryMatchAllParticles = [&](std::size_t derivedIndex, std::vector<bool>& currentUsed) {
                if (derivedIndex >= normalizedDerived->children.size()) {
                    for (std::size_t baseIndex = 0; baseIndex < normalizedBase->children.size(); ++baseIndex) {
                        if (!currentUsed[baseIndex] && normalizedBase->children[baseIndex].minOccurs > 0) {
                            lastAllError = "XML Schema " + label + " cannot omit required base all particles";
                            return false;
                        }
                    }
                    return true;
                }

                const Particle& currentDerivedChild = normalizedDerived->children[derivedIndex];
                bool foundShapeCandidate = false;
                for (std::size_t baseIndex = 0; baseIndex < normalizedBase->children.size(); ++baseIndex) {
                    if (currentUsed[baseIndex]
                        || !particleShapesCanCorrespond(normalizedBase->children[baseIndex], currentDerivedChild)) {
                        continue;
                    }

                    foundShapeCandidate = true;
                    std::vector<bool> nextUsed = currentUsed;
                    nextUsed[baseIndex] = true;

                    try {
                        validateDerivedParticleRestriction(
                            normalizedBase->children[baseIndex],
                            currentDerivedChild,
                            label + " all particle");
                    } catch (const XmlException& exception) {
                        lastAllError = exception.what();
                        continue;
                    }

                    if (tryMatchAllParticles(derivedIndex + 1, nextUsed)) {
                        currentUsed = std::move(nextUsed);
                        return true;
                    }
                }

                if (!foundShapeCandidate && lastAllError.empty()) {
                    lastAllError = "XML Schema " + label + " cannot add a new all particle that is absent from the base type";
                }
                return false;
            };

            if (!tryMatchAllParticles(0, used)) {
                if (!lastAllError.empty()) {
                    throw XmlException(lastAllError);
                }
                throw XmlException("XML Schema " + label + " cannot add a new all particle that is absent from the base type");
            }
            return;
        }

        std::string lastSequenceError;
        std::function<bool(std::size_t, std::size_t)> tryMatchSequenceParticles;

        tryMatchSequenceParticles = [&](std::size_t derivedIndex, std::size_t baseIndex) {
            if (derivedIndex >= normalizedDerived->children.size()) {
                for (std::size_t remainingBaseIndex = baseIndex; remainingBaseIndex < normalizedBase->children.size(); ++remainingBaseIndex) {
                    if (normalizedBase->children[remainingBaseIndex].minOccurs > 0) {
                        lastSequenceError = "XML Schema " + label + " cannot omit required base sequence particles";
                        return false;
                    }
                }
                return true;
            }

            const Particle& currentDerivedChild = normalizedDerived->children[derivedIndex];
            bool foundShapeCandidate = false;
            for (std::size_t candidateBaseIndex = baseIndex; candidateBaseIndex < normalizedBase->children.size(); ++candidateBaseIndex) {
                const Particle& candidateBaseChild = normalizedBase->children[candidateBaseIndex];
                bool skippedRequired = false;
                for (std::size_t skippedIndex = baseIndex; skippedIndex < candidateBaseIndex; ++skippedIndex) {
                    if (normalizedBase->children[skippedIndex].minOccurs > 0) {
                        skippedRequired = true;
                        break;
                    }
                }
                if (skippedRequired) {
                    lastSequenceError = "XML Schema " + label + " cannot omit or reorder required base sequence particles";
                    break;
                }

                const Particle& normalizedCandidateBaseChild = *unwrapEquivalentParticle(candidateBaseChild);
                const bool canTryDerivedSlice = (normalizedCandidateBaseChild.kind == Particle::Kind::Sequence
                    || normalizedCandidateBaseChild.kind == Particle::Kind::Choice)
                    && currentDerivedChild.kind != Particle::Kind::Sequence;

                auto trySequenceCandidate = [&](const Particle& derivedCandidate, std::size_t consumedDerivedCount) {
                    foundShapeCandidate = true;
                    try {
                        validateDerivedParticleRestriction(
                            candidateBaseChild,
                            derivedCandidate,
                            label + " sequence particle");
                    } catch (const XmlException& exception) {
                        lastSequenceError = exception.what();
                        return false;
                    }

                    return tryMatchSequenceParticles(derivedIndex + consumedDerivedCount, candidateBaseIndex + 1);
                };

                if (particleShapesCanCorrespond(candidateBaseChild, currentDerivedChild)
                    && trySequenceCandidate(currentDerivedChild, 1)) {
                    return true;
                }

                if (canTryDerivedSlice) {
                    const std::size_t maxSliceLength = normalizedDerived->children.size() - derivedIndex;
                    for (std::size_t sliceLength = 2; sliceLength <= maxSliceLength; ++sliceLength) {
                        Particle derivedSlice;
                        derivedSlice.kind = Particle::Kind::Sequence;
                        for (std::size_t sliceIndex = 0; sliceIndex < sliceLength; ++sliceIndex) {
                            derivedSlice.children.push_back(normalizedDerived->children[derivedIndex + sliceIndex]);
                        }
                        if (trySequenceCandidate(derivedSlice, sliceLength)) {
                            return true;
                        }
                    }
                }
            }

            if (!foundShapeCandidate && lastSequenceError.empty()) {
                lastSequenceError = "XML Schema " + label + " cannot add a new sequence particle that is absent from the base type";
            }
            return false;
        };

        if (!tryMatchSequenceParticles(0, 0)) {
            if (!lastSequenceError.empty()) {
                throw XmlException(lastSequenceError);
            }
            throw XmlException("XML Schema " + label + " cannot add a new sequence particle that is absent from the base type");
        }
    };

    validateAnonymousComplexTypeRestriction = [&](const ComplexTypeRule& baseRule,
        const ComplexTypeRule& derivedRule,
        const std::string& label) {
        if (baseRule.allowsText != derivedRule.allowsText) {
            throw XmlException("XML Schema " + label + " must preserve the base complexType text/content shape when using restriction");
        }

        if (baseRule.textType.has_value()) {
            if (!derivedRule.textType.has_value()) {
                throw XmlException("XML Schema " + label + " must preserve the base complexType text type when using restriction");
            }
            validateDerivedSimpleTypeRestriction(*baseRule.textType, *derivedRule.textType, label + " text content");
        } else if (derivedRule.textType.has_value()) {
            throw XmlException("XML Schema " + label + " cannot introduce text content absent from the base complexType");
        }

        const std::optional<Particle> baseParticle = makeFlatParticleFromRule(baseRule);
        const std::optional<Particle> derivedParticle = makeFlatParticleFromRule(derivedRule);
        if (derivedParticle.has_value()) {
            if (!baseParticle.has_value()) {
                throw XmlException("XML Schema " + label + " cannot introduce child particles when the base complexType has no element content");
            }
            validateDerivedParticleRestriction(*baseParticle, *derivedParticle, label);
        } else if (baseParticle.has_value()) {
            throw XmlException("XML Schema " + label + " cannot remove child particles from the base complexType");
        }

        validateAttributeRestrictionLegality(
            baseRule.attributes,
            derivedRule.attributes,
            baseRule.anyAttributeAllowed,
            baseRule.anyAttributeNamespaceConstraint,
            false,
            label);
        validateAnyAttributeRestrictionLegality(
            baseRule.anyAttributeAllowed,
            baseRule.anyAttributeNamespaceConstraint,
            baseRule.anyAttributeProcessContents,
            derivedRule.anyAttributeAllowed,
            derivedRule.anyAttributeNamespaceConstraint,
            derivedRule.anyAttributeProcessContents,
            false,
            label);
    };

    anonymousComplexTypesAreEquivalent = [&](const ComplexTypeRule& leftRule, const ComplexTypeRule& rightRule) {
        try {
            validateAnonymousComplexTypeRestriction(leftRule, rightRule, "anonymous complexType equivalence");
            validateAnonymousComplexTypeRestriction(rightRule, leftRule, "anonymous complexType equivalence");
            return true;
        } catch (const XmlException&) {
            return false;
        }
    };

    const auto mergeComplexContent = [&](ComplexTypeRule& destination, const std::optional<Particle>& extensionParticle) {
        if (!extensionParticle.has_value()) {
            return;
        }

        const auto baseParticle = makeFlatParticleFromRule(destination);
        if (!baseParticle.has_value()) {
            destination.particle = extensionParticle;
            destination.children.clear();
            destination.contentModel = ContentModel::Empty;
            return;
        }

        Particle merged;
        merged.kind = Particle::Kind::Sequence;
        merged.children.push_back(*baseParticle);
        merged.children.push_back(*extensionParticle);
        destination.particle = merged;
        destination.children.clear();
        destination.contentModel = ContentModel::Empty;
    };

    const auto applyAttributeChildrenToRule = [&](ComplexTypeRule& rule, const XmlElement& parentElement) {
        for (const auto& child : parentElement.ChildNodes()) {
            if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                continue;
            }
            const auto* childElement = static_cast<const XmlElement*>(child.get());
            if (childElement->NamespaceURI() != kSchemaNamespace) {
                continue;
            }

            if (childElement->LocalName() == "attribute") {
                rule.attributes.push_back(parseAttributeUse(*childElement, false));
            } else if (childElement->LocalName() == "attributeGroup") {
                const std::string attributeGroupRef = childElement->GetAttribute("ref");
                if (attributeGroupRef.empty()) {
                    throw XmlException("XML Schema attributeGroup references require a ref");
                }
                const auto [localName, namespaceUri] = resolveTypeName(*childElement, attributeGroupRef);
                const auto referencedAttributes = ensureAttributeGroupResolved(localName, namespaceUri, attributeGroupRef);
                appendAttributes(rule.attributes, referencedAttributes);
                mergeAnyAttribute(
                    rule.anyAttributeAllowed,
                    rule.anyAttributeNamespaceConstraint,
                    rule.anyAttributeProcessContents,
                    resolvedAttributeGroupAnyAllowed[makeQualifiedRuleKey(localName, namespaceUri)],
                    resolvedAttributeGroupAnyNamespace[makeQualifiedRuleKey(localName, namespaceUri)],
                    resolvedAttributeGroupAnyProcessContents[makeQualifiedRuleKey(localName, namespaceUri)]);
            } else if (childElement->LocalName() == "anyAttribute") {
                rule.anyAttributeAllowed = true;
                rule.anyAttributeNamespaceConstraint = childElement->GetAttribute("namespace");
                if (rule.anyAttributeNamespaceConstraint.empty()) {
                    rule.anyAttributeNamespaceConstraint = "##any";
                }
                rule.anyAttributeProcessContents = parseProcessContentsValue(*childElement);
            }
        }
    };

    parseAttributeUse = [&](const XmlElement& attributeElement, bool isGlobalDeclaration) -> AttributeUse {
        const std::string attributeRef = attributeElement.GetAttribute("ref");
        if (!attributeRef.empty()) {
            std::string_view defaultValueView;
            std::string_view fixedValueView;
            if ((TryGetAttributeValueViewInternal(attributeElement, "default", defaultValueView) && !defaultValueView.empty())
                || (TryGetAttributeValueViewInternal(attributeElement, "fixed", fixedValueView) && !fixedValueView.empty())) {
                throw XmlException("XML Schema attribute references cannot specify default or fixed values");
            }
            const auto [localName, namespaceUri] = resolveTypeName(attributeElement, attributeRef);
            AttributeUse referenced = ensureAttributeResolved(localName, namespaceUri, attributeRef);
            const std::string use = parseAttributeUseValue(attributeElement);
            if (!use.empty()) {
                referenced.required = use == "required";
            }
            return referenced;
        }

        AttributeUse attributeUse;
        attributeUse.annotation = parseAnnotation(attributeElement);
        attributeUse.name = attributeElement.GetAttribute("name");
        attributeUse.namespaceUri = resolveAttributeDeclarationNamespace(attributeElement, isGlobalDeclaration);
        attributeUse.required = parseAttributeUseValue(attributeElement) == "required";
        const std::string defaultValue = attributeElement.GetAttribute("default");
        const std::string fixedValue = attributeElement.GetAttribute("fixed");
        if (!defaultValue.empty() && !fixedValue.empty()) {
            throw XmlException("XML Schema attribute declarations cannot specify both default and fixed values");
        }
        if (attributeUse.required && !defaultValue.empty()) {
            throw XmlException("XML Schema required attributes cannot specify a default value");
        }
        if (!defaultValue.empty()) {
            attributeUse.defaultValue = defaultValue;
        }
        if (!fixedValue.empty()) {
            attributeUse.fixedValue = fixedValue;
        }
        const std::string attributeType = attributeElement.GetAttribute("type");
        if (!attributeType.empty()) {
            if (const auto builtinType = resolveBuiltinSimpleType(attributeType); builtinType.has_value()) {
                attributeUse.type = builtinType;
            } else {
                const auto [localTypeName, typeNamespaceUri] = resolveTypeName(attributeElement, attributeType);
                attributeUse.type = ensureSimpleTypeResolved(localTypeName, typeNamespaceUri, attributeType);
            }
        }
        if (attributeUse.name.empty()) {
            throw XmlException("XML Schema attribute declarations require a name or ref");
        }
        for (const auto& attributeChild : attributeElement.ChildNodes()) {
            if (attributeChild == nullptr || attributeChild->NodeType() != XmlNodeType::Element) {
                continue;
            }
            const auto* attributeChildElement = static_cast<const XmlElement*>(attributeChild.get());
            if (attributeChildElement->LocalName() == "simpleType" && attributeChildElement->NamespaceURI() == kSchemaNamespace) {
                attributeUse.type = parseSimpleType(*attributeChildElement);
            }
        }
        return attributeUse;
    };

    parseAttributeGroup = [&](const XmlElement& attributeGroupElement, bool& anyAttributeAllowed, std::string& anyAttributeNamespaceConstraint, std::string& anyAttributeProcessContents) -> std::vector<AttributeUse> {
        std::vector<AttributeUse> attributes;
        for (const auto& child : attributeGroupElement.ChildNodes()) {
            if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                continue;
            }
            const auto* childElement = static_cast<const XmlElement*>(child.get());
            if (childElement->NamespaceURI() != kSchemaNamespace) {
                continue;
            }
            if (childElement->LocalName() == "attribute") {
                attributes.push_back(parseAttributeUse(*childElement, false));
            } else if (childElement->LocalName() == "attributeGroup") {
                const std::string attributeGroupRef = childElement->GetAttribute("ref");
                if (attributeGroupRef.empty()) {
                    throw XmlException("XML Schema attributeGroup references require a ref");
                }
                const auto [localName, namespaceUri] = resolveTypeName(*childElement, attributeGroupRef);
                const auto referencedAttributes = ensureAttributeGroupResolved(localName, namespaceUri, attributeGroupRef);
                appendAttributes(attributes, referencedAttributes);
                mergeAnyAttribute(
                    anyAttributeAllowed,
                    anyAttributeNamespaceConstraint,
                    anyAttributeProcessContents,
                    resolvedAttributeGroupAnyAllowed[makeQualifiedRuleKey(localName, namespaceUri)],
                    resolvedAttributeGroupAnyNamespace[makeQualifiedRuleKey(localName, namespaceUri)],
                    resolvedAttributeGroupAnyProcessContents[makeQualifiedRuleKey(localName, namespaceUri)]);
            } else if (childElement->LocalName() == "anyAttribute") {
                anyAttributeAllowed = true;
                anyAttributeNamespaceConstraint = childElement->GetAttribute("namespace");
                if (anyAttributeNamespaceConstraint.empty()) {
                    anyAttributeNamespaceConstraint = "##any";
                }
                anyAttributeProcessContents = parseProcessContentsValue(*childElement);
            }
        }
        return attributes;
    };

    const auto validateAllChildParticleOccurs = [&](const XmlElement& childElement) {
        const std::string childLocalName = childElement.LocalName();
        if (childLocalName != "element" && childLocalName != "group") {
            throw XmlException("XML Schema xs:all currently supports only child element particles and group references");
        }

        const std::size_t childMinOccurs = parseOccurs(childElement.GetAttribute("minOccurs"), 1);
        const std::size_t childMaxOccurs = parseOccurs(childElement.GetAttribute("maxOccurs"), 1);
        if (childLocalName == "element") {
            if (childMaxOccurs > 1) {
                throw XmlException("XML Schema xs:all child elements currently support maxOccurs up to 1");
            }
            if (childMinOccurs > 1) {
                throw XmlException("XML Schema xs:all child elements currently support minOccurs up to 1");
            }
            return;
        }

        if (childMaxOccurs > 1) {
            throw XmlException("XML Schema xs:all group references currently support maxOccurs up to 1");
        }
        if (childMinOccurs > 1) {
            throw XmlException("XML Schema xs:all group references currently support minOccurs up to 1");
        }
    };

    const auto validateAllChildParticleShape = [&](const XmlElement& childElement, const Particle& childParticle) {
        if (childElement.LocalName() == "group" && childParticle.kind != Particle::Kind::All) {
            throw XmlException("XML Schema xs:all group references currently support only groups that resolve to xs:all");
        }
    };

    const auto applyAttributeRestrictionsToRule = [&](ComplexTypeRule& rule, const XmlElement& parentElement, bool allowNewAttributes, const std::string& label) {
        const std::vector<AttributeUse> baseAttributes = rule.attributes;
        std::vector<AttributeUse> restrictedAttributes = rule.attributes;
        const bool baseAnyAttributeAllowed = rule.anyAttributeAllowed;
        const std::string baseAnyAttributeNamespaceConstraint = rule.anyAttributeNamespaceConstraint;
        const std::string baseAnyAttributeProcessContents = rule.anyAttributeProcessContents;
        bool anyAttributeAllowed = rule.anyAttributeAllowed;
        std::string anyAttributeNamespaceConstraint = rule.anyAttributeNamespaceConstraint;
        std::string anyAttributeProcessContents = rule.anyAttributeProcessContents;

        for (const auto& child : parentElement.ChildNodes()) {
            if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                continue;
            }
            const auto* childElement = static_cast<const XmlElement*>(child.get());
            if (childElement->NamespaceURI() != kSchemaNamespace) {
                continue;
            }

            if (childElement->LocalName() == "attribute") {
                if (AttributeValueEqualsInternal(*childElement, "use", "prohibited")) {
                    std::string name = childElement->GetAttribute("name");
                    std::string namespaceUri;
                    const std::string attributeRef = childElement->GetAttribute("ref");
                    if (!attributeRef.empty()) {
                        const auto resolved = resolveTypeName(*childElement, attributeRef);
                        name = resolved.first;
                        namespaceUri = resolved.second;
                    }
                    if (name.empty()) {
                        throw XmlException("XML Schema prohibited attribute restrictions require a name or ref");
                    }
                    eraseAttributeUse(restrictedAttributes, name, namespaceUri);
                    continue;
                }

                upsertAttributeUse(restrictedAttributes, parseAttributeUse(*childElement, false));
            } else if (childElement->LocalName() == "attributeGroup") {
                const std::string attributeGroupRef = childElement->GetAttribute("ref");
                if (attributeGroupRef.empty()) {
                    throw XmlException("XML Schema attributeGroup references require a ref");
                }
                const auto [localName, namespaceUri] = resolveTypeName(*childElement, attributeGroupRef);
                const auto referencedAttributes = ensureAttributeGroupResolved(localName, namespaceUri, attributeGroupRef);
                for (const auto& referencedAttribute : referencedAttributes) {
                    upsertAttributeUse(restrictedAttributes, referencedAttribute);
                }
                const std::string ruleKey = makeQualifiedRuleKey(localName, namespaceUri);
                mergeAnyAttribute(
                    anyAttributeAllowed,
                    anyAttributeNamespaceConstraint,
                    anyAttributeProcessContents,
                    resolvedAttributeGroupAnyAllowed[ruleKey],
                    resolvedAttributeGroupAnyNamespace[ruleKey],
                    resolvedAttributeGroupAnyProcessContents[ruleKey]);
            } else if (childElement->LocalName() == "anyAttribute") {
                anyAttributeAllowed = true;
                anyAttributeNamespaceConstraint = childElement->GetAttribute("namespace");
                if (anyAttributeNamespaceConstraint.empty()) {
                    anyAttributeNamespaceConstraint = "##any";
                }
                anyAttributeProcessContents = parseProcessContentsValue(*childElement);
            }
        }

        validateAttributeRestrictionLegality(
            baseAttributes,
            restrictedAttributes,
            baseAnyAttributeAllowed,
            baseAnyAttributeNamespaceConstraint,
            allowNewAttributes,
            label);
        validateAnyAttributeRestrictionLegality(
            baseAnyAttributeAllowed,
            baseAnyAttributeNamespaceConstraint,
            baseAnyAttributeProcessContents,
            anyAttributeAllowed,
            anyAttributeNamespaceConstraint,
            anyAttributeProcessContents,
            allowNewAttributes,
            label);
        rule.attributes = std::move(restrictedAttributes);
        rule.anyAttributeAllowed = anyAttributeAllowed;
        rule.anyAttributeNamespaceConstraint = anyAttributeNamespaceConstraint;
        rule.anyAttributeProcessContents = anyAttributeProcessContents;
    };

    parseParticle = [&](const XmlElement& particleElement) -> Particle {
        Particle particle;
        particle.minOccurs = parseOccurs(particleElement.GetAttribute("minOccurs"), 1);
        particle.maxOccurs = parseOccurs(particleElement.GetAttribute("maxOccurs"), 1);

        if (particleElement.LocalName() == "element") {
            validateParticleOccursRange(particle.minOccurs, particle.maxOccurs);
            particle.kind = Particle::Kind::Element;
            const std::string elementRef = particleElement.GetAttribute("ref");
            if (!elementRef.empty()) {
                const auto [localName, namespaceUri] = resolveTypeName(particleElement, elementRef);
                const ElementRule referencedRule = ensureElementResolved(localName, namespaceUri, elementRef);
                particle.name = referencedRule.name;
                particle.namespaceUri = referencedRule.namespaceUri;
                particle.elementSimpleType = referencedRule.declaredSimpleType;
                if (referencedRule.declaredComplexType.has_value()) {
                    particle.elementComplexType = std::make_shared<ComplexTypeRule>(*referencedRule.declaredComplexType);
                }
                particle.elementIsNillable = referencedRule.isNillable;
                particle.elementDefaultValue = referencedRule.defaultValue;
                particle.elementFixedValue = referencedRule.fixedValue;
                particle.elementBlockRestriction = referencedRule.blockRestriction;
                particle.elementBlockExtension = referencedRule.blockExtension;
                particle.elementFinalRestriction = referencedRule.finalRestriction;
                particle.elementFinalExtension = referencedRule.finalExtension;
            } else {
                const ElementRule localRule = parseElementRule(particleElement, false);
                particle.name = localRule.name;
                particle.namespaceUri = localRule.namespaceUri;
                if (particle.name.empty()) {
                    throw XmlException("XML Schema child element declarations require a name or ref");
                }
                particle.elementSimpleType = localRule.declaredSimpleType;
                if (localRule.declaredComplexType.has_value()) {
                    particle.elementComplexType = std::make_shared<ComplexTypeRule>(*localRule.declaredComplexType);
                }
                particle.elementIsNillable = localRule.isNillable;
                particle.elementDefaultValue = localRule.defaultValue;
                particle.elementFixedValue = localRule.fixedValue;
                particle.elementBlockRestriction = localRule.blockRestriction;
                particle.elementBlockExtension = localRule.blockExtension;
                particle.elementFinalRestriction = localRule.finalRestriction;
                particle.elementFinalExtension = localRule.finalExtension;
                upsertRule(localRule);
            }
            return particle;
        }

        if (particleElement.LocalName() == "any") {
            validateParticleOccursRange(particle.minOccurs, particle.maxOccurs);
            particle.kind = Particle::Kind::Any;
            particle.namespaceUri = particleElement.GetAttribute("namespace");
            if (particle.namespaceUri.empty()) {
                particle.namespaceUri = "##any";
            }
            particle.processContents = parseProcessContentsValue(particleElement);
            return particle;
        }

        if (particleElement.LocalName() == "group") {
            validateParticleOccursRange(particle.minOccurs, particle.maxOccurs);
            const std::string groupRef = particleElement.GetAttribute("ref");
            if (groupRef.empty()) {
                throw XmlException("XML Schema group references require a ref");
            }

            const std::size_t minOccurs = particle.minOccurs;
            const std::size_t maxOccurs = particle.maxOccurs;
            const auto [localName, namespaceUri] = resolveTypeName(particleElement, groupRef);
            particle = ensureGroupResolved(localName, namespaceUri, groupRef);
            particle.minOccurs = minOccurs;
            particle.maxOccurs = maxOccurs;
            return particle;
        }

        if (particleElement.LocalName() == "all") {
            particle.kind = Particle::Kind::All;
            if (particle.maxOccurs > 1) {
                throw XmlException("XML Schema xs:all currently supports maxOccurs up to 1");
            }
            if (particle.minOccurs > 1) {
                throw XmlException("XML Schema xs:all currently supports minOccurs up to 1");
            }
        } else {
            particle.kind = particleElement.LocalName() == "choice" ? Particle::Kind::Choice : Particle::Kind::Sequence;
        }
        validateParticleOccursRange(particle.minOccurs, particle.maxOccurs);
        for (const auto& child : particleElement.ChildNodes()) {
            if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                continue;
            }
            const auto* childElement = static_cast<const XmlElement*>(child.get());
            if (childElement->NamespaceURI() != kSchemaNamespace) {
                continue;
            }
            if (particle.kind == Particle::Kind::All) {
                validateAllChildParticleOccurs(*childElement);
            }
            if (childElement->LocalName() == "element"
                || childElement->LocalName() == "any"
                || childElement->LocalName() == "group"
                || childElement->LocalName() == "sequence"
                || childElement->LocalName() == "choice"
                || childElement->LocalName() == "all") {
                Particle childParticle = parseParticle(*childElement);
                if (particle.kind == Particle::Kind::All) {
                    validateAllChildParticleShape(*childElement, childParticle);
                }
                particle.children.push_back(std::move(childParticle));
            }
        }
        return particle;
    };

    ensureComplexTypeResolved = [&](const std::string& localTypeName, const std::string& typeNamespaceUri, const std::string& qualifiedName) -> ComplexTypeRule {
        const std::string ruleKey = makeQualifiedRuleKey(localTypeName, typeNamespaceUri);
        const auto declaration = declaredComplexTypes.find(ruleKey);
        if (declaration == declaredComplexTypes.end()) {
            if (const auto* namedType = FindComplexTypeRule(localTypeName, typeNamespaceUri)) {
                return *namedType;
            }
            throw XmlException("XML Schema complexType '" + qualifiedName + "' is not supported");
        }

        if (resolvedLocalComplexTypes.find(ruleKey) != resolvedLocalComplexTypes.end()) {
            if (const auto* namedType = FindComplexTypeRule(localTypeName, typeNamespaceUri)) {
                return *namedType;
            }
        }

        if (!resolvingComplexTypes.insert(ruleKey).second) {
            throw XmlException("XML Schema complexType '" + qualifiedName + "' contains a circular type reference");
        }

        ComplexTypeRule rule;
        try {
            rule = parseComplexType(*declaration->second);
        } catch (...) {
            resolvingComplexTypes.erase(ruleKey);
            throw;
        }
        resolvingComplexTypes.erase(ruleKey);

        rule.namedTypeName = localTypeName;
        rule.namedTypeNamespaceUri = typeNamespaceUri;

        upsertComplexType(NamedComplexTypeRule{localTypeName, typeNamespaceUri, rule});
        resolvedLocalComplexTypes.insert(ruleKey);
        return rule;
    };

    ensureElementResolved = [&](const std::string& localName, const std::string& namespaceUri, const std::string& qualifiedName) -> ElementRule {
        const std::string ruleKey = makeQualifiedRuleKey(localName, namespaceUri);
        const auto declaration = declaredElements.find(ruleKey);
        if (declaration == declaredElements.end()) {
            if (const auto* rule = FindElementRule(localName, namespaceUri)) {
                return *rule;
            }
            throw XmlException("XML Schema element '" + qualifiedName + "' is not supported");
        }

        if (resolvedLocalElements.find(ruleKey) != resolvedLocalElements.end()) {
            if (const auto* rule = FindElementRule(localName, namespaceUri)) {
                return *rule;
            }
        }

        if (!resolvingElements.insert(ruleKey).second) {
            throw XmlException("XML Schema element '" + qualifiedName + "' contains a circular element reference");
        }

        ElementRule rule;
        try {
            rule = parseElementRule(*declaration->second, true);
        } catch (...) {
            resolvingElements.erase(ruleKey);
            throw;
        }
        resolvingElements.erase(ruleKey);

        upsertRule(rule);
        resolvedLocalElements.insert(ruleKey);
        return rule;
    };

    ensureGroupResolved = [&](const std::string& localName, const std::string& namespaceUri, const std::string& qualifiedName) -> Particle {
        const std::string ruleKey = makeQualifiedRuleKey(localName, namespaceUri);
        if (const auto found = resolvedGroups.find(ruleKey); found != resolvedGroups.end()) {
            return found->second;
        }

        const auto declaration = declaredGroups.find(ruleKey);
        if (declaration == declaredGroups.end()) {
            if (const auto* rule = FindGroupRule(localName, namespaceUri)) {
                resolvedGroups[ruleKey] = *rule;
                return *rule;
            }
            throw XmlException("XML Schema group '" + qualifiedName + "' is not supported");
        }
        if (!resolvingGroups.insert(ruleKey).second) {
            throw XmlException("XML Schema group '" + qualifiedName + "' contains a circular group reference");
        }

        Particle particle;
        bool foundParticle = false;
        try {
            for (const auto& child : declaration->second->ChildNodes()) {
                if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                    continue;
                }
                const auto* childElement = static_cast<const XmlElement*>(child.get());
                if (childElement->NamespaceURI() != kSchemaNamespace) {
                    continue;
                }
                if (childElement->LocalName() == "sequence"
                    || childElement->LocalName() == "choice"
                    || childElement->LocalName() == "group"
                    || childElement->LocalName() == "all") {
                    particle = parseParticle(*childElement);
                    foundParticle = true;
                    break;
                }
            }
        } catch (...) {
            resolvingGroups.erase(ruleKey);
            throw;
        }
        resolvingGroups.erase(ruleKey);

        if (!foundParticle) {
            throw XmlException("XML Schema group '" + qualifiedName + "' must contain a sequence, choice, all, or group");
        }
        resolvedGroups[ruleKey] = particle;
        upsertGroup(NamedGroupRule{localName, namespaceUri, particle});
        return particle;
    };

    ensureAttributeResolved = [&](const std::string& localName, const std::string& namespaceUri, const std::string& qualifiedName) -> AttributeUse {
        const std::string ruleKey = makeQualifiedRuleKey(localName, namespaceUri);
        if (const auto found = resolvedAttributes.find(ruleKey); found != resolvedAttributes.end()) {
            return found->second;
        }

        const auto declaration = declaredAttributes.find(ruleKey);
        if (declaration == declaredAttributes.end()) {
            if (const auto* namedAttribute = FindAttributeRule(localName, namespaceUri)) {
                return *namedAttribute;
            }
            throw XmlException("XML Schema attribute '" + qualifiedName + "' is not supported");
        }
        if (!resolvingAttributes.insert(ruleKey).second) {
            throw XmlException("XML Schema attribute '" + qualifiedName + "' contains a circular attribute reference");
        }

        AttributeUse attributeUse;
        try {
            attributeUse = parseAttributeUse(*declaration->second, true);
        } catch (...) {
            resolvingAttributes.erase(ruleKey);
            throw;
        }
        resolvingAttributes.erase(ruleKey);

        resolvedAttributes[ruleKey] = attributeUse;
        upsertAttribute(NamedAttributeRule{localName, namespaceUri, attributeUse});
        return attributeUse;
    };

    ensureAttributeGroupResolved = [&](const std::string& localName, const std::string& namespaceUri, const std::string& qualifiedName) -> std::vector<AttributeUse> {
        const std::string ruleKey = makeQualifiedRuleKey(localName, namespaceUri);
        if (const auto found = resolvedAttributeGroups.find(ruleKey); found != resolvedAttributeGroups.end()) {
            return found->second;
        }

        const auto declaration = declaredAttributeGroups.find(ruleKey);
        if (declaration == declaredAttributeGroups.end()) {
            if (const auto* namedAttributeGroup = FindAttributeGroupRule(localName, namespaceUri)) {
                resolvedAttributeGroups[ruleKey] = namedAttributeGroup->attributes;
                resolvedAttributeGroupAnyAllowed[ruleKey] = namedAttributeGroup->anyAttributeAllowed;
                resolvedAttributeGroupAnyNamespace[ruleKey] = namedAttributeGroup->anyAttributeNamespaceConstraint;
                resolvedAttributeGroupAnyProcessContents[ruleKey] = namedAttributeGroup->anyAttributeProcessContents;
                return namedAttributeGroup->attributes;
            }
            throw XmlException("XML Schema attributeGroup '" + qualifiedName + "' is not supported");
        }
        if (!resolvingAttributeGroups.insert(ruleKey).second) {
            throw XmlException("XML Schema attributeGroup '" + qualifiedName + "' contains a circular attributeGroup reference");
        }

        std::vector<AttributeUse> attributes;
        bool anyAttributeAllowed = false;
        std::string anyAttributeNamespaceConstraint = "##any";
        std::string anyAttributeProcessContents = "strict";
        try {
            attributes = parseAttributeGroup(*declaration->second, anyAttributeAllowed, anyAttributeNamespaceConstraint, anyAttributeProcessContents);
        } catch (...) {
            resolvingAttributeGroups.erase(ruleKey);
            throw;
        }
        resolvingAttributeGroups.erase(ruleKey);

        resolvedAttributeGroups[ruleKey] = attributes;
        resolvedAttributeGroupAnyAllowed[ruleKey] = anyAttributeAllowed;
        resolvedAttributeGroupAnyNamespace[ruleKey] = anyAttributeNamespaceConstraint;
        resolvedAttributeGroupAnyProcessContents[ruleKey] = anyAttributeProcessContents;
        upsertAttributeGroup(NamedAttributeGroupRule{
            localName,
            namespaceUri,
            AttributeGroupRule{attributes, anyAttributeAllowed, anyAttributeNamespaceConstraint, anyAttributeProcessContents},
        });
        return attributes;
    };

    parseComplexType = [&](const XmlElement& complexTypeElement) -> ComplexTypeRule {
        const XmlSchemaSet::Annotation declarationAnnotation = parseAnnotation(complexTypeElement);
        ComplexTypeRule rule;
        rule.annotation = declarationAnnotation;
        rule.namedTypeName.clear();
        rule.namedTypeNamespaceUri.clear();
        rule.derivationMethod = ComplexTypeRule::DerivationMethod::None;
        rule.derivationBaseName.clear();
        rule.derivationBaseNamespaceUri.clear();
        rule.allowsText = false;
        rule.contentModel = ContentModel::Empty;
        const std::string effectiveBlock = effectiveDerivationControlValue(complexTypeElement.GetAttribute("block"), schemaBlockDefault);
        const std::string effectiveFinal = effectiveDerivationControlValue(complexTypeElement.GetAttribute("final"), schemaFinalDefault);
        rule.blockRestriction = containsDerivationToken(effectiveBlock, "restriction");
        rule.blockExtension = containsDerivationToken(effectiveBlock, "extension");
        rule.finalRestriction = containsDerivationToken(effectiveFinal, "restriction");
        rule.finalExtension = containsDerivationToken(effectiveFinal, "extension");

        struct SimpleContentBaseInfo {
            SimpleTypeRule textType;
            std::vector<AttributeUse> attributes;
            bool attributesMayBeIntroduced = false;
            bool anyAttributeAllowed = false;
            std::string anyAttributeNamespaceConstraint = "##any";
            std::string anyAttributeProcessContents = "strict";
        };

        const auto resolveSimpleContentBase = [&](const XmlElement& context, const std::string& baseType, const std::string& description) {
            SimpleContentBaseInfo baseInfo;
            if (const auto builtinType = resolveBuiltinSimpleType(baseType); builtinType.has_value()) {
                baseInfo.textType = *builtinType;
                baseInfo.attributesMayBeIntroduced = true;
                return baseInfo;
            }

            const auto [localTypeName, typeNamespaceUri] = resolveTypeName(context, baseType);
            if (const auto* namedSimpleType = FindSimpleTypeRule(localTypeName, typeNamespaceUri)) {
                baseInfo.textType = *namedSimpleType;
                baseInfo.attributesMayBeIntroduced = true;
                return baseInfo;
            }
            if (declaredSimpleTypes.find(makeQualifiedRuleKey(localTypeName, typeNamespaceUri)) != declaredSimpleTypes.end()) {
                baseInfo.textType = ensureSimpleTypeResolved(localTypeName, typeNamespaceUri, baseType);
                baseInfo.attributesMayBeIntroduced = true;
                return baseInfo;
            }

            ComplexTypeRule baseRule;
            if (const auto builtinComplexType = resolveBuiltinComplexType(baseType); builtinComplexType.has_value()) {
                baseRule = *builtinComplexType;
            } else if (const auto* namedComplexType = FindComplexTypeRule(localTypeName, typeNamespaceUri)) {
                baseRule = *namedComplexType;
            } else if (declaredComplexTypes.find(makeQualifiedRuleKey(localTypeName, typeNamespaceUri)) != declaredComplexTypes.end()) {
                baseRule = ensureComplexTypeResolved(localTypeName, typeNamespaceUri, baseType);
            } else {
                throw XmlException("XML Schema " + description + " base '" + baseType + "' is not supported");
            }

            if (!baseRule.allowsText || !baseRule.textType.has_value()) {
                throw XmlException("XML Schema " + description + " base '" + baseType + "' must have simple content");
            }

            baseInfo.textType = *baseRule.textType;
            baseInfo.attributes = baseRule.attributes;
            baseInfo.anyAttributeAllowed = baseRule.anyAttributeAllowed;
            baseInfo.anyAttributeNamespaceConstraint = baseRule.anyAttributeNamespaceConstraint;
            baseInfo.anyAttributeProcessContents = baseRule.anyAttributeProcessContents;
            return baseInfo;
        };

        const auto ensureComplexTypeFinalAllowsDerivation = [&](const ComplexTypeRule& baseRule,
            const std::string& baseType,
            const std::string& description,
            bool isRestriction) {
            const bool blocked = isRestriction ? baseRule.finalRestriction : baseRule.finalExtension;
            if (!blocked) {
                return;
            }

            throw XmlException(
                "XML Schema " + description + " base '" + baseType + "' blocks "
                + (isRestriction ? std::string("restriction") : std::string("extension"))
                + " derivation via complexType final");
        };

        for (const auto& child : complexTypeElement.ChildNodes()) {
            if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                continue;
            }
            const auto* childElement = static_cast<const XmlElement*>(child.get());
            if (childElement->NamespaceURI() != kSchemaNamespace) {
                continue;
            }

            if (childElement->LocalName() == "simpleContent") {
                const XmlElement* extensionElement = nullptr;
                const XmlElement* restrictionElement = nullptr;
                for (const auto& contentChild : childElement->ChildNodes()) {
                    if (contentChild == nullptr || contentChild->NodeType() != XmlNodeType::Element) {
                        continue;
                    }
                    const auto* contentChildElement = static_cast<const XmlElement*>(contentChild.get());
                    if (contentChildElement->NamespaceURI() == kSchemaNamespace
                        && contentChildElement->LocalName() == "extension") {
                        extensionElement = contentChildElement;
                        break;
                    }
                    if (contentChildElement->NamespaceURI() == kSchemaNamespace
                        && contentChildElement->LocalName() == "restriction") {
                        restrictionElement = contentChildElement;
                        break;
                    }
                }
                if (extensionElement == nullptr && restrictionElement == nullptr) {
                    throw XmlException("XML Schema simpleContent currently requires an extension or restriction");
                }

                if (restrictionElement != nullptr) {
                    const std::string baseType = restrictionElement->GetAttribute("base");
                    if (baseType.empty()) {
                        throw XmlException("XML Schema simpleContent restriction requires a base type");
                    }

                    const auto baseInfo = resolveSimpleContentBase(*restrictionElement, baseType, "simpleContent restriction");
                    const auto [localTypeName, typeNamespaceUri] = resolveTypeName(*restrictionElement, baseType);
                    if (const auto* namedComplexType = FindComplexTypeRule(localTypeName, typeNamespaceUri)) {
                        ensureComplexTypeFinalAllowsDerivation(*namedComplexType, baseType, "simpleContent restriction", true);
                    } else if (declaredComplexTypes.find(makeQualifiedRuleKey(localTypeName, typeNamespaceUri)) != declaredComplexTypes.end()) {
                        ensureComplexTypeFinalAllowsDerivation(
                            ensureComplexTypeResolved(localTypeName, typeNamespaceUri, baseType),
                            baseType,
                            "simpleContent restriction",
                            true);
                    }
                    rule.allowsText = true;
                    rule.derivationMethod = ComplexTypeRule::DerivationMethod::Restriction;
                    rule.derivationBaseName = localTypeName;
                    rule.derivationBaseNamespaceUri = typeNamespaceUri;
                    rule.textType = baseInfo.textType;
                    rule.attributes = baseInfo.attributes;
                    rule.anyAttributeAllowed = baseInfo.anyAttributeAllowed;
                    rule.anyAttributeNamespaceConstraint = baseInfo.anyAttributeNamespaceConstraint;
                    rule.anyAttributeProcessContents = baseInfo.anyAttributeProcessContents;

                    for (const auto& restrictionChild : restrictionElement->ChildNodes()) {
                        if (restrictionChild == nullptr || restrictionChild->NodeType() != XmlNodeType::Element) {
                            continue;
                        }
                        const auto* restrictionChildElement = static_cast<const XmlElement*>(restrictionChild.get());
                        if (restrictionChildElement->LocalName() == "simpleType" && restrictionChildElement->NamespaceURI() == kSchemaNamespace) {
                            rule.textType = parseSimpleType(*restrictionChildElement);
                        } else if (isSimpleTypeFacetElement(*restrictionChildElement) && rule.textType.has_value()) {
                            applyFacetToSimpleType(*rule.textType, *restrictionChildElement);
                        }
                    }

                    if (rule.textType.has_value()) {
                        validateDerivedSimpleTypeRestriction(baseInfo.textType, *rule.textType,
                            "simpleContent restriction base '" + baseType + "'");
                    }

                    applyAttributeRestrictionsToRule(rule, *restrictionElement, baseInfo.attributesMayBeIntroduced,
                        "simpleContent restriction base '" + baseType + "'");
                    rule.annotation = declarationAnnotation;
                    return rule;
                }

                const std::string baseType = extensionElement->GetAttribute("base");
                if (baseType.empty()) {
                    throw XmlException("XML Schema simpleContent extension requires a base type");
                }

                rule.allowsText = true;
                if (const auto builtinType = resolveBuiltinSimpleType(baseType); builtinType.has_value()) {
                    rule.textType = builtinType;
                    rule.derivationMethod = ComplexTypeRule::DerivationMethod::Extension;
                    rule.derivationBaseName.clear();
                    rule.derivationBaseNamespaceUri.clear();
                } else {
                    const auto [localTypeName, typeNamespaceUri] = resolveTypeName(*extensionElement, baseType);
                    if (const auto* namedSimpleType = FindSimpleTypeRule(localTypeName, typeNamespaceUri)) {
                        rule.textType = *namedSimpleType;
                        rule.derivationMethod = ComplexTypeRule::DerivationMethod::Extension;
                        rule.derivationBaseName.clear();
                        rule.derivationBaseNamespaceUri.clear();
                    } else if (declaredSimpleTypes.find(makeQualifiedRuleKey(localTypeName, typeNamespaceUri)) != declaredSimpleTypes.end()) {
                        rule.textType = ensureSimpleTypeResolved(localTypeName, typeNamespaceUri, baseType);
                        rule.derivationMethod = ComplexTypeRule::DerivationMethod::Extension;
                        rule.derivationBaseName.clear();
                        rule.derivationBaseNamespaceUri.clear();
                    } else if (const auto builtinComplexType = resolveBuiltinComplexType(baseType); builtinComplexType.has_value()) {
                        if (!builtinComplexType->allowsText || !builtinComplexType->textType.has_value()) {
                            throw XmlException("XML Schema simpleContent extension base '" + baseType + "' must have simple content");
                        }
                        rule = *builtinComplexType;
                        rule.allowsText = true;
                        rule.namedTypeName.clear();
                        rule.namedTypeNamespaceUri.clear();
                        rule.derivationMethod = ComplexTypeRule::DerivationMethod::Extension;
                        rule.derivationBaseName = localTypeName;
                        rule.derivationBaseNamespaceUri = typeNamespaceUri;
                    } else if (const auto* namedComplexType = FindComplexTypeRule(localTypeName, typeNamespaceUri)) {
                        ensureComplexTypeFinalAllowsDerivation(*namedComplexType, baseType, "simpleContent extension", false);
                        if (!namedComplexType->allowsText || !namedComplexType->textType.has_value()) {
                            throw XmlException("XML Schema simpleContent extension base '" + baseType + "' must have simple content");
                        }
                        rule = *namedComplexType;
                        rule.allowsText = true;
                        rule.namedTypeName.clear();
                        rule.namedTypeNamespaceUri.clear();
                        rule.derivationMethod = ComplexTypeRule::DerivationMethod::Extension;
                        rule.derivationBaseName = localTypeName;
                        rule.derivationBaseNamespaceUri = typeNamespaceUri;
                    } else if (declaredComplexTypes.find(makeQualifiedRuleKey(localTypeName, typeNamespaceUri)) != declaredComplexTypes.end()) {
                        const ComplexTypeRule baseRule = ensureComplexTypeResolved(localTypeName, typeNamespaceUri, baseType);
                        ensureComplexTypeFinalAllowsDerivation(baseRule, baseType, "simpleContent extension", false);
                        if (!baseRule.allowsText || !baseRule.textType.has_value()) {
                            throw XmlException("XML Schema simpleContent extension base '" + baseType + "' must have simple content");
                        }
                        rule = baseRule;
                        rule.allowsText = true;
                        rule.namedTypeName.clear();
                        rule.namedTypeNamespaceUri.clear();
                        rule.derivationMethod = ComplexTypeRule::DerivationMethod::Extension;
                        rule.derivationBaseName = localTypeName;
                        rule.derivationBaseNamespaceUri = typeNamespaceUri;
                    } else {
                        throw XmlException("XML Schema simpleContent base '" + baseType + "' is not supported");
                    }
                }

                applyAttributeChildrenToRule(rule, *extensionElement);
                rule.annotation = declarationAnnotation;
                return rule;
            }

            if (childElement->LocalName() == "complexContent") {
                const XmlElement* extensionElement = nullptr;
                const XmlElement* restrictionElement = nullptr;
                for (const auto& contentChild : childElement->ChildNodes()) {
                    if (contentChild == nullptr || contentChild->NodeType() != XmlNodeType::Element) {
                        continue;
                    }
                    const auto* contentChildElement = static_cast<const XmlElement*>(contentChild.get());
                    if (contentChildElement->NamespaceURI() == kSchemaNamespace
                        && contentChildElement->LocalName() == "extension") {
                        extensionElement = contentChildElement;
                        break;
                    }
                    if (contentChildElement->NamespaceURI() == kSchemaNamespace
                        && contentChildElement->LocalName() == "restriction") {
                        restrictionElement = contentChildElement;
                        break;
                    }
                }
                if (extensionElement == nullptr && restrictionElement == nullptr) {
                    throw XmlException("XML Schema complexContent currently requires an extension or restriction");
                }

                if (restrictionElement != nullptr) {
                    const std::string baseType = restrictionElement->GetAttribute("base");
                    if (baseType.empty()) {
                        throw XmlException("XML Schema complexContent restriction requires a base type");
                    }

                    const auto [localTypeName, typeNamespaceUri] = resolveTypeName(*restrictionElement, baseType);
                    if (const auto builtinComplexType = resolveBuiltinComplexType(baseType); builtinComplexType.has_value()) {
                        rule = *builtinComplexType;
                    } else if (const auto* namedComplexType = FindComplexTypeRule(localTypeName, typeNamespaceUri)) {
                        ensureComplexTypeFinalAllowsDerivation(*namedComplexType, baseType, "complexContent restriction", true);
                        rule = *namedComplexType;
                    } else if (declaredComplexTypes.find(makeQualifiedRuleKey(localTypeName, typeNamespaceUri)) != declaredComplexTypes.end()) {
                        rule = ensureComplexTypeResolved(localTypeName, typeNamespaceUri, baseType);
                        ensureComplexTypeFinalAllowsDerivation(rule, baseType, "complexContent restriction", true);
                    } else {
                        throw XmlException("XML Schema complexContent base '" + baseType + "' is not supported");
                    }

                    rule.namedTypeName.clear();
                    rule.namedTypeNamespaceUri.clear();
                    rule.derivationMethod = ComplexTypeRule::DerivationMethod::Restriction;
                    rule.derivationBaseName = localTypeName;
                    rule.derivationBaseNamespaceUri = typeNamespaceUri;

                    const std::optional<Particle> baseParticle = makeFlatParticleFromRule(rule);

                    for (const auto& restrictionChild : restrictionElement->ChildNodes()) {
                        if (restrictionChild == nullptr || restrictionChild->NodeType() != XmlNodeType::Element) {
                            continue;
                        }
                        const auto* restrictionChildElement = static_cast<const XmlElement*>(restrictionChild.get());
                        if (restrictionChildElement->NamespaceURI() != kSchemaNamespace) {
                            continue;
                        }
                        if (restrictionChildElement->LocalName() == "sequence"
                            || restrictionChildElement->LocalName() == "choice"
                            || restrictionChildElement->LocalName() == "group"
                            || restrictionChildElement->LocalName() == "all"
                            || restrictionChildElement->LocalName() == "any") {
                            const Particle derivedParticle = parseParticle(*restrictionChildElement);
                            if (!baseParticle.has_value()) {
                                throw XmlException("XML Schema complexContent restriction base '" + baseType + "' cannot introduce child particles when the base type has no element content");
                            }
                            validateDerivedParticleRestriction(*baseParticle, derivedParticle,
                                "complexContent restriction base '" + baseType + "'");
                            applyParticleToRule(rule, derivedParticle);
                            break;
                        }
                    }

                    applyAttributeRestrictionsToRule(rule, *restrictionElement, false,
                        "complexContent restriction base '" + baseType + "'");
                    rule.annotation = declarationAnnotation;
                    return rule;
                }

                const std::string baseType = extensionElement->GetAttribute("base");
                if (baseType.empty()) {
                    throw XmlException("XML Schema complexContent extension requires a base type");
                }

                const auto [localTypeName, typeNamespaceUri] = resolveTypeName(*extensionElement, baseType);
                if (const auto builtinComplexType = resolveBuiltinComplexType(baseType); builtinComplexType.has_value()) {
                    rule = *builtinComplexType;
                } else if (const auto* namedComplexType = FindComplexTypeRule(localTypeName, typeNamespaceUri)) {
                    ensureComplexTypeFinalAllowsDerivation(*namedComplexType, baseType, "complexContent extension", false);
                    rule = *namedComplexType;
                } else if (declaredComplexTypes.find(makeQualifiedRuleKey(localTypeName, typeNamespaceUri)) != declaredComplexTypes.end()) {
                    rule = ensureComplexTypeResolved(localTypeName, typeNamespaceUri, baseType);
                    ensureComplexTypeFinalAllowsDerivation(rule, baseType, "complexContent extension", false);
                } else {
                    throw XmlException("XML Schema complexContent base '" + baseType + "' is not supported");
                }

                rule.namedTypeName.clear();
                rule.namedTypeNamespaceUri.clear();
                rule.derivationMethod = ComplexTypeRule::DerivationMethod::Extension;
                rule.derivationBaseName = localTypeName;
                rule.derivationBaseNamespaceUri = typeNamespaceUri;

                std::optional<Particle> extensionParticle;
                for (const auto& extensionChild : extensionElement->ChildNodes()) {
                    if (extensionChild == nullptr || extensionChild->NodeType() != XmlNodeType::Element) {
                        continue;
                    }
                    const auto* extensionChildElement = static_cast<const XmlElement*>(extensionChild.get());
                    if (extensionChildElement->NamespaceURI() != kSchemaNamespace) {
                        continue;
                    }
                    if (extensionChildElement->LocalName() == "sequence"
                        || extensionChildElement->LocalName() == "choice"
                        || extensionChildElement->LocalName() == "group"
                        || extensionChildElement->LocalName() == "all"
                        || extensionChildElement->LocalName() == "any") {
                        extensionParticle = parseParticle(*extensionChildElement);
                        break;
                    }
                }

                mergeComplexContent(rule, extensionParticle);
                applyAttributeChildrenToRule(rule, *extensionElement);
                rule.annotation = declarationAnnotation;
                return rule;
            }

            if (childElement->LocalName() == "sequence"
                || childElement->LocalName() == "choice"
                || childElement->LocalName() == "group"
                || childElement->LocalName() == "all"
                || childElement->LocalName() == "any") {
                applyParticleToRule(rule, parseParticle(*childElement));
            } else if (childElement->LocalName() == "attribute"
                || childElement->LocalName() == "attributeGroup"
                || childElement->LocalName() == "anyAttribute") {
                applyAttributeChildrenToRule(rule, complexTypeElement);
                break;
            }
        }

        rule.annotation = declarationAnnotation;
        return rule;
    };

    simpleTypeDerivesFrom = [&](const SimpleTypeRule& derivedType,
        const SimpleTypeRule& baseType,
        bool& usesRestriction) {
        if (!derivedType.namedTypeName.empty()
            && derivedType.namedTypeName == baseType.namedTypeName
            && derivedType.namedTypeNamespaceUri == baseType.namedTypeNamespaceUri) {
            return true;
        }
        if (derivedType.namedTypeName.empty()
            && baseType.namedTypeName.empty()
            && derivedType.derivationMethod == SimpleTypeRule::DerivationMethod::None
            && baseType.derivationMethod == SimpleTypeRule::DerivationMethod::None
            && derivedType.variety == baseType.variety) {
            bool builtinUsesRestriction = false;
            if (BuiltinSimpleTypeDerivesFrom(derivedType.baseType, baseType.baseType, builtinUsesRestriction)) {
                usesRestriction = usesRestriction || builtinUsesRestriction;
                return true;
            }
        }

        const SimpleTypeRule* current = &derivedType;
        std::unordered_set<std::string> visited;
        while (true) {
            if (current->derivationMethod == SimpleTypeRule::DerivationMethod::Restriction) {
                usesRestriction = true;
            }
            if (!baseType.namedTypeName.empty()
                && current->derivationBaseName == baseType.namedTypeName
                && current->derivationBaseNamespaceUri == baseType.namedTypeNamespaceUri) {
                return true;
            }
            if (baseType.namedTypeName.empty()
                && current->derivationMethod == SimpleTypeRule::DerivationMethod::Restriction) {
                bool builtinUsesRestriction = false;
                if (BuiltinSimpleTypeDerivesFrom(current->baseType, baseType.baseType, builtinUsesRestriction)) {
                    usesRestriction = usesRestriction || builtinUsesRestriction;
                    return true;
                }
            }
            if (current->derivationBaseName.empty()) {
                return false;
            }

            const std::string currentKey = current->derivationBaseNamespaceUri + "\n" + current->derivationBaseName;
            if (!visited.insert(currentKey).second) {
                return false;
            }

            current = FindSimpleTypeRule(current->derivationBaseName, current->derivationBaseNamespaceUri);
            if (current == nullptr) {
                return false;
            }
            if (!baseType.namedTypeName.empty()
                && current->namedTypeName == baseType.namedTypeName
                && current->namedTypeNamespaceUri == baseType.namedTypeNamespaceUri) {
                return true;
            }
        }
    };

    complexTypeDerivesFrom = [&](const ComplexTypeRule& derivedType,
        const ComplexTypeRule& baseType,
        bool& usesRestriction,
        bool& usesExtension) {
        if (baseType.namedTypeName == "anyType"
            && baseType.namedTypeNamespaceUri == "http://www.w3.org/2001/XMLSchema") {
            return true;
        }
        if (!derivedType.namedTypeName.empty()
            && derivedType.namedTypeName == baseType.namedTypeName
            && derivedType.namedTypeNamespaceUri == baseType.namedTypeNamespaceUri) {
            return true;
        }

        const ComplexTypeRule* current = &derivedType;
        std::unordered_set<std::string> visited;
        while (true) {
            if (current->derivationMethod == ComplexTypeRule::DerivationMethod::Restriction) {
                usesRestriction = true;
            } else if (current->derivationMethod == ComplexTypeRule::DerivationMethod::Extension) {
                usesExtension = true;
            }
            if (current->derivationBaseName == baseType.namedTypeName
                && current->derivationBaseNamespaceUri == baseType.namedTypeNamespaceUri) {
                return true;
            }
            if (current->derivationBaseName.empty()) {
                return false;
            }

            const std::string currentKey = current->derivationBaseNamespaceUri + "\n" + current->derivationBaseName;
            if (!visited.insert(currentKey).second) {
                return false;
            }

            current = FindComplexTypeRule(current->derivationBaseName, current->derivationBaseNamespaceUri);
            if (current == nullptr) {
                return false;
            }
            if (!baseType.namedTypeName.empty()
                && current->namedTypeName == baseType.namedTypeName
                && current->namedTypeNamespaceUri == baseType.namedTypeNamespaceUri) {
                return true;
            }
        }
    };

    const auto elementTypeCanJoinSubstitutionHead = [&](const ElementRule& memberRule,
        const ElementRule& headRule,
        bool& usesRestriction,
        bool& usesExtension) {
        if (headRule.declaredComplexType.has_value()) {
            if (!memberRule.declaredComplexType.has_value()) {
                return false;
            }
            return complexTypeDerivesFrom(*memberRule.declaredComplexType, *headRule.declaredComplexType, usesRestriction, usesExtension);
        }
        if (headRule.declaredSimpleType.has_value()) {
            if (!memberRule.declaredSimpleType.has_value()) {
                return false;
            }
            return simpleTypeDerivesFrom(*memberRule.declaredSimpleType, *headRule.declaredSimpleType, usesRestriction);
        }
        return true;
    };

    parseElementRule = [&](const XmlElement& schemaElement, bool isGlobalDeclaration) -> ElementRule {
        const std::string elementRef = schemaElement.GetAttribute("ref");
        if (!elementRef.empty()) {
            const auto [localName, namespaceUri] = resolveTypeName(schemaElement, elementRef);
            return ensureElementResolved(localName, namespaceUri, elementRef);
        }

        ElementRule rule;
        rule.annotation = parseAnnotation(schemaElement);
        rule.name = schemaElement.GetAttribute("name");
        rule.namespaceUri = resolveElementDeclarationNamespace(schemaElement, isGlobalDeclaration);
        rule.isAbstract = parseSchemaBooleanAttribute(schemaElement, "abstract");
        rule.isNillable = parseSchemaBooleanAttribute(schemaElement, "nillable");
        const std::string defaultValue = schemaElement.GetAttribute("default");
        const std::string fixedValue = schemaElement.GetAttribute("fixed");
        if (!defaultValue.empty() && !fixedValue.empty()) {
            throw XmlException("XML Schema element declarations cannot specify both default and fixed values");
        }
        if (!defaultValue.empty()) {
            rule.defaultValue = defaultValue;
        }
        if (!fixedValue.empty()) {
            rule.fixedValue = fixedValue;
        }
        const std::string effectiveBlock = effectiveDerivationControlValue(schemaElement.GetAttribute("block"), schemaBlockDefault);
        const std::string effectiveFinal = effectiveDerivationControlValue(schemaElement.GetAttribute("final"), schemaFinalDefault);
        rule.blockSubstitution = containsDerivationToken(effectiveBlock, "substitution");
        rule.blockRestriction = containsDerivationToken(effectiveBlock, "restriction");
        rule.blockExtension = containsDerivationToken(effectiveBlock, "extension");
        rule.finalSubstitution = containsDerivationToken(effectiveFinal, "substitution");
        rule.finalRestriction = containsDerivationToken(effectiveFinal, "restriction");
        rule.finalExtension = containsDerivationToken(effectiveFinal, "extension");
        rule.allowsText = true;
        rule.contentModel = ContentModel::Empty;
        if (rule.name.empty()) {
            throw XmlException("XML Schema element declarations require a name");
        }

        const std::string substitutionGroup = schemaElement.GetAttribute("substitutionGroup");
        std::string substitutionGroupHeadName;
        std::string substitutionGroupHeadNamespaceUri;
        if (!substitutionGroup.empty()) {
            const auto resolvedHead = resolveTypeName(schemaElement, substitutionGroup);
            substitutionGroupHeadName = resolvedHead.first;
            substitutionGroupHeadNamespaceUri = resolvedHead.second;
        }

        const std::string typeAttribute = schemaElement.GetAttribute("type");
        if (!typeAttribute.empty()) {
            if (const auto builtinType = resolveBuiltinSimpleType(typeAttribute); builtinType.has_value()) {
                rule.textType = builtinType;
                rule.declaredSimpleType = *builtinType;
            } else if (const auto builtinComplexType = resolveBuiltinComplexType(typeAttribute); builtinComplexType.has_value()) {
                applyComplexType(rule, *builtinComplexType);
                rule.declaredComplexType = *builtinComplexType;
            } else {
                const auto [localTypeName, typeNamespaceUri] = resolveTypeName(schemaElement, typeAttribute);
                if (const auto* namedSimpleType = FindSimpleTypeRule(localTypeName, typeNamespaceUri)) {
                    rule.textType = *namedSimpleType;
                    rule.declaredSimpleType = *namedSimpleType;
                } else if (declaredSimpleTypes.find(makeQualifiedRuleKey(localTypeName, typeNamespaceUri)) != declaredSimpleTypes.end()) {
                    rule.textType = ensureSimpleTypeResolved(localTypeName, typeNamespaceUri, typeAttribute);
                    rule.declaredSimpleType = *rule.textType;
                } else if (const auto* namedComplexType = FindComplexTypeRule(localTypeName, typeNamespaceUri)) {
                    applyComplexType(rule, *namedComplexType);
                    rule.declaredComplexType = *namedComplexType;
                } else if (declaredComplexTypes.find(makeQualifiedRuleKey(localTypeName, typeNamespaceUri)) != declaredComplexTypes.end()) {
                    const ComplexTypeRule resolvedType = ensureComplexTypeResolved(localTypeName, typeNamespaceUri, typeAttribute);
                    applyComplexType(rule, resolvedType);
                    rule.declaredComplexType = resolvedType;
                } else {
                    throw XmlException("XML Schema element type '" + typeAttribute + "' is not supported");
                }
            }
        }

        const XmlElement* complexType = nullptr;
        const XmlElement* simpleType = nullptr;
        for (const auto& child : schemaElement.ChildNodes()) {
            if (child != nullptr && child->NodeType() == XmlNodeType::Element) {
                const auto* childElement = static_cast<const XmlElement*>(child.get());
                if (childElement->LocalName() == "complexType" && childElement->NamespaceURI() == kSchemaNamespace) {
                    complexType = childElement;
                    break;
                }
                if (childElement->LocalName() == "simpleType" && childElement->NamespaceURI() == kSchemaNamespace) {
                    simpleType = childElement;
                }
            }
        }

        if (simpleType != nullptr) {
            rule.textType = parseSimpleType(*simpleType);
            rule.declaredSimpleType = *rule.textType;
            rule.allowsText = true;
            for (const auto& child : schemaElement.ChildNodes()) {
                if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                    continue;
                }
                const auto* childElement = static_cast<const XmlElement*>(child.get());
                if (childElement->NamespaceURI() != kSchemaNamespace) {
                    continue;
                }
                if (childElement->LocalName() == "key"
                    || childElement->LocalName() == "unique"
                    || childElement->LocalName() == "keyref") {
                    rule.identityConstraints.push_back(parseIdentityConstraint(*childElement));
                }
            }
            if (!substitutionGroup.empty()) {
                const ElementRule headRule = ensureElementResolved(substitutionGroupHeadName, substitutionGroupHeadNamespaceUri, substitutionGroup);
                if (headRule.finalSubstitution) {
                    throw XmlException("XML Schema element '" + rule.name + "' cannot join substitutionGroup '" + substitutionGroup + "' because the head blocks substitution derivation");
                }
                bool usesRestriction = false;
                bool usesExtension = false;
                if (!elementTypeCanJoinSubstitutionHead(rule, headRule, usesRestriction, usesExtension)) {
                    throw XmlException("XML Schema element '" + rule.name + "' must use the same type as, or a type derived from, substitutionGroup head '" + substitutionGroup + "'");
                }
                if ((usesRestriction && headRule.finalRestriction) || (usesExtension && headRule.finalExtension)) {
                    throw XmlException("XML Schema element '" + rule.name + "' cannot join substitutionGroup '" + substitutionGroup + "' because the head blocks the required type derivation");
                }
                rule.substitutionGroupHeadName = substitutionGroupHeadName;
                rule.substitutionGroupHeadNamespaceUri = substitutionGroupHeadNamespaceUri;
            }
            return rule;
        }

        if (complexType == nullptr) {
            if ((rule.defaultValue.has_value() || rule.fixedValue.has_value()) && !rule.allowsText) {
                throw XmlException("XML Schema element default/fixed values are only supported for simple-content elements");
            }
            for (const auto& child : schemaElement.ChildNodes()) {
                if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                    continue;
                }
                const auto* childElement = static_cast<const XmlElement*>(child.get());
                if (childElement->NamespaceURI() != kSchemaNamespace) {
                    continue;
                }
                if (childElement->LocalName() == "key"
                    || childElement->LocalName() == "unique"
                    || childElement->LocalName() == "keyref") {
                    rule.identityConstraints.push_back(parseIdentityConstraint(*childElement));
                }
            }
            if (!substitutionGroup.empty()) {
                const ElementRule headRule = ensureElementResolved(substitutionGroupHeadName, substitutionGroupHeadNamespaceUri, substitutionGroup);
                if (headRule.finalSubstitution) {
                    throw XmlException("XML Schema element '" + rule.name + "' cannot join substitutionGroup '" + substitutionGroup + "' because the head blocks substitution derivation");
                }
                bool usesRestriction = false;
                bool usesExtension = false;
                if (!elementTypeCanJoinSubstitutionHead(rule, headRule, usesRestriction, usesExtension)) {
                    throw XmlException("XML Schema element '" + rule.name + "' must use the same type as, or a type derived from, substitutionGroup head '" + substitutionGroup + "'");
                }
                if ((usesRestriction && headRule.finalRestriction) || (usesExtension && headRule.finalExtension)) {
                    throw XmlException("XML Schema element '" + rule.name + "' cannot join substitutionGroup '" + substitutionGroup + "' because the head blocks the required type derivation");
                }
                rule.substitutionGroupHeadName = substitutionGroupHeadName;
                rule.substitutionGroupHeadNamespaceUri = substitutionGroupHeadNamespaceUri;
            }
            return rule;
        }

        const ComplexTypeRule inlineComplexType = parseComplexType(*complexType);
        applyComplexType(rule, inlineComplexType);
        rule.declaredComplexType = inlineComplexType;

        if ((rule.defaultValue.has_value() || rule.fixedValue.has_value())
            && (!rule.allowsText || rule.particle.has_value() || !rule.children.empty())) {
            throw XmlException("XML Schema element default/fixed values are only supported for simple-content elements");
        }

        for (const auto& child : schemaElement.ChildNodes()) {
            if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                continue;
            }
            const auto* childElement = static_cast<const XmlElement*>(child.get());
            if (childElement->NamespaceURI() != kSchemaNamespace) {
                continue;
            }
            if (childElement->LocalName() == "key"
                || childElement->LocalName() == "unique"
                || childElement->LocalName() == "keyref") {
                rule.identityConstraints.push_back(parseIdentityConstraint(*childElement));
            }
        }

        if (!substitutionGroup.empty()) {
            const ElementRule headRule = ensureElementResolved(substitutionGroupHeadName, substitutionGroupHeadNamespaceUri, substitutionGroup);
            if (headRule.finalSubstitution) {
                throw XmlException("XML Schema element '" + rule.name + "' cannot join substitutionGroup '" + substitutionGroup + "' because the head blocks substitution derivation");
            }
            bool usesRestriction = false;
            bool usesExtension = false;
            if (!elementTypeCanJoinSubstitutionHead(rule, headRule, usesRestriction, usesExtension)) {
                throw XmlException("XML Schema element '" + rule.name + "' must use the same type as, or a type derived from, substitutionGroup head '" + substitutionGroup + "'");
            }
            if ((usesRestriction && headRule.finalRestriction) || (usesExtension && headRule.finalExtension)) {
                throw XmlException("XML Schema element '" + rule.name + "' cannot join substitutionGroup '" + substitutionGroup + "' because the head blocks the required type derivation");
            }
            rule.substitutionGroupHeadName = substitutionGroupHeadName;
            rule.substitutionGroupHeadNamespaceUri = substitutionGroupHeadNamespaceUri;
        }

        return rule;
    };

    for (const auto& [ruleKey, declaration] : declaredSimpleTypes) {
        (void)ruleKey;
        const std::string name = declaration->GetAttribute("name");
        (void)ensureSimpleTypeResolved(name, targetNamespace, name);
    }

    for (const auto& [ruleKey, declaration] : declaredComplexTypes) {
        (void)ruleKey;
        const std::string name = declaration->GetAttribute("name");
        (void)ensureComplexTypeResolved(name, targetNamespace, name);
    }

    for (const auto& [ruleKey, declaration] : declaredGroups) {
        (void)ruleKey;
        const std::string name = declaration->GetAttribute("name");
        upsertGroupAnnotation(NamedGroupAnnotation{name, targetNamespace, parseAnnotation(*declaration)});
        (void)ensureGroupResolved(name, targetNamespace, name);
    }

    for (const auto& [ruleKey, declaration] : declaredAttributes) {
        (void)ruleKey;
        const std::string name = declaration->GetAttribute("name");
        (void)ensureAttributeResolved(name, targetNamespace, name);
    }

    for (const auto& [ruleKey, declaration] : declaredAttributeGroups) {
        (void)ruleKey;
        const std::string name = declaration->GetAttribute("name");
        upsertAttributeGroupAnnotation(NamedAttributeGroupAnnotation{name, targetNamespace, parseAnnotation(*declaration)});
        (void)ensureAttributeGroupResolved(name, targetNamespace, name);
    }

    for (const auto& [ruleKey, declaration] : declaredElements) {
        (void)ruleKey;
        const std::string name = declaration->GetAttribute("name");
        (void)ensureElementResolved(name, targetNamespace, name);
    }

    for (const auto& child : root->ChildNodes()) {
        if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
            continue;
        }
        const auto* childElement = static_cast<const XmlElement*>(child.get());
        if (childElement->LocalName() != "element" || childElement->NamespaceURI() != kSchemaNamespace) {
            continue;
        }
        const std::string name = childElement->GetAttribute("name");
        if (!name.empty()) {
            (void)ensureElementResolved(name, targetNamespace, name);
        }
    }

    for (const auto& constraint : identityConstraints_) {
        if (constraint.kind != ElementRule::IdentityConstraint::Kind::KeyRef) {
            continue;
        }
        const auto* referencedConstraint = FindIdentityConstraint(constraint.referName, constraint.referNamespaceUri);
        if (referencedConstraint == nullptr) {
            throw XmlException("XML Schema keyref '" + constraint.name + "' refers to an unknown identity constraint");
        }
        if (referencedConstraint->kind == ElementRule::IdentityConstraint::Kind::KeyRef) {
            throw XmlException("XML Schema keyref '" + constraint.name + "' must refer to a key or unique constraint");
        }
        if (referencedConstraint->fieldXPaths.size() != constraint.fieldXPaths.size()) {
            throw XmlException("XML Schema keyref '" + constraint.name + "' must declare the same number of fields as its referenced key/unique constraint");
        }
    }
}

void XmlSchemaSet::AddFile(const std::string& path) {
    static const std::string kSchemaNamespace = "http://www.w3.org/2001/XMLSchema";

    const std::filesystem::path schemaPath = NormalizeSchemaPath(std::filesystem::path(path));
    const std::string normalizedPath = schemaPath.generic_string();
    if (loadedSchemaFiles_.find(normalizedPath) != loadedSchemaFiles_.end()) {
        return;
    }
    if (!activeSchemaFiles_.insert(normalizedPath).second) {
        throw XmlException("Circular XML Schema include/import/override/redefine detected for file: " + normalizedPath);
    }

    try {
        std::ifstream stream(schemaPath, std::ios::binary);
        if (!stream) {
            throw XmlException("Failed to open XML schema file: " + normalizedPath);
        }

        std::ostringstream buffer;
        buffer << stream.rdbuf();
        const std::string xml = buffer.str();

        const auto document = XmlDocument::Parse(xml);
        const auto root = document->DocumentElement();
        if (root == nullptr || root->LocalName() != "schema" || root->NamespaceURI() != kSchemaNamespace) {
            throw XmlException("XmlSchemaSet::AddFile requires an XML Schema document");
        }

        const std::string targetNamespace = root->GetAttribute("targetNamespace");
        const auto loadReferencedSchema = [&](const XmlElement& referenceElement, const char* referenceKind) {
            const std::string referenceKindString(referenceKind);
            const std::string schemaLocation = referenceElement.GetAttribute("schemaLocation");
            if (schemaLocation.empty()) {
                if (referenceKindString == "import") {
                    const std::string importedNamespace = referenceElement.GetAttribute("namespace");
                    const auto loadedNamespace = std::find_if(schemaAnnotations_.begin(), schemaAnnotations_.end(), [&](const auto& schemaAnnotation) {
                        return schemaAnnotation.namespaceUri == importedNamespace;
                    });
                    if (loadedNamespace == schemaAnnotations_.end()) {
                        throw XmlException("XML Schema import without schemaLocation requires the namespace to already be loaded into the XmlSchemaSet");
                    }
                    return;
                }
                throw XmlException(std::string("XML Schema ") + referenceKind + " requires a schemaLocation when loaded via AddFile");
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
            if (referencedRoot == nullptr || referencedRoot->LocalName() != "schema" || referencedRoot->NamespaceURI() != kSchemaNamespace) {
                throw XmlException("Referenced XML schema file is not a valid XML Schema document: " + referencedPath.generic_string());
            }

            const std::string referencedTargetNamespace = referencedRoot->GetAttribute("targetNamespace");
            if (referenceKindString == "include" || referenceKindString == "override" || referenceKindString == "redefine") {
                if (referencedTargetNamespace != targetNamespace) {
                    throw XmlException(
                        std::string("XML Schema ") + referenceKind + " requires the referenced schema to use the same targetNamespace");
                }
            } else {
                const std::string importedNamespace = referenceElement.GetAttribute("namespace");
                if (!importedNamespace.empty()) {
                    if (referencedTargetNamespace != importedNamespace) {
                        throw XmlException(
                            "XML Schema import requires the referenced schema targetNamespace to match the declared namespace");
                    }
                } else if (!referencedTargetNamespace.empty()) {
                    throw XmlException(
                        "XML Schema import without a namespace can only reference a schema with no targetNamespace");
                }
            }

            AddFile(referencedPath.string());
        };

        for (const auto& child : root->ChildNodes()) {
            if (child == nullptr || child->NodeType() != XmlNodeType::Element) {
                continue;
            }
            const auto* childElement = static_cast<const XmlElement*>(child.get());
            if (childElement->NamespaceURI() != kSchemaNamespace) {
                continue;
            }

            if (childElement->LocalName() == "include") {
                loadReferencedSchema(*childElement, "include");
            } else if (childElement->LocalName() == "import") {
                loadReferencedSchema(*childElement, "import");
            } else if (childElement->LocalName() == "override") {
                loadReferencedSchema(*childElement, "override");
            } else if (childElement->LocalName() == "redefine") {
                loadReferencedSchema(*childElement, "redefine");
            }
        }

        AddXml(BuildSchemaAddXmlPayload(*root, schemaPath, kSchemaNamespace));
        loadedSchemaFiles_.insert(normalizedPath);
        activeSchemaFiles_.erase(normalizedPath);
    } catch (...) {
        activeSchemaFiles_.erase(normalizedPath);
        throw;
    }
}

std::size_t XmlSchemaSet::Count() const noexcept {
    return elements_.size();
}

bool XmlSchemaSet::HasIdentityConstraints() const noexcept {
    return !identityConstraints_.empty();
}

const XmlSchemaSet::Annotation* XmlSchemaSet::FindSchemaAnnotation(const std::string& namespaceUri) const {
    return FindStoredSchemaAnnotation(namespaceUri);
}

const XmlSchemaSet::Annotation* XmlSchemaSet::FindElementAnnotation(const std::string& localName, const std::string& namespaceUri) const {
    const auto* rule = FindElementRule(localName, namespaceUri);
    return rule == nullptr ? nullptr : std::addressof(rule->annotation);
}

const XmlSchemaSet::Annotation* XmlSchemaSet::FindSimpleTypeAnnotation(const std::string& localName, const std::string& namespaceUri) const {
    const auto* rule = FindSimpleTypeRule(localName, namespaceUri);
    return rule == nullptr ? nullptr : std::addressof(rule->annotation);
}

const XmlSchemaSet::Annotation* XmlSchemaSet::FindComplexTypeAnnotation(const std::string& localName, const std::string& namespaceUri) const {
    const auto* rule = FindComplexTypeRule(localName, namespaceUri);
    return rule == nullptr ? nullptr : std::addressof(rule->annotation);
}

const XmlSchemaSet::Annotation* XmlSchemaSet::FindAttributeAnnotation(const std::string& localName, const std::string& namespaceUri) const {
    const auto* rule = FindAttributeRule(localName, namespaceUri);
    return rule == nullptr ? nullptr : std::addressof(rule->annotation);
}

const XmlSchemaSet::Annotation* XmlSchemaSet::FindGroupAnnotation(const std::string& localName, const std::string& namespaceUri) const {
    return FindStoredGroupAnnotation(localName, namespaceUri);
}

const XmlSchemaSet::Annotation* XmlSchemaSet::FindAttributeGroupAnnotation(const std::string& localName, const std::string& namespaceUri) const {
    return FindStoredAttributeGroupAnnotation(localName, namespaceUri);
}

const XmlSchemaSet::Annotation* XmlSchemaSet::FindIdentityConstraintAnnotation(const std::string& localName, const std::string& namespaceUri) const {
    const auto* constraint = FindIdentityConstraint(localName, namespaceUri);
    return constraint == nullptr ? nullptr : std::addressof(constraint->annotation);
}

const XmlSchemaSet::ElementRule* XmlSchemaSet::FindElementRule(const std::string& localName, const std::string& namespaceUri) const {
    const auto found = std::find_if(elements_.begin(), elements_.end(), [&](const auto& rule) {
        return rule.name == localName && rule.namespaceUri == namespaceUri;
    });
    return found == elements_.end() ? nullptr : std::addressof(*found);
}

const XmlSchemaSet::SimpleTypeRule* XmlSchemaSet::FindSimpleTypeRule(const std::string& localName, const std::string& namespaceUri) const {
    const auto found = std::find_if(simpleTypes_.begin(), simpleTypes_.end(), [&](const auto& rule) {
        return rule.name == localName && rule.namespaceUri == namespaceUri;
    });
    return found == simpleTypes_.end() ? nullptr : std::addressof(found->rule);
}

const XmlSchemaSet::ComplexTypeRule* XmlSchemaSet::FindComplexTypeRule(const std::string& localName, const std::string& namespaceUri) const {
    const auto found = std::find_if(complexTypes_.begin(), complexTypes_.end(), [&](const auto& rule) {
        return rule.name == localName && rule.namespaceUri == namespaceUri;
    });
    return found == complexTypes_.end() ? nullptr : std::addressof(found->rule);
}

const XmlSchemaSet::AttributeUse* XmlSchemaSet::FindAttributeRule(const std::string& localName, const std::string& namespaceUri) const {
    const auto found = std::find_if(attributes_.begin(), attributes_.end(), [&](const auto& rule) {
        return rule.name == localName && rule.namespaceUri == namespaceUri;
    });
    return found == attributes_.end() ? nullptr : std::addressof(found->rule);
}

const XmlSchemaSet::Particle* XmlSchemaSet::FindGroupRule(const std::string& localName, const std::string& namespaceUri) const {
    const auto found = std::find_if(groupRules_.begin(), groupRules_.end(), [&](const auto& rule) {
        return rule.name == localName && rule.namespaceUri == namespaceUri;
    });
    return found == groupRules_.end() ? nullptr : std::addressof(found->rule);
}

const XmlSchemaSet::AttributeGroupRule* XmlSchemaSet::FindAttributeGroupRule(const std::string& localName, const std::string& namespaceUri) const {
    const auto found = std::find_if(attributeGroupRules_.begin(), attributeGroupRules_.end(), [&](const auto& rule) {
        return rule.name == localName && rule.namespaceUri == namespaceUri;
    });
    return found == attributeGroupRules_.end() ? nullptr : std::addressof(found->rule);
}

const XmlSchemaSet::Annotation* XmlSchemaSet::FindStoredSchemaAnnotation(const std::string& namespaceUri) const {
    const auto found = std::find_if(schemaAnnotations_.begin(), schemaAnnotations_.end(), [&](const auto& annotation) {
        return annotation.namespaceUri == namespaceUri;
    });
    return found == schemaAnnotations_.end() ? nullptr : std::addressof(found->annotation);
}

const XmlSchemaSet::Annotation* XmlSchemaSet::FindStoredGroupAnnotation(const std::string& localName, const std::string& namespaceUri) const {
    const auto found = std::find_if(groups_.begin(), groups_.end(), [&](const auto& annotation) {
        return annotation.name == localName && annotation.namespaceUri == namespaceUri;
    });
    return found == groups_.end() ? nullptr : std::addressof(found->annotation);
}

const XmlSchemaSet::Annotation* XmlSchemaSet::FindStoredAttributeGroupAnnotation(const std::string& localName, const std::string& namespaceUri) const {
    const auto found = std::find_if(attributeGroups_.begin(), attributeGroups_.end(), [&](const auto& annotation) {
        return annotation.name == localName && annotation.namespaceUri == namespaceUri;
    });
    return found == attributeGroups_.end() ? nullptr : std::addressof(found->annotation);
}

const XmlSchemaSet::ElementRule::IdentityConstraint* XmlSchemaSet::FindIdentityConstraint(
    const std::string& localName,
    const std::string& namespaceUri) const {
    const auto found = std::find_if(identityConstraints_.begin(), identityConstraints_.end(), [&](const auto& constraint) {
        return constraint.name == localName && constraint.namespaceUri == namespaceUri;
    });
    return found == identityConstraints_.end() ? nullptr : std::addressof(*found);
}

XPathNavigator::XPathNavigator(const XmlNode* node) : node_(node) {
}

bool XPathNavigator::IsEmpty() const noexcept {
    return node_ == nullptr;
}

XmlNodeType XPathNavigator::NodeType() const {
    return node_ == nullptr ? XmlNodeType::None : node_->NodeType();
}

std::string XPathNavigator::Name() const {
    return node_ == nullptr ? std::string{} : node_->Name();
}

std::string XPathNavigator::LocalName() const {
    return node_ == nullptr ? std::string{} : node_->LocalName();
}

std::string XPathNavigator::Prefix() const {
    return node_ == nullptr ? std::string{} : node_->Prefix();
}

std::string XPathNavigator::NamespaceURI() const {
    return node_ == nullptr ? std::string{} : node_->NamespaceURI();
}

std::string XPathNavigator::Value() const {
    return node_ == nullptr ? std::string{} : node_->Value();
}

std::string XPathNavigator::InnerXml() const {
    return node_ == nullptr ? std::string{} : node_->InnerXml();
}

std::string XPathNavigator::OuterXml() const {
    return node_ == nullptr ? std::string{} : node_->OuterXml();
}

XPathNavigator XPathNavigator::Clone() const {
    return XPathNavigator(node_);
}

bool XPathNavigator::MoveToFirstChild() {
    if (node_ == nullptr || node_->NodeType() == XmlNodeType::Attribute) {
        return false;
    }
    const auto child = node_->FirstChild();
    if (child == nullptr) {
        return false;
    }
    node_ = child.get();
    return true;
}

bool XPathNavigator::MoveToNext() {
    if (node_ == nullptr) {
        return false;
    }
    if (node_->NodeType() == XmlNodeType::Attribute) {
        const auto* parent = node_->ParentNode();
        if (parent == nullptr || parent->NodeType() != XmlNodeType::Element) {
            return false;
        }
        const auto& attributes = static_cast<const XmlElement*>(parent)->Attributes();
        for (std::size_t index = 0; index < attributes.size(); ++index) {
            if (attributes[index].get() != node_) {
                continue;
            }
            if (index + 1 >= attributes.size()) {
                return false;
            }
            node_ = attributes[index + 1].get();
            return true;
        }
        return false;
    }
    const auto sibling = node_->NextSibling();
    if (sibling == nullptr) {
        return false;
    }
    node_ = sibling.get();
    return true;
}

bool XPathNavigator::MoveToPrevious() {
    if (node_ == nullptr) {
        return false;
    }
    if (node_->NodeType() == XmlNodeType::Attribute) {
        const auto* parent = node_->ParentNode();
        if (parent == nullptr || parent->NodeType() != XmlNodeType::Element) {
            return false;
        }
        const auto& attributes = static_cast<const XmlElement*>(parent)->Attributes();
        for (std::size_t index = 0; index < attributes.size(); ++index) {
            if (attributes[index].get() != node_) {
                continue;
            }
            if (index == 0) {
                return false;
            }
            node_ = attributes[index - 1].get();
            return true;
        }
        return false;
    }
    const auto sibling = node_->PreviousSibling();
    if (sibling == nullptr) {
        return false;
    }
    node_ = sibling.get();
    return true;
}

bool XPathNavigator::MoveToParent() {
    if (node_ == nullptr || node_->ParentNode() == nullptr) {
        return false;
    }
    node_ = node_->ParentNode();
    return true;
}

bool XPathNavigator::MoveToFirstAttribute() {
    if (node_ == nullptr || node_->NodeType() != XmlNodeType::Element) {
        return false;
    }
    const auto& attributes = static_cast<const XmlElement*>(node_)->Attributes();
    if (attributes.empty()) {
        return false;
    }
    node_ = attributes.front().get();
    return true;
}

bool XPathNavigator::MoveToNextAttribute() {
    if (node_ == nullptr || node_->NodeType() != XmlNodeType::Attribute) {
        return false;
    }
    return MoveToNext();
}

void XPathNavigator::MoveToRoot() {
    if (node_ == nullptr) {
        return;
    }
    while (node_->ParentNode() != nullptr) {
        node_ = node_->ParentNode();
    }
}

XPathNavigator XPathNavigator::SelectSingleNode(const std::string& xpath) const {
    const auto selected = node_ == nullptr ? nullptr : node_->SelectSingleNode(xpath);
    return XPathNavigator(selected.get());
}

XPathNavigator XPathNavigator::SelectSingleNode(const std::string& xpath, const XmlNamespaceManager& namespaces) const {
    const auto selected = node_ == nullptr ? nullptr : node_->SelectSingleNode(xpath, namespaces);
    return XPathNavigator(selected.get());
}

std::vector<XPathNavigator> XPathNavigator::Select(const std::string& xpath) const {
    std::vector<XPathNavigator> result;
    if (node_ == nullptr) {
        return result;
    }
    const auto selected = node_->SelectNodes(xpath);
    result.reserve(selected.Count());
    for (const auto& item : selected) {
        if (item != nullptr) {
            result.emplace_back(item.get());
        }
    }
    return result;
}

std::vector<XPathNavigator> XPathNavigator::Select(const std::string& xpath, const XmlNamespaceManager& namespaces) const {
    std::vector<XPathNavigator> result;
    if (node_ == nullptr) {
        return result;
    }
    const auto selected = node_->SelectNodes(xpath, namespaces);
    result.reserve(selected.Count());
    for (const auto& item : selected) {
        if (item != nullptr) {
            result.emplace_back(item.get());
        }
    }
    return result;
}

const XmlNode* XPathNavigator::UnderlyingNode() const noexcept {
    return node_;
}

XPathDocument::XPathDocument() : document_(std::make_shared<XmlDocument>()) {
}

XPathDocument::XPathDocument(const std::string& xml) : document_(XmlDocument::Parse(xml)) {
}

std::shared_ptr<XPathDocument> XPathDocument::Parse(const std::string& xml) {
    return std::make_shared<XPathDocument>(xml);
}

void XPathDocument::LoadXml(const std::string& xml) {
    if (document_ == nullptr) {
        document_ = std::make_shared<XmlDocument>();
    }
    document_->LoadXml(xml);
}

void XPathDocument::Load(const std::string& path) {
    if (document_ == nullptr) {
        document_ = std::make_shared<XmlDocument>();
    }
    document_->Load(path);
}

void XPathDocument::Load(std::istream& stream) {
    if (document_ == nullptr) {
        document_ = std::make_shared<XmlDocument>();
    }
    document_->Load(stream);
}

XPathNavigator XPathDocument::CreateNavigator() const {
    return XPathNavigator(document_.get());
}

const XmlDocument& XPathDocument::Document() const noexcept {
    return *document_;
}

}  // namespace System::Xml
