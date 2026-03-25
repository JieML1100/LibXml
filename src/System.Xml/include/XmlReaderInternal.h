#pragma once
// XmlReader and DTD parsing internal helpers.

#include "XmlDomInternal.h"

namespace System::Xml {

namespace {

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
            const char next = available >= 6 ? prefix[5] : '\0';
            if (next == '\0') {
                return XmlMarkupKind::None;
            }
            if (IsWhitespace(next) || next == '?') {
                return XmlMarkupKind::XmlDeclaration;
            }
            return XmlMarkupKind::ProcessingInstruction;
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
#elif defined(_M_ARM64) || defined(__aarch64__)
    const uint8x16_t quoteVector = vdupq_n_u8(static_cast<std::uint8_t>(quote));
    const uint8x16_t nulVector = vdupq_n_u8(0);

    std::size_t offset = 0;
    while (offset + 16 <= length) {
        const uint8x16_t block = vld1q_u8(reinterpret_cast<const std::uint8_t*>(data + offset));
        const uint8x16_t quoteMatches = vceqq_u8(block, quoteVector);
        const uint8x16_t nulMatches = vceqq_u8(block, nulVector);
        const uint8x16_t anyMatches = vorrq_u8(quoteMatches, nulMatches);

        alignas(16) std::uint8_t maskBytes[16];
        vst1q_u8(maskBytes, anyMatches);
        for (std::size_t index = 0; index < 16; ++index) {
            if (maskBytes[index] != 0) {
                return offset + index;
            }
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

std::size_t FindTextSpecialInBuffer(const char* data, std::size_t length) noexcept {
    if (data == nullptr || length == 0) {
        return std::string::npos;
    }

#if defined(_M_X64) || defined(_M_IX86)
    const __m128i lessThanVector = _mm_set1_epi8('<');
    const __m128i ampersandVector = _mm_set1_epi8('&');

    std::size_t offset = 0;
    while (offset + 16 <= length) {
        const __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + offset));
        const __m128i lessThanMatches = _mm_cmpeq_epi8(block, lessThanVector);
        const __m128i ampersandMatches = _mm_cmpeq_epi8(block, ampersandVector);
        const unsigned int mask = static_cast<unsigned int>(_mm_movemask_epi8(_mm_or_si128(lessThanMatches, ampersandMatches)));
        if (mask != 0) {
            unsigned long firstMatch = 0;
            _BitScanForward(&firstMatch, mask);
            return offset + static_cast<std::size_t>(firstMatch);
        }
        offset += 16;
    }

    for (; offset < length; ++offset) {
        if (data[offset] == '<' || data[offset] == '&') {
            return offset;
        }
    }
    return std::string::npos;
#elif defined(_M_ARM64) || defined(__aarch64__)
    const uint8x16_t lessThanVector = vdupq_n_u8(static_cast<std::uint8_t>('<'));
    const uint8x16_t ampersandVector = vdupq_n_u8(static_cast<std::uint8_t>('&'));

    std::size_t offset = 0;
    while (offset + 16 <= length) {
        const uint8x16_t block = vld1q_u8(reinterpret_cast<const std::uint8_t*>(data + offset));
        const uint8x16_t lessThanMatches = vceqq_u8(block, lessThanVector);
        const uint8x16_t ampersandMatches = vceqq_u8(block, ampersandVector);
        const uint8x16_t anyMatches = vorrq_u8(lessThanMatches, ampersandMatches);

        alignas(16) std::uint8_t maskBytes[16];
        vst1q_u8(maskBytes, anyMatches);
        for (std::size_t index = 0; index < 16; ++index) {
            if (maskBytes[index] != 0) {
                return offset + index;
            }
        }
        offset += 16;
    }

    for (; offset < length; ++offset) {
        if (data[offset] == '<' || data[offset] == '&') {
            return offset;
        }
    }
    return std::string::npos;
#else
    for (std::size_t offset = 0; offset < length; ++offset) {
        if (data[offset] == '<' || data[offset] == '&') {
            return offset;
        }
    }
    return std::string::npos;
#endif
}

std::size_t FindTextLineSpecialInBuffer(const char* data, std::size_t length) noexcept {
    if (data == nullptr || length == 0) {
        return std::string::npos;
    }

#if defined(_M_X64) || defined(_M_IX86)
    const __m128i newlineVector = _mm_set1_epi8('\n');
    const __m128i lessThanVector = _mm_set1_epi8('<');
    const __m128i ampersandVector = _mm_set1_epi8('&');

    std::size_t offset = 0;
    while (offset + 16 <= length) {
        const __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + offset));
        const __m128i newlineMatches = _mm_cmpeq_epi8(block, newlineVector);
        const __m128i lessThanMatches = _mm_cmpeq_epi8(block, lessThanVector);
        const __m128i ampersandMatches = _mm_cmpeq_epi8(block, ampersandVector);
        const __m128i anyMatches = _mm_or_si128(newlineMatches, _mm_or_si128(lessThanMatches, ampersandMatches));
        const unsigned int mask = static_cast<unsigned int>(_mm_movemask_epi8(anyMatches));
        if (mask != 0) {
            unsigned long firstMatch = 0;
            _BitScanForward(&firstMatch, mask);
            return offset + static_cast<std::size_t>(firstMatch);
        }
        offset += 16;
    }

    for (; offset < length; ++offset) {
        if (data[offset] == '\n' || data[offset] == '<' || data[offset] == '&') {
            return offset;
        }
    }
    return std::string::npos;
#elif defined(_M_ARM64) || defined(__aarch64__)
    const uint8x16_t newlineVector = vdupq_n_u8(static_cast<std::uint8_t>('\n'));
    const uint8x16_t lessThanVector = vdupq_n_u8(static_cast<std::uint8_t>('<'));
    const uint8x16_t ampersandVector = vdupq_n_u8(static_cast<std::uint8_t>('&'));

    std::size_t offset = 0;
    while (offset + 16 <= length) {
        const uint8x16_t block = vld1q_u8(reinterpret_cast<const std::uint8_t*>(data + offset));
        const uint8x16_t newlineMatches = vceqq_u8(block, newlineVector);
        const uint8x16_t lessThanMatches = vceqq_u8(block, lessThanVector);
        const uint8x16_t ampersandMatches = vceqq_u8(block, ampersandVector);
        const uint8x16_t anyMatches = vorrq_u8(newlineMatches, vorrq_u8(lessThanMatches, ampersandMatches));

        alignas(16) std::uint8_t maskBytes[16];
        vst1q_u8(maskBytes, anyMatches);
        for (std::size_t index = 0; index < 16; ++index) {
            if (maskBytes[index] != 0) {
                return offset + index;
            }
        }
        offset += 16;
    }

    for (; offset < length; ++offset) {
        if (data[offset] == '\n' || data[offset] == '<' || data[offset] == '&') {
            return offset;
        }
    }
    return std::string::npos;
#else
    for (std::size_t offset = 0; offset < length; ++offset) {
        if (data[offset] == '\n' || data[offset] == '<' || data[offset] == '&') {
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

std::shared_ptr<std::istream> SpoolStreamToTemporaryReplayStream(std::istream& stream, std::string_view initialBytes = {}) {
    const auto path = CreateTemporaryXmlReplayPath();
    auto replayStream = std::shared_ptr<std::fstream>(
        new std::fstream(path, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc),
        [path](std::fstream* file) {
            if (file != nullptr) {
                file->close();
                delete file;
            }
            std::error_code error;
            std::filesystem::remove(path, error);
        });

    if (!*replayStream) {
        throw XmlException("Failed to create temporary XML replay file");
    }

    if (!initialBytes.empty()) {
        replayStream->write(initialBytes.data(), static_cast<std::streamsize>(initialBytes.size()));
        if (!*replayStream) {
            replayStream.reset();
            throw XmlException("Failed to write temporary XML replay file");
        }
    }

    char chunk[64 * 1024];
    while (stream) {
        stream.read(chunk, static_cast<std::streamsize>(sizeof(chunk)));
        const auto bytesRead = stream.gcount();
        if (bytesRead <= 0) {
            break;
        }

        replayStream->write(chunk, bytesRead);
        if (!*replayStream) {
            break;
        }
    }

    if (!*replayStream) {
        replayStream.reset();
        throw XmlException("Failed to write temporary XML replay file");
    }

    replayStream->flush();
    replayStream->clear();
    replayStream->seekg(0, std::ios::beg);
    if (!*replayStream) {
        replayStream.reset();
        throw XmlException("Failed to rewind temporary XML replay file");
    }

    return std::static_pointer_cast<std::istream>(replayStream);
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

std::size_t GetUtf8BomLength(std::string_view text) noexcept {
    return text.size() >= 3
            && static_cast<unsigned char>(text[0]) == 0xEF
            && static_cast<unsigned char>(text[1]) == 0xBB
            && static_cast<unsigned char>(text[2]) == 0xBF
        ? 3u
        : 0u;
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


}  // namespace

namespace {

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

bool ReadSubsetConditionalSection(
    std::string_view text,
    std::size_t& position,
    bool& includeSection,
    std::string_view& content) {
    if (!SubsetStartsWithAt(text, position, "<![")) {
        return false;
    }

    position += 3;
    SkipSubsetWhitespace(text, position);
    if (SubsetStartsWithAt(text, position, "INCLUDE")) {
        includeSection = true;
        position += 7;
    } else if (SubsetStartsWithAt(text, position, "IGNORE")) {
        includeSection = false;
        position += 6;
    } else {
        return false;
    }

    SkipSubsetWhitespace(text, position);
    if (position >= text.size() || text[position] != '[') {
        return false;
    }
    ++position;

    const auto contentStart = position;
    int depth = 1;
    char quote = '\0';
    while (position < text.size()) {
        if (quote != '\0') {
            if (text[position++] == quote) {
                quote = '\0';
            }
            continue;
        }

        if (text[position] == '\'' || text[position] == '"') {
            quote = text[position++];
            continue;
        }

        if (StartsWithAt(text, position, "<![")) {
            position += 3;
            ++depth;
            continue;
        }

        if (StartsWithAt(text, position, "]]>") ) {
            --depth;
            if (depth == 0) {
                content = text.substr(contentStart, position - contentStart);
                position += 3;
                return true;
            }
            position += 3;
            continue;
        }

        ++position;
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
            const bool parameterEntity = position < text.size() && text[position] == '%';
            if (parameterEntity) {
                ++position;
                SkipSubsetWhitespace(text, position);
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
                if (parameterEntity) {
                    ThrowMalformedDtdDeclaration("parameter entity");
                }
                position += 5;
                SkipSubsetWhitespace(text, position);
                notationName = ReadSubsetName(text, position);
                if (notationName.empty()) {
                    ThrowMalformedDtdDeclaration("entity");
                }
            }

            if (!SkipSubsetDeclaration(text, position)) {
                ThrowMalformedDtdDeclaration(parameterEntity ? "parameter entity" : "entity");
            }
            if (!parameterEntity && !name.empty()) {
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
            bool includeSection = false;
            std::string_view conditionalContent;
            if (!ReadSubsetConditionalSection(text, position, includeSection, conditionalContent)) {
                ThrowMalformedDtdDeclaration("conditional section");
            }
            if (includeSection) {
                ParseDocumentTypeInternalSubset(std::string(conditionalContent), entities, notations);
            }
            continue;
        }

        if (!SkipSubsetDeclaration(text, position)) {
            ThrowMalformedDtdDeclaration("declaration");
        }
    }
}


}  // namespace

class XmlReaderInputSource {
public:
    virtual ~XmlReaderInputSource() = default;

    virtual char CharAt(std::size_t position) const noexcept = 0;
    virtual const char* PtrAt(std::size_t position, std::size_t& available) const noexcept = 0;
    virtual std::size_t Find(std::string_view token, std::size_t position) const noexcept = 0;
    virtual std::string Slice(std::size_t start, std::size_t count) const = 0;
    virtual void AppendSliceTo(std::string& target, std::size_t start, std::size_t count) const = 0;
    virtual void EnableReplay() const {}
    virtual void DiscardBefore(std::size_t) const {}
};

class StringXmlReaderInputSource final : public XmlReaderInputSource {
public:
    explicit StringXmlReaderInputSource(std::shared_ptr<const std::string> text);

    const std::shared_ptr<const std::string>& Text() const noexcept;

    char CharAt(std::size_t position) const noexcept override;
    const char* PtrAt(std::size_t position, std::size_t& available) const noexcept override;
    std::size_t Find(std::string_view token, std::size_t position) const noexcept override;
    std::size_t FindFirstOf(std::string_view tokens, std::size_t position) const noexcept;
    std::size_t ScanQuotedValueEnd(std::size_t quoteStart) const noexcept;
    std::string Slice(std::size_t start, std::size_t count) const override;
    void AppendSliceTo(std::string& target, std::size_t start, std::size_t count) const override;

private:
    std::shared_ptr<const std::string> text_;
};

}  // namespace System::Xml
