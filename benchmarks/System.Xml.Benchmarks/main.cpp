#include "System/Xml/Xml.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace System::Xml;

namespace {

struct BenchmarkResult {
    std::string name;
    std::size_t payloadBytes = 0;
    int iterations = 0;
    double averageMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
};

struct DataSetSpec {
    std::string label;
    int primaryCount = 0;
    int secondaryCount = 0;
    int iterations = 1;
    bool runStreamVariant = true;
};

struct SchemaDataSetSpec {
    std::string label;
    std::string namespaceUri;
    int sectionCount = 0;
    int booksPerSection = 0;
    int iterations = 1;
    bool runStreamVariant = true;
};

struct ReaderStreamingDataSetSpec {
    std::string label;
    std::size_t childTextBytes = 0;
    int trailingSiblingCount = 0;
    int iterations = 1;
    int segmentCount = 1;
};

struct ExternalFileSpec {
    std::string label;
    std::string path;
    int iterations = 1;
    std::string subtreeElementName;
};

struct ExternalFileObservation {
    std::string rootName;
    std::string firstChildElementName;
    std::size_t nodeCount = 0;
    std::size_t elementCount = 0;
    std::size_t endElementCount = 0;
    std::size_t textCount = 0;
    std::size_t whitespaceCount = 0;
    std::size_t significantWhitespaceCount = 0;
    std::size_t commentCount = 0;
    std::size_t cdataCount = 0;
    std::size_t processingInstructionCount = 0;
    std::size_t attributeCount = 0;
    std::size_t subtreeMatchCount = 0;
    std::size_t maxValueBytes = 0;
    XmlNodeType maxValueNodeType = XmlNodeType::None;
    int maxDepth = 0;
};

struct PreparedReaderSubtreeReplayState {
    std::shared_ptr<std::istringstream> stream;
    std::unique_ptr<XmlReader> reader;
    std::unique_ptr<XmlReader> subtree;
    std::size_t sink = 0;
};

struct PreparedParentAdvanceState {
    std::shared_ptr<std::istringstream> stream;
    std::unique_ptr<XmlReader> reader;
    std::unique_ptr<XmlReader> subtree;
    std::size_t sink = 0;
};

struct PreparedSubtreeCreateState {
    std::shared_ptr<std::istringstream> stream;
    std::unique_ptr<XmlReader> reader;
    std::size_t sink = 0;
};

enum class DomBuildBreakdownMode {
    EventsOnly,
    DetachedNodes,
    BuildTree,
};

template <typename TAction>
BenchmarkResult RunBenchmark(const std::string& name, const std::size_t payloadBytes, const int iterations, TAction action) {
    using Clock = std::chrono::steady_clock;

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(iterations));

    action();

    for (int iteration = 0; iteration < iterations; ++iteration) {
        const auto start = Clock::now();
        action();
        const auto end = Clock::now();
        const auto elapsed = std::chrono::duration<double, std::milli>(end - start).count();
        samples.push_back(elapsed);
    }

    const auto [minIt, maxIt] = std::minmax_element(samples.begin(), samples.end());
    const double total = std::accumulate(samples.begin(), samples.end(), 0.0);
    return BenchmarkResult{name, payloadBytes, iterations, total / static_cast<double>(iterations), *minIt, *maxIt};
}

template <typename TAction>
BenchmarkResult RunBenchmarkNoWarmup(const std::string& name, const std::size_t payloadBytes, const int iterations, TAction action) {
    using Clock = std::chrono::steady_clock;

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(iterations));

    for (int iteration = 0; iteration < iterations; ++iteration) {
        const auto start = Clock::now();
        action();
        const auto end = Clock::now();
        const auto elapsed = std::chrono::duration<double, std::milli>(end - start).count();
        samples.push_back(elapsed);
    }

    const auto [minIt, maxIt] = std::minmax_element(samples.begin(), samples.end());
    const double total = std::accumulate(samples.begin(), samples.end(), 0.0);
    return BenchmarkResult{name, payloadBytes, iterations, total / static_cast<double>(iterations), *minIt, *maxIt};
}

template <typename TSetup, typename TAction>
BenchmarkResult RunBenchmarkWithSetup(
    const std::string& name,
    const std::size_t payloadBytes,
    const int iterations,
    TSetup setup,
    TAction action) {
    using Clock = std::chrono::steady_clock;

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(iterations));

    {
        auto state = setup();
        action(state);
    }

    for (int iteration = 0; iteration < iterations; ++iteration) {
        auto state = setup();
        const auto start = Clock::now();
        action(state);
        const auto end = Clock::now();
        const auto elapsed = std::chrono::duration<double, std::milli>(end - start).count();
        samples.push_back(elapsed);
    }

    const auto [minIt, maxIt] = std::minmax_element(samples.begin(), samples.end());
    const double total = std::accumulate(samples.begin(), samples.end(), 0.0);
    return BenchmarkResult{name, payloadBytes, iterations, total / static_cast<double>(iterations), *minIt, *maxIt};
}

std::string GetEnvironmentVariableOrEmpty(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
}

int GetEnvironmentVariableIntOrDefault(const char* name, const int defaultValue) {
    const std::string value = GetEnvironmentVariableOrEmpty(name);
    if (value.empty()) {
        return defaultValue;
    }

    try {
        const int parsed = std::stoi(value);
        return parsed > 0 ? parsed : defaultValue;
    } catch (...) {
        return defaultValue;
    }
}

std::size_t GetFileSizeOrZero(const std::string& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return 0;
    }

    return static_cast<std::size_t>(size);
}

std::vector<ExternalFileSpec> GetExternalFileSpecs() {
    const std::string configuredPath = GetEnvironmentVariableOrEmpty("LIBXML_REAL_XML_PATH");
    const std::string configuredSubtreeElement = GetEnvironmentVariableOrEmpty("LIBXML_REAL_XML_SUBTREE");
    const std::string path = configuredPath.empty() ? "C:\\Windows\\Panther\\MigLog.xml" : configuredPath;

    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        return {};
    }

    const std::string subtreeElementName = configuredSubtreeElement.empty() ? "Session" : configuredSubtreeElement;
    const std::string fileName = std::filesystem::path(path).filename().string();
    return {
        ExternalFileSpec{
            "real-file " + fileName,
            path,
            GetEnvironmentVariableIntOrDefault("LIBXML_REAL_XML_ITERATIONS", 1),
            subtreeElementName,
        }};
}

std::string_view NodeTypeName(const XmlNodeType nodeType) {
    switch (nodeType) {
    case XmlNodeType::None:
        return "None";
    case XmlNodeType::Element:
        return "Element";
    case XmlNodeType::Attribute:
        return "Attribute";
    case XmlNodeType::Text:
        return "Text";
    case XmlNodeType::CDATA:
        return "CDATA";
    case XmlNodeType::EntityReference:
        return "EntityReference";
    case XmlNodeType::Entity:
        return "Entity";
    case XmlNodeType::ProcessingInstruction:
        return "ProcessingInstruction";
    case XmlNodeType::Comment:
        return "Comment";
    case XmlNodeType::Document:
        return "Document";
    case XmlNodeType::DocumentType:
        return "DocumentType";
    case XmlNodeType::DocumentFragment:
        return "DocumentFragment";
    case XmlNodeType::Notation:
        return "Notation";
    case XmlNodeType::Whitespace:
        return "Whitespace";
    case XmlNodeType::SignificantWhitespace:
        return "SignificantWhitespace";
    case XmlNodeType::EndElement:
        return "EndElement";
    case XmlNodeType::EndEntity:
        return "EndEntity";
    case XmlNodeType::XmlDeclaration:
        return "XmlDeclaration";
    }

    return "Unknown";
}

std::string BuildCatalogXml(const int bookCount, const int chaptersPerBook) {
    std::ostringstream stream;
    stream << "<lib:catalog xmlns:lib=\"urn:books\" xmlns:meta=\"urn:meta\">";
    for (int bookIndex = 0; bookIndex < bookCount; ++bookIndex) {
        stream << "<lib:book meta:id=\"b" << bookIndex << "\" meta:rank=\"" << (bookIndex % 100) << "\">";
        stream << "<lib:title>Book " << bookIndex << "</lib:title>";
        stream << "<lib:author>Author " << (bookIndex % 37) << "</lib:author>";
        stream << "<lib:summary>Summary " << bookIndex << " with repeated content for parser work.</lib:summary>";
        stream << "<lib:chapters>";
        for (int chapterIndex = 0; chapterIndex < chaptersPerBook; ++chapterIndex) {
            stream << "<lib:chapter meta:index=\"" << chapterIndex << "\">";
            stream << "<lib:name>Chapter " << chapterIndex << "</lib:name>";
            stream << "<lib:pages>" << (chapterIndex + 10) << "</lib:pages>";
            stream << "</lib:chapter>";
        }
        stream << "</lib:chapters>";
        stream << "</lib:book>";
    }
    stream << "</lib:catalog>";
    return stream.str();
}

std::string BuildReaderStreamingXml(const std::size_t childTextBytes, const int trailingSiblingCount) {
    std::ostringstream stream;
    stream << "<root><child>";

    static constexpr std::size_t ChunkSize = 4096;
    const std::string chunk(ChunkSize, 'x');
    std::size_t remaining = childTextBytes;
    while (remaining >= ChunkSize) {
        stream << chunk;
        remaining -= ChunkSize;
    }
    if (remaining != 0) {
        stream << chunk.substr(0, remaining);
    }

    stream << "</child>";
    for (int siblingIndex = 0; siblingIndex < trailingSiblingCount; ++siblingIndex) {
        stream << "<other index=\"" << siblingIndex << "\">tail" << siblingIndex << "</other>";
    }
    stream << "</root>";
    return stream.str();
}

std::string BuildSegmentedReaderStreamingXml(
    const std::size_t childTextBytes,
    const int segmentCount,
    const int trailingSiblingCount) {
    if (segmentCount <= 1) {
        return BuildReaderStreamingXml(childTextBytes, trailingSiblingCount);
    }

    std::ostringstream stream;
    stream << "<root><child>";

    static constexpr std::size_t ChunkSize = 4096;
    const std::string chunk(ChunkSize, 'x');
    const std::size_t safeSegmentCount = static_cast<std::size_t>((std::max)(segmentCount, 1));
    std::size_t remaining = childTextBytes;

    for (std::size_t segmentIndex = 0; segmentIndex < safeSegmentCount; ++segmentIndex) {
        const std::size_t segmentsLeft = safeSegmentCount - segmentIndex;
        const std::size_t segmentBytes = segmentsLeft == 0 ? 0 : remaining / segmentsLeft;

        std::size_t segmentRemaining = segmentBytes;
        while (segmentRemaining >= ChunkSize) {
            stream << chunk;
            segmentRemaining -= ChunkSize;
        }
        if (segmentRemaining != 0) {
            stream << chunk.substr(0, segmentRemaining);
        }

        remaining -= segmentBytes;
        if (segmentIndex + 1 < safeSegmentCount) {
            stream << "<!--split-->";
        }
    }

    stream << "</child>";
    for (int siblingIndex = 0; siblingIndex < trailingSiblingCount; ++siblingIndex) {
        stream << "<other index=\"" << siblingIndex << "\">tail" << siblingIndex << "</other>";
    }
    stream << "</root>";
    return stream.str();
}

std::shared_ptr<XmlSchemaSet> BuildReaderSafeNestedXsiTypeSchemas() {
    auto schemas = std::make_shared<XmlSchemaSet>();
    schemas->AddXml(
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:tns=\"urn:benchmark-schema-reader-safe\" targetNamespace=\"urn:benchmark-schema-reader-safe\">"
        "  <xs:complexType name=\"BasePayloadType\">"
        "    <xs:sequence/>"
        "  </xs:complexType>"
        "  <xs:complexType name=\"DerivedPayloadType\">"
        "    <xs:complexContent>"
        "      <xs:extension base=\"tns:BasePayloadType\">"
        "        <xs:sequence>"
        "          <xs:element ref=\"tns:catalog\"/>"
        "        </xs:sequence>"
        "      </xs:extension>"
        "    </xs:complexContent>"
        "  </xs:complexType>"
        "  <xs:element name=\"root\">"
        "    <xs:complexType>"
        "      <xs:sequence>"
        "        <xs:element name=\"payload\" type=\"tns:BasePayloadType\"/>"
        "      </xs:sequence>"
        "    </xs:complexType>"
        "  </xs:element>"
        "  <xs:element name=\"catalog\">"
        "    <xs:complexType>"
        "      <xs:sequence>"
        "        <xs:element name=\"section\" maxOccurs=\"unbounded\">"
        "          <xs:complexType>"
        "            <xs:sequence>"
        "              <xs:element name=\"book\" maxOccurs=\"unbounded\">"
        "                <xs:complexType><xs:attribute name=\"id\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
        "              </xs:element>"
        "              <xs:element name=\"reference\" minOccurs=\"0\" maxOccurs=\"unbounded\">"
        "                <xs:complexType><xs:attribute name=\"bookId\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
        "              </xs:element>"
        "            </xs:sequence>"
        "          </xs:complexType>"
        "        </xs:element>"
        "      </xs:sequence>"
        "    </xs:complexType>"
        "    <xs:key name=\"bookKey\">"
        "      <xs:selector xpath=\"tns:section/tns:book\"/>"
        "      <xs:field xpath=\"@id\"/>"
        "    </xs:key>"
        "    <xs:keyref name=\"bookRef\" refer=\"tns:bookKey\">"
        "      <xs:selector xpath=\"tns:section/tns:reference\"/>"
        "      <xs:field xpath=\"@bookId\"/>"
        "    </xs:keyref>"
        "  </xs:element>"
        "</xs:schema>");
    return schemas;
}

std::shared_ptr<XmlSchemaSet> BuildDirectReaderSafeIdentitySchemas() {
    auto schemas = std::make_shared<XmlSchemaSet>();
    schemas->AddXml(
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:tns=\"urn:benchmark-schema-direct-reader-safe\" targetNamespace=\"urn:benchmark-schema-direct-reader-safe\">"
        "  <xs:complexType name=\"DerivedPayloadType\">"
        "    <xs:sequence>"
        "      <xs:element ref=\"tns:catalog\"/>"
        "    </xs:sequence>"
        "  </xs:complexType>"
        "  <xs:element name=\"root\">"
        "    <xs:complexType>"
        "      <xs:sequence>"
        "        <xs:element name=\"payload\" type=\"tns:DerivedPayloadType\"/>"
        "      </xs:sequence>"
        "    </xs:complexType>"
        "  </xs:element>"
        "  <xs:element name=\"catalog\">"
        "    <xs:complexType>"
        "      <xs:sequence>"
        "        <xs:element name=\"section\" maxOccurs=\"unbounded\">"
        "          <xs:complexType>"
        "            <xs:sequence>"
        "              <xs:element name=\"book\" maxOccurs=\"unbounded\">"
        "                <xs:complexType><xs:attribute name=\"id\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
        "              </xs:element>"
        "              <xs:element name=\"reference\" minOccurs=\"0\" maxOccurs=\"unbounded\">"
        "                <xs:complexType><xs:attribute name=\"bookId\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
        "              </xs:element>"
        "            </xs:sequence>"
        "          </xs:complexType>"
        "        </xs:element>"
        "      </xs:sequence>"
        "    </xs:complexType>"
        "    <xs:key name=\"bookKey\">"
        "      <xs:selector xpath=\"tns:section/tns:book\"/>"
        "      <xs:field xpath=\"@id\"/>"
        "    </xs:key>"
        "    <xs:keyref name=\"bookRef\" refer=\"tns:bookKey\">"
        "      <xs:selector xpath=\"tns:section/tns:reference\"/>"
        "      <xs:field xpath=\"@bookId\"/>"
        "    </xs:keyref>"
        "  </xs:element>"
        "</xs:schema>");
    return schemas;
}

std::shared_ptr<XmlSchemaSet> BuildBaselineNestedXsiTypeSchemas() {
    auto schemas = std::make_shared<XmlSchemaSet>();
    schemas->AddXml(
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:tns=\"urn:benchmark-schema-baseline\" targetNamespace=\"urn:benchmark-schema-baseline\">"
        "  <xs:complexType name=\"BasePayloadType\">"
        "    <xs:sequence/>"
        "  </xs:complexType>"
        "  <xs:complexType name=\"DerivedPayloadType\">"
        "    <xs:complexContent>"
        "      <xs:extension base=\"tns:BasePayloadType\">"
        "        <xs:sequence>"
        "          <xs:element ref=\"tns:catalog\"/>"
        "        </xs:sequence>"
        "      </xs:extension>"
        "    </xs:complexContent>"
        "  </xs:complexType>"
        "  <xs:element name=\"root\">"
        "    <xs:complexType>"
        "      <xs:sequence>"
        "        <xs:element name=\"payload\" type=\"tns:BasePayloadType\"/>"
        "      </xs:sequence>"
        "    </xs:complexType>"
        "  </xs:element>"
        "  <xs:element name=\"catalog\">"
        "    <xs:complexType>"
        "      <xs:sequence>"
        "        <xs:element name=\"section\" maxOccurs=\"unbounded\">"
        "          <xs:complexType>"
        "            <xs:sequence>"
        "              <xs:element name=\"book\" maxOccurs=\"unbounded\">"
        "                <xs:complexType><xs:attribute name=\"id\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
        "              </xs:element>"
        "              <xs:element name=\"reference\" minOccurs=\"0\" maxOccurs=\"unbounded\">"
        "                <xs:complexType><xs:attribute name=\"bookId\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
        "              </xs:element>"
        "            </xs:sequence>"
        "          </xs:complexType>"
        "        </xs:element>"
        "      </xs:sequence>"
        "    </xs:complexType>"
        "  </xs:element>"
        "</xs:schema>");
    return schemas;
}

std::shared_ptr<XmlSchemaSet> BuildNestedXsiTypeFallbackSchemas() {
    auto schemas = std::make_shared<XmlSchemaSet>();
    schemas->AddXml(
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:tns=\"urn:benchmark-schema-fallback\" targetNamespace=\"urn:benchmark-schema-fallback\">"
        "  <xs:complexType name=\"BasePayloadType\">"
        "    <xs:sequence/>"
        "  </xs:complexType>"
        "  <xs:complexType name=\"DerivedPayloadType\">"
        "    <xs:complexContent>"
        "      <xs:extension base=\"tns:BasePayloadType\">"
        "        <xs:sequence>"
        "          <xs:element ref=\"tns:catalog\"/>"
        "        </xs:sequence>"
        "      </xs:extension>"
        "    </xs:complexContent>"
        "  </xs:complexType>"
        "  <xs:element name=\"root\">"
        "    <xs:complexType>"
        "      <xs:sequence>"
        "        <xs:element name=\"payload\" type=\"tns:BasePayloadType\"/>"
        "      </xs:sequence>"
        "    </xs:complexType>"
        "  </xs:element>"
        "  <xs:element name=\"catalog\">"
        "    <xs:complexType>"
        "      <xs:sequence>"
        "        <xs:element name=\"section\" maxOccurs=\"unbounded\">"
        "          <xs:complexType>"
        "            <xs:sequence>"
        "              <xs:element name=\"book\" maxOccurs=\"unbounded\">"
        "                <xs:complexType><xs:attribute name=\"id\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
        "              </xs:element>"
        "              <xs:element name=\"reference\" minOccurs=\"0\" maxOccurs=\"unbounded\">"
        "                <xs:complexType><xs:attribute name=\"bookId\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
        "              </xs:element>"
        "            </xs:sequence>"
        "          </xs:complexType>"
        "        </xs:element>"
        "      </xs:sequence>"
        "    </xs:complexType>"
        "    <xs:key name=\"posBookKey\">"
        "      <xs:selector xpath=\"tns:section/tns:book[position()=2]\"/>"
        "      <xs:field xpath=\"@id\"/>"
        "    </xs:key>"
        "    <xs:keyref name=\"posBookRef\" refer=\"tns:posBookKey\">"
        "      <xs:selector xpath=\"tns:section/tns:reference[position()=1]\"/>"
        "      <xs:field xpath=\"@bookId\"/>"
        "    </xs:keyref>"
        "  </xs:element>"
        "</xs:schema>");
    return schemas;
}

std::shared_ptr<XmlSchemaSet> BuildDirectFallbackIdentitySchemas() {
    auto schemas = std::make_shared<XmlSchemaSet>();
    schemas->AddXml(
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:tns=\"urn:benchmark-schema-direct-fallback\" targetNamespace=\"urn:benchmark-schema-direct-fallback\">"
        "  <xs:complexType name=\"DerivedPayloadType\">"
        "    <xs:sequence>"
        "      <xs:element ref=\"tns:catalog\"/>"
        "    </xs:sequence>"
        "  </xs:complexType>"
        "  <xs:element name=\"root\">"
        "    <xs:complexType>"
        "      <xs:sequence>"
        "        <xs:element name=\"payload\" type=\"tns:DerivedPayloadType\"/>"
        "      </xs:sequence>"
        "    </xs:complexType>"
        "  </xs:element>"
        "  <xs:element name=\"catalog\">"
        "    <xs:complexType>"
        "      <xs:sequence>"
        "        <xs:element name=\"section\" maxOccurs=\"unbounded\">"
        "          <xs:complexType>"
        "            <xs:sequence>"
        "              <xs:element name=\"book\" maxOccurs=\"unbounded\">"
        "                <xs:complexType><xs:attribute name=\"id\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
        "              </xs:element>"
        "              <xs:element name=\"reference\" minOccurs=\"0\" maxOccurs=\"unbounded\">"
        "                <xs:complexType><xs:attribute name=\"bookId\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
        "              </xs:element>"
        "            </xs:sequence>"
        "          </xs:complexType>"
        "        </xs:element>"
        "      </xs:sequence>"
        "    </xs:complexType>"
        "    <xs:key name=\"posBookKey\">"
        "      <xs:selector xpath=\"tns:section/tns:book[position()=2]\"/>"
        "      <xs:field xpath=\"@id\"/>"
        "    </xs:key>"
        "    <xs:keyref name=\"posBookRef\" refer=\"tns:posBookKey\">"
        "      <xs:selector xpath=\"tns:section/tns:reference[position()=1]\"/>"
        "      <xs:field xpath=\"@bookId\"/>"
        "    </xs:keyref>"
        "  </xs:element>"
        "</xs:schema>");
    return schemas;
}

std::string BuildNestedXsiTypeValidationXml(const std::string& namespaceUri, const int sectionCount, const int booksPerSection) {
    std::ostringstream stream;
    stream << "<root xmlns=\"" << namespaceUri << "\" xmlns:tns=\"" << namespaceUri
           << "\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">";
    stream << "<payload xsi:type=\"tns:DerivedPayloadType\"><catalog>";
    for (int sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
        stream << "<section>";
        for (int bookIndex = 0; bookIndex < booksPerSection; ++bookIndex) {
            stream << "<book id=\"s" << sectionIndex << "-b" << bookIndex << "\"/>";
        }
        const int referencedBookIndex = booksPerSection > 1 ? 1 : 0;
        stream << "<reference bookId=\"s" << sectionIndex << "-b" << referencedBookIndex << "\"/>";
        stream << "</section>";
    }
    stream << "</catalog></payload></root>";
    return stream.str();
}

std::string BuildDirectIdentityValidationXml(const std::string& namespaceUri, const int sectionCount, const int booksPerSection) {
    std::ostringstream stream;
    stream << "<root xmlns=\"" << namespaceUri << "\">";
    stream << "<payload><catalog>";
    for (int sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
        stream << "<section>";
        for (int bookIndex = 0; bookIndex < booksPerSection; ++bookIndex) {
            stream << "<book id=\"s" << sectionIndex << "-b" << bookIndex << "\"/>";
        }
        const int referencedBookIndex = booksPerSection > 1 ? 1 : 0;
        stream << "<reference bookId=\"s" << sectionIndex << "-b" << referencedBookIndex << "\"/>";
        stream << "</section>";
    }
    stream << "</catalog></payload></root>";
    return stream.str();
}

XmlNamespaceManager BuildBenchmarkNamespaces() {
    XmlNamespaceManager namespaces;
    namespaces.AddNamespace("bk", "urn:books");
    namespaces.AddNamespace("m", "urn:meta");
    return namespaces;
}

void ConsumeNodeList(const XmlNodeList& nodes) {
    volatile std::size_t sink = nodes.Count();
    (void)sink;
}

void ConsumeReader(XmlReader& reader) {
    volatile std::size_t nodeCount = 0;
    volatile std::size_t attributeCount = 0;
    while (reader.Read()) {
        ++nodeCount;
        attributeCount += static_cast<std::size_t>(reader.AttributeCount());
    }
    (void)nodeCount;
    (void)attributeCount;
}

void ConsumeReaderFromFile(const std::string& path) {
    auto reader = XmlReader::CreateFromFile(path);
    ConsumeReader(reader);
}

void ConsumeXmlDocumentLoadFromFile(const std::string& path) {
    XmlDocument document;
    document.Load(path);
    volatile bool hasRoot = document.DocumentElement() != nullptr;
    (void)hasRoot;
}

void ConsumeXmlDocumentLoadFromIstreamFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Failed to open XML sample file: " + path);
    }

    XmlDocument document;
    document.Load(stream);
    volatile bool hasRoot = document.DocumentElement() != nullptr;
    (void)hasRoot;
}

void ConsumeXmlDocumentBuildBreakdownFromFile(const std::string& path, const DomBuildBreakdownMode mode) {
    auto reader = XmlReader::CreateFromFile(path);
    XmlDocument document;
    std::vector<std::shared_ptr<XmlElement>> elementStack;
    int entityExpansionDepth = 0;
    volatile std::size_t sink = 0;

    auto appendNode = [&](const std::shared_ptr<XmlNode>& node) {
        if (node == nullptr) {
            return;
        }
        if (elementStack.empty()) {
            document.AppendChild(node);
        } else {
            elementStack.back()->AppendChild(node);
        }
    };

    while (reader.Read()) {
        const auto nodeType = reader.NodeType();
        sink += static_cast<std::size_t>(nodeType);

        if (entityExpansionDepth > 0) {
            if (nodeType == XmlNodeType::EntityReference) {
                ++entityExpansionDepth;
            } else if (nodeType == XmlNodeType::EndEntity) {
                --entityExpansionDepth;
            }
            continue;
        }

        switch (nodeType) {
        case XmlNodeType::Element: {
            if (mode == DomBuildBreakdownMode::EventsOnly) {
                sink += static_cast<std::size_t>(reader.AttributeCount());
                if (!reader.IsEmptyElement()) {
                    sink += 1;
                }
                break;
            }

            auto element = document.CreateElement(reader.Name());
            for (int index = 0; index < reader.AttributeCount(); ++index) {
                reader.MoveToAttribute(index);
                element->SetAttribute(reader.Name(), reader.Value());
            }
            reader.MoveToElement();

            if (mode == DomBuildBreakdownMode::BuildTree) {
                appendNode(element);
            }

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
            if (mode == DomBuildBreakdownMode::EventsOnly) {
                sink += reader.Value().size();
                break;
            }
            if (mode == DomBuildBreakdownMode::BuildTree) {
                appendNode(document.CreateTextNode(reader.Value()));
            } else {
                auto node = document.CreateTextNode(reader.Value());
                sink += node->Value().size();
            }
            break;
        case XmlNodeType::CDATA:
            if (mode == DomBuildBreakdownMode::EventsOnly) {
                sink += reader.Value().size();
                break;
            }
            if (mode == DomBuildBreakdownMode::BuildTree) {
                appendNode(document.CreateCDataSection(reader.Value()));
            } else {
                auto node = document.CreateCDataSection(reader.Value());
                sink += node->Value().size();
            }
            break;
        case XmlNodeType::Whitespace:
            if (mode == DomBuildBreakdownMode::EventsOnly) {
                sink += reader.Value().size();
                break;
            }
            if (document.PreserveWhitespace()) {
                if (mode == DomBuildBreakdownMode::BuildTree) {
                    appendNode(document.CreateWhitespace(reader.Value()));
                } else {
                    auto node = document.CreateWhitespace(reader.Value());
                    sink += node->Value().size();
                }
            }
            break;
        case XmlNodeType::SignificantWhitespace:
            if (mode == DomBuildBreakdownMode::EventsOnly) {
                sink += reader.Value().size();
                break;
            }
            if (mode == DomBuildBreakdownMode::BuildTree) {
                appendNode(document.CreateTextNode(reader.Value()));
            } else {
                auto node = document.CreateTextNode(reader.Value());
                sink += node->Value().size();
            }
            break;
        case XmlNodeType::Comment:
            if (mode == DomBuildBreakdownMode::EventsOnly) {
                sink += reader.Value().size();
                break;
            }
            if (mode == DomBuildBreakdownMode::BuildTree) {
                appendNode(document.CreateComment(reader.Value()));
            } else {
                auto node = document.CreateComment(reader.Value());
                sink += node->Value().size();
            }
            break;
        case XmlNodeType::ProcessingInstruction:
            if (mode == DomBuildBreakdownMode::EventsOnly) {
                sink += reader.Name().size() + reader.Value().size();
                break;
            }
            if (mode == DomBuildBreakdownMode::BuildTree) {
                appendNode(document.CreateProcessingInstruction(reader.Name(), reader.Value()));
            } else {
                auto node = document.CreateProcessingInstruction(reader.Name(), reader.Value());
                sink += node->Name().size() + node->Value().size();
            }
            break;
        case XmlNodeType::XmlDeclaration:
            if (mode == DomBuildBreakdownMode::EventsOnly) {
                sink += reader.Value().size();
                break;
            }
            if (mode == DomBuildBreakdownMode::BuildTree) {
                appendNode(document.CreateXmlDeclaration());
            } else {
                auto node = document.CreateXmlDeclaration();
                sink += node->Name().size();
            }
            break;
        case XmlNodeType::DocumentType:
            if (mode == DomBuildBreakdownMode::EventsOnly) {
                sink += reader.Name().size();
                break;
            }
            if (mode == DomBuildBreakdownMode::BuildTree) {
                appendNode(document.CreateDocumentType(reader.Name()));
            } else {
                auto node = document.CreateDocumentType(reader.Name());
                sink += node->Name().size();
            }
            break;
        case XmlNodeType::EntityReference:
            if (mode == DomBuildBreakdownMode::EventsOnly) {
                sink += reader.Name().size();
            } else if (mode == DomBuildBreakdownMode::BuildTree) {
                appendNode(document.CreateEntityReference(reader.Name()));
            } else {
                auto node = document.CreateEntityReference(reader.Name());
                sink += node->Name().size();
            }
            entityExpansionDepth = 1;
            break;
        case XmlNodeType::EndEntity:
        case XmlNodeType::None:
        case XmlNodeType::Attribute:
        case XmlNodeType::Entity:
        case XmlNodeType::Document:
        case XmlNodeType::DocumentFragment:
        case XmlNodeType::Notation:
            break;
        }
    }

    if (mode == DomBuildBreakdownMode::BuildTree) {
        volatile bool hasRoot = document.DocumentElement() != nullptr;
        (void)hasRoot;
    }
    (void)sink;
}

void ConsumeReaderReadOnlyFromFile(const std::string& path) {
    auto reader = XmlReader::CreateFromFile(path);
    volatile std::size_t nodeCount = 0;
    while (reader.Read()) {
        ++nodeCount;
    }
    (void)nodeCount;
}

void ConsumeReaderNodeTypeFromFile(const std::string& path) {
    auto reader = XmlReader::CreateFromFile(path);
    volatile std::size_t sink = 0;
    while (reader.Read()) {
        sink += static_cast<std::size_t>(reader.NodeType());
    }
    (void)sink;
}

void ConsumeReaderAttributeCountFromFile(const std::string& path) {
    auto reader = XmlReader::CreateFromFile(path);
    volatile std::size_t sink = 0;
    while (reader.Read()) {
        sink += static_cast<std::size_t>(reader.AttributeCount());
    }
    (void)sink;
}

void ConsumeReaderDepthFromFile(const std::string& path) {
    auto reader = XmlReader::CreateFromFile(path);
    volatile std::size_t sink = 0;
    while (reader.Read()) {
        sink += static_cast<std::size_t>(reader.Depth());
    }
    (void)sink;
}

void ConsumeReaderValueLengthFromFile(const std::string& path) {
    auto reader = XmlReader::CreateFromFile(path);
    volatile std::size_t sink = 0;
    while (reader.Read()) {
        if (reader.HasValue()) {
            sink += reader.Value().size();
        }
    }
    (void)sink;
}

void ConsumeReaderFromIstreamFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Failed to open XML sample file: " + path);
    }

    auto reader = XmlReader::Create(stream);
    ConsumeReader(reader);
}

void ConsumeNamedSubtreeOnlyFromFile(const std::string& path, const std::string& elementName) {
    auto reader = XmlReader::CreateFromFile(path);
    if (!reader.ReadToFollowing(elementName)) {
        throw std::runtime_error("Real-file benchmark expected subtree element: " + elementName);
    }

    volatile std::size_t sink = 0;
    auto subtree = reader.ReadSubtree();
    while (subtree.Read()) {
        sink += static_cast<std::size_t>(subtree.NodeType());
        if (subtree.HasValue()) {
            sink += subtree.Value().size();
        }
    }
    (void)sink;
}

void ConsumeNamedSubtreeCreateFromFile(const std::string& path, const std::string& elementName) {
    auto reader = XmlReader::CreateFromFile(path);
    if (!reader.ReadToFollowing(elementName)) {
        throw std::runtime_error("Real-file benchmark expected subtree element: " + elementName);
    }

    volatile std::size_t sink = 0;
    auto subtree = reader.ReadSubtree();
    sink += static_cast<std::size_t>(subtree.NodeType());
    (void)sink;
}

void ConsumeNamedSubtreeReplayFromFile(const std::string& path, const std::string& elementName) {
    auto reader = XmlReader::CreateFromFile(path);
    if (!reader.ReadToFollowing(elementName)) {
        throw std::runtime_error("Real-file benchmark expected subtree element: " + elementName);
    }

    volatile std::size_t sink = 0;
    auto subtree = reader.ReadSubtree();

    while (reader.Read()) {
        sink += static_cast<std::size_t>(reader.AttributeCount());
        if (reader.HasValue()) {
            sink += reader.Value().size();
        }
    }

    while (subtree.Read()) {
        sink += static_cast<std::size_t>(subtree.NodeType());
        if (subtree.HasValue()) {
            sink += subtree.Value().size();
        }
    }

    (void)sink;
}

ExternalFileObservation ObserveExternalFile(const ExternalFileSpec& spec) {
    auto reader = XmlReader::CreateFromFile(spec.path);
    ExternalFileObservation observation;

    while (reader.Read()) {
        ++observation.nodeCount;
        observation.attributeCount += static_cast<std::size_t>(reader.AttributeCount());
        observation.maxDepth = std::max(observation.maxDepth, reader.Depth());

        if (reader.HasValue() && reader.Value().size() > observation.maxValueBytes) {
            observation.maxValueBytes = reader.Value().size();
            observation.maxValueNodeType = reader.NodeType();
        }

        switch (reader.NodeType()) {
        case XmlNodeType::Element:
            ++observation.elementCount;
            if (observation.rootName.empty() && reader.Depth() == 0) {
                observation.rootName = reader.Name();
            } else if (observation.firstChildElementName.empty() && reader.Depth() == 1) {
                observation.firstChildElementName = reader.Name();
            }
            if (reader.Name() == spec.subtreeElementName) {
                ++observation.subtreeMatchCount;
            }
            break;
        case XmlNodeType::EndElement:
            ++observation.endElementCount;
            break;
        case XmlNodeType::Text:
            ++observation.textCount;
            break;
        case XmlNodeType::Whitespace:
            ++observation.whitespaceCount;
            break;
        case XmlNodeType::SignificantWhitespace:
            ++observation.significantWhitespaceCount;
            break;
        case XmlNodeType::Comment:
            ++observation.commentCount;
            break;
        case XmlNodeType::CDATA:
            ++observation.cdataCount;
            break;
        case XmlNodeType::ProcessingInstruction:
        case XmlNodeType::XmlDeclaration:
            ++observation.processingInstructionCount;
            break;
        default:
            break;
        }
    }

    return observation;
}

void PrintExternalFileObservation(const ExternalFileSpec& spec, const ExternalFileObservation& observation) {
    std::cout << "  sample: root=" << observation.rootName
              << " first_child=" << observation.firstChildElementName
              << " subtree_element=" << spec.subtreeElementName
              << " subtree_matches=" << observation.subtreeMatchCount
              << " max_depth=" << observation.maxDepth
              << '\n';
    std::cout << "  sample: nodes=" << observation.nodeCount
              << " elements=" << observation.elementCount
              << " end_elements=" << observation.endElementCount
              << " text=" << observation.textCount
              << " whitespace=" << observation.whitespaceCount
              << " significant_whitespace=" << observation.significantWhitespaceCount
              << " comments=" << observation.commentCount
              << " cdata=" << observation.cdataCount
              << " attributes=" << observation.attributeCount
              << '\n';
    std::cout << "  sample: max_value_bytes=" << observation.maxValueBytes
              << " max_value_node_type=" << NodeTypeName(observation.maxValueNodeType)
              << '\n' << std::flush;
}

void ConsumeReaderSubtreeReplay(const std::string& xml) {
    std::istringstream stream(xml);
    auto reader = XmlReader::Create(stream);

    volatile std::size_t sink = 0;
    if (!reader.Read() || reader.Name() != "root") {
        throw std::runtime_error("Streaming benchmark expected root element");
    }
    if (!reader.Read() || reader.Name() != "child") {
        throw std::runtime_error("Streaming benchmark expected child element");
    }

    auto subtree = reader.ReadSubtree();
    while (reader.Read()) {
        if (reader.NodeType() == XmlNodeType::Text || reader.NodeType() == XmlNodeType::CDATA) {
            sink += reader.Value().size();
        }
    }

    if (!subtree.Read() || subtree.Name() != "child") {
        throw std::runtime_error("Streaming benchmark subtree expected child root");
    }

    const std::string value = subtree.ReadElementContentAsString();
    sink += value.size();
    (void)sink;
}

void ConsumeReaderSubtreeCreateOnly(const std::string& xml) {
    std::istringstream stream(xml);
    auto reader = XmlReader::Create(stream);

    volatile std::size_t sink = 0;
    if (!reader.Read() || reader.Name() != "root") {
        throw std::runtime_error("Streaming benchmark expected root element");
    }
    if (!reader.Read() || reader.Name() != "child") {
        throw std::runtime_error("Streaming benchmark expected child element");
    }

    auto subtree = reader.ReadSubtree();
    sink += static_cast<std::size_t>(subtree.NodeType());
    (void)sink;
}

void ConsumeReaderParentAdvanceAfterSubtree(const std::string& xml) {
    std::istringstream stream(xml);
    auto reader = XmlReader::Create(stream);

    volatile std::size_t sink = 0;
    if (!reader.Read() || reader.Name() != "root") {
        throw std::runtime_error("Streaming benchmark expected root element");
    }
    if (!reader.Read() || reader.Name() != "child") {
        throw std::runtime_error("Streaming benchmark expected child element");
    }

    auto subtree = reader.ReadSubtree();
    sink += static_cast<std::size_t>(subtree.NodeType());
    while (reader.Read()) {
        if (reader.NodeType() == XmlNodeType::Text || reader.NodeType() == XmlNodeType::CDATA) {
            sink += reader.Value().size();
        }
    }
    (void)sink;
}

void ConsumeReaderSubtreeReplayReadOnly(const std::string& xml) {
    std::istringstream stream(xml);
    auto reader = XmlReader::Create(stream);

    volatile std::size_t sink = 0;
    if (!reader.Read() || reader.Name() != "root") {
        throw std::runtime_error("Streaming benchmark expected root element");
    }
    if (!reader.Read() || reader.Name() != "child") {
        throw std::runtime_error("Streaming benchmark expected child element");
    }

    auto subtree = reader.ReadSubtree();
    while (reader.Read()) {
        if (reader.NodeType() == XmlNodeType::Text || reader.NodeType() == XmlNodeType::CDATA) {
            sink += reader.Value().size();
        }
    }

    if (!subtree.Read() || subtree.Name() != "child") {
        throw std::runtime_error("Streaming benchmark subtree expected child root");
    }

    sink += subtree.ReadElementContentAsString().size();
    (void)sink;
}

PreparedReaderSubtreeReplayState PrepareReaderSubtreeReplayState(const std::string& xml) {
    auto stream = std::make_shared<std::istringstream>(xml);
    auto reader = std::make_unique<XmlReader>(XmlReader::Create(*stream));

    if (!reader->Read() || reader->Name() != "root") {
        throw std::runtime_error("Streaming benchmark expected root element");
    }
    if (!reader->Read() || reader->Name() != "child") {
        throw std::runtime_error("Streaming benchmark expected child element");
    }

    auto subtree = std::make_unique<XmlReader>(reader->ReadSubtree());

    std::size_t sink = 0;
    while (reader->Read()) {
        if (reader->NodeType() == XmlNodeType::Text || reader->NodeType() == XmlNodeType::CDATA) {
            sink += reader->Value().size();
        }
    }

    return PreparedReaderSubtreeReplayState{
        std::move(stream),
        std::move(reader),
        std::move(subtree),
        sink};
}

PreparedSubtreeCreateState PrepareSubtreeCreateState(const std::string& xml) {
    auto stream = std::make_shared<std::istringstream>(xml);
    auto reader = std::make_unique<XmlReader>(XmlReader::Create(*stream));

    if (!reader->Read() || reader->Name() != "root") {
        throw std::runtime_error("Streaming benchmark expected root element");
    }
    if (!reader->Read() || reader->Name() != "child") {
        throw std::runtime_error("Streaming benchmark expected child element");
    }

    return PreparedSubtreeCreateState{
        std::move(stream),
        std::move(reader),
        0};
}

PreparedSubtreeCreateState PrepareSubtreeCreateStateWithCachedBounds(const std::string& xml) {
    auto state = PrepareSubtreeCreateState(xml);
    auto subtree = state.reader->ReadSubtree();
    state.sink += static_cast<std::size_t>(subtree.NodeType());
    return state;
}

PreparedParentAdvanceState PrepareParentAdvanceState(const std::string& xml) {
    auto stream = std::make_shared<std::istringstream>(xml);
    auto reader = std::make_unique<XmlReader>(XmlReader::Create(*stream));

    if (!reader->Read() || reader->Name() != "root") {
        throw std::runtime_error("Streaming benchmark expected root element");
    }
    if (!reader->Read() || reader->Name() != "child") {
        throw std::runtime_error("Streaming benchmark expected child element");
    }

    auto subtree = std::make_unique<XmlReader>(reader->ReadSubtree());
    std::size_t sink = static_cast<std::size_t>(subtree->NodeType());
    return PreparedParentAdvanceState{
        std::move(stream),
        std::move(reader),
        std::move(subtree),
        sink};
}

PreparedParentAdvanceState PrepareParentAdvanceStateAtTextNode(const std::string& xml) {
    auto state = PrepareParentAdvanceState(xml);
    if (!state.reader->Read()) {
        throw std::runtime_error("Streaming benchmark expected child text node");
    }
    return state;
}

PreparedParentAdvanceState PrepareParentAdvanceStateAfterTextValue(const std::string& xml) {
    auto state = PrepareParentAdvanceStateAtTextNode(xml);
    state.sink += state.reader->Value().size();
    return state;
}

PreparedParentAdvanceState PrepareParentAdvanceStateAtFirstSibling(const std::string& xml) {
    auto state = PrepareParentAdvanceState(xml);
    while (state.reader->Read()) {
        if (state.reader->NodeType() == XmlNodeType::Text || state.reader->NodeType() == XmlNodeType::CDATA) {
            state.sink += state.reader->Value().size();
        }
        if (state.reader->NodeType() == XmlNodeType::Element && state.reader->Name() == "other") {
            return state;
        }
    }

    throw std::runtime_error("Streaming benchmark expected first sibling element");
}

PreparedReaderSubtreeReplayState PrepareReaderSubtreeReplayStateAtTextNode(const std::string& xml) {
    auto state = PrepareReaderSubtreeReplayState(xml);
    if (!state.subtree->Read() || state.subtree->Name() != "child") {
        throw std::runtime_error("Streaming benchmark subtree expected child root");
    }
    if (!state.subtree->Read()) {
        throw std::runtime_error("Streaming benchmark subtree expected text node");
    }
    return state;
}

PreparedReaderSubtreeReplayState PrepareReaderSubtreeReplayStateAtRootNode(const std::string& xml) {
    auto state = PrepareReaderSubtreeReplayState(xml);
    if (!state.subtree->Read() || state.subtree->Name() != "child") {
        throw std::runtime_error("Streaming benchmark subtree expected child root");
    }
    return state;
}

PreparedReaderSubtreeReplayState PrepareReaderSubtreeReplayStateAfterTextValue(const std::string& xml) {
    auto state = PrepareReaderSubtreeReplayStateAtTextNode(xml);
    state.sink += state.subtree->Value().size();
    return state;
}

PreparedReaderSubtreeReplayState PrepareReaderSubtreeReplayStateWithCachedTextValue(const std::string& xml) {
    auto state = PrepareReaderSubtreeReplayStateAtTextNode(xml);
    state.sink += state.subtree->Value().size();
    return state;
}

PreparedReaderSubtreeReplayState PrepareReaderSubtreeReplayStateAtFirstComment(const std::string& xml) {
    auto state = PrepareReaderSubtreeReplayStateAfterTextValue(xml);
    if (!state.subtree->Read() || state.subtree->NodeType() != XmlNodeType::Comment) {
        throw std::runtime_error("Streaming benchmark subtree expected comment node after first text segment");
    }
    return state;
}

PreparedReaderSubtreeReplayState PrepareReaderSubtreeReplayStateAtEndElement(const std::string& xml) {
    auto state = PrepareReaderSubtreeReplayStateAfterTextValue(xml);
    if (!state.subtree->Read() || state.subtree->NodeType() != XmlNodeType::EndElement) {
        throw std::runtime_error("Streaming benchmark subtree expected end element");
    }
    return state;
}

void ConsumePreparedReaderSubtreeFirstTextNode(PreparedReaderSubtreeReplayState& state) {
    if (!state.subtree->Read() || state.subtree->Name() != "child") {
        throw std::runtime_error("Streaming benchmark subtree expected child root");
    }
    if (!state.subtree->Read()) {
        throw std::runtime_error("Streaming benchmark subtree expected text node");
    }

    state.sink += static_cast<std::size_t>(state.subtree->NodeType());
}

void ConsumePreparedReaderSubtreeRootNodeOnly(PreparedReaderSubtreeReplayState& state) {
    if (!state.subtree->Read() || state.subtree->Name() != "child") {
        throw std::runtime_error("Streaming benchmark subtree expected child root");
    }

    state.sink += static_cast<std::size_t>(state.subtree->NodeType());
}

void ConsumePreparedReaderSubtreeTextNodeOnly(PreparedReaderSubtreeReplayState& state) {
    if (!state.subtree->Read()) {
        throw std::runtime_error("Streaming benchmark subtree expected text node");
    }

    state.sink += static_cast<std::size_t>(state.subtree->NodeType());
}

void ConsumePreparedReaderSubtreeTextValueOnly(PreparedReaderSubtreeReplayState& state) {
    state.sink += state.subtree->Value().size();
}

void ConsumePreparedReaderSubtreeCachedTextValueOnly(PreparedReaderSubtreeReplayState& state) {
    state.sink += state.subtree->Value().size();
}

void ConsumePreparedReaderSubtreeCachedTextReadStringOnly(PreparedReaderSubtreeReplayState& state) {
    state.sink += state.subtree->ReadString().size();
}

void ConsumePreparedReaderSubtreeTextReadStringOnly(PreparedReaderSubtreeReplayState& state) {
    state.sink += state.subtree->ReadString().size();
}

void ConsumePreparedReaderSubtreeRootReadStringOnly(PreparedReaderSubtreeReplayState& state) {
    state.sink += state.subtree->ReadString().size();
}

void ConsumePreparedReaderSubtreeRootReadElementContentOnly(PreparedReaderSubtreeReplayState& state) {
    state.sink += state.subtree->ReadElementContentAsString().size();
}

void ConsumePreparedReaderSubtreeRootTraverseOnly(PreparedReaderSubtreeReplayState& state) {
    while (state.subtree->Read()) {
        state.sink += static_cast<std::size_t>(state.subtree->NodeType());
    }
}

void ConsumePreparedReaderSubtreeRootTraverseWithValue(PreparedReaderSubtreeReplayState& state) {
    while (state.subtree->Read()) {
        const auto nodeType = state.subtree->NodeType();
        state.sink += static_cast<std::size_t>(nodeType);
        if (nodeType == XmlNodeType::Text
            || nodeType == XmlNodeType::CDATA
            || nodeType == XmlNodeType::Whitespace
            || nodeType == XmlNodeType::SignificantWhitespace) {
            state.sink += state.subtree->Value().size();
        }
    }
}

void ConsumePreparedReaderSubtreeCommentTailOnly(PreparedReaderSubtreeReplayState& state) {
    while (state.subtree->Read()) {
        state.sink += static_cast<std::size_t>(state.subtree->NodeType());
    }
}

void ConsumePreparedReaderSubtreeCommentTailWithValue(PreparedReaderSubtreeReplayState& state) {
    while (state.subtree->Read()) {
        const auto nodeType = state.subtree->NodeType();
        state.sink += static_cast<std::size_t>(nodeType);
        if (nodeType == XmlNodeType::Text
            || nodeType == XmlNodeType::CDATA
            || nodeType == XmlNodeType::Whitespace
            || nodeType == XmlNodeType::SignificantWhitespace) {
            state.sink += state.subtree->Value().size();
        }
    }
}

void ConsumePreparedReaderSubtreeTextTailOnly(PreparedReaderSubtreeReplayState& state) {
    while (state.subtree->Read()) {
        state.sink += static_cast<std::size_t>(state.subtree->NodeType());
    }
}

void ConsumePreparedReaderSubtreeNextNodeOnly(PreparedReaderSubtreeReplayState& state) {
    if (!state.subtree->Read()) {
        throw std::runtime_error("Streaming benchmark subtree expected next node");
    }

    state.sink += static_cast<std::size_t>(state.subtree->NodeType());
}

void ConsumePreparedReaderSubtreeEofOnly(PreparedReaderSubtreeReplayState& state) {
    if (state.subtree->Read()) {
        throw std::runtime_error("Streaming benchmark subtree expected EOF");
    }

    ++state.sink;
}

void ConsumePreparedParentFirstTextNodeOnly(PreparedParentAdvanceState& state) {
    if (!state.reader->Read()) {
        throw std::runtime_error("Streaming benchmark expected child text node");
    }

    state.sink += static_cast<std::size_t>(state.reader->NodeType());
}

void ConsumePreparedParentTextValueOnly(PreparedParentAdvanceState& state) {
    state.sink += state.reader->Value().size();
}

void ConsumePreparedParentTextTailOnly(PreparedParentAdvanceState& state) {
    while (state.reader->Read()) {
        if (state.reader->NodeType() == XmlNodeType::Text || state.reader->NodeType() == XmlNodeType::CDATA) {
            state.sink += state.reader->Value().size();
        }
        if (state.reader->NodeType() == XmlNodeType::Element && state.reader->Name() == "other") {
            return;
        }
    }

    throw std::runtime_error("Streaming benchmark expected first sibling element");
}

void ConsumePreparedParentChildTailOnly(PreparedParentAdvanceState& state) {
    while (state.reader->Read()) {
        if (state.reader->NodeType() == XmlNodeType::Text || state.reader->NodeType() == XmlNodeType::CDATA) {
            state.sink += state.reader->Value().size();
        }
        if (state.reader->NodeType() == XmlNodeType::Element && state.reader->Name() == "other") {
            return;
        }
    }

    throw std::runtime_error("Streaming benchmark expected first sibling element");
}

void ConsumePreparedParentSiblingTailOnly(PreparedParentAdvanceState& state) {
    do {
        if (state.reader->NodeType() == XmlNodeType::Text || state.reader->NodeType() == XmlNodeType::CDATA) {
            state.sink += state.reader->Value().size();
        }
    } while (state.reader->Read());
}

void ConsumePreparedSubtreeCreateOnly(PreparedSubtreeCreateState& state) {
    auto subtree = state.reader->ReadSubtree();
    state.sink += static_cast<std::size_t>(subtree.NodeType());
}

void PrintResult(const BenchmarkResult& result) {
    std::cout
        << std::left << std::setw(34) << result.name
        << " payload=" << std::setw(9) << result.payloadBytes
        << " iterations=" << std::setw(3) << result.iterations
        << " avg_ms=" << std::fixed << std::setprecision(3) << std::setw(9) << result.averageMs
        << " min_ms=" << std::setw(9) << result.minMs
        << " max_ms=" << std::setw(9) << result.maxMs
    << '\n' << std::flush;
}

template <typename TAction>
void RunBenchmarkAndPrint(const std::string& name, const std::size_t payloadBytes, const int iterations, TAction action) {
    try {
        PrintResult(RunBenchmark(name, payloadBytes, iterations, std::move(action)));
    } catch (const std::bad_alloc&) {
        std::cout << std::left << std::setw(34) << name
                  << " payload=" << std::setw(9) << payloadBytes
                  << " iterations=" << std::setw(3) << iterations
                  << " failed=bad_alloc\n";
    } catch (const std::exception& exception) {
        std::cout << std::left << std::setw(34) << name
                  << " payload=" << std::setw(9) << payloadBytes
                  << " iterations=" << std::setw(3) << iterations
                  << " failed=" << exception.what() << '\n';
    }
}

template <typename TAction>
void RunBenchmarkAndPrintNoWarmup(const std::string& name, const std::size_t payloadBytes, const int iterations, TAction action) {
    try {
        PrintResult(RunBenchmarkNoWarmup(name, payloadBytes, iterations, std::move(action)));
    } catch (const std::bad_alloc&) {
        std::cout << std::left << std::setw(34) << name
                  << " payload=" << std::setw(9) << payloadBytes
                  << " iterations=" << std::setw(3) << iterations
                  << " failed=bad_alloc\n";
    } catch (const std::exception& exception) {
        std::cout << std::left << std::setw(34) << name
                  << " payload=" << std::setw(9) << payloadBytes
                  << " iterations=" << std::setw(3) << iterations
                  << " failed=" << exception.what() << '\n';
    }
}

void RunSchemaValidationSuite(const std::string& label,
                              const std::string& xml,
                              const XmlReaderSettings& settings,
                              const int iterations,
                              const bool runStreamVariant = true) {
    std::cout << "\n[schema] " << label << " bytes=" << xml.size() << " iterations=" << iterations << '\n';

    RunBenchmarkAndPrint(
        "Schema XmlReader::Create(string)",
        xml.size(),
        iterations,
        [&xml, &settings]() {
            auto reader = XmlReader::Create(xml, settings);
            volatile bool eof = reader.IsEOF();
            (void)eof;
        });

    RunBenchmarkAndPrint(
        "Schema XmlReader::Create(string)+Read",
        xml.size(),
        iterations,
        [&xml, &settings]() {
            auto reader = XmlReader::Create(xml, settings);
            ConsumeReader(reader);
        });

    if (runStreamVariant) {
        RunBenchmarkAndPrint(
            "Schema XmlReader::Create(stream)",
            xml.size(),
            iterations,
            [&xml, &settings]() {
                std::istringstream stream(xml);
                auto reader = XmlReader::Create(stream, settings);
                volatile bool eof = reader.IsEOF();
                (void)eof;
            });

        RunBenchmarkAndPrint(
            "Schema XmlReader::Create(stream)+Read",
            xml.size(),
            iterations,
            [&xml, &settings]() {
                std::istringstream stream(xml);
                auto reader = XmlReader::Create(stream, settings);
                ConsumeReader(reader);
            });
    }
}

void RunDomFallbackBreakdownSuite(const std::string& label,
                                  const std::string& xml,
                                  const XmlSchemaSet& schemas,
                                  const int iterations) {
    std::cout << "\n[fallback-dom] " << label << " bytes=" << xml.size() << " iterations=" << iterations << '\n';

    RunBenchmarkAndPrint(
        "DOM XmlDocument::Parse",
        xml.size(),
        iterations,
        [&xml]() {
            const auto document = XmlDocument::Parse(xml);
            volatile bool hasRoot = document->DocumentElement() != nullptr;
            (void)hasRoot;
        });

    const auto parsedDocument = XmlDocument::Parse(xml);
    RunBenchmarkAndPrint(
        "DOM XmlDocument::Validate",
        xml.size(),
        iterations,
        [&parsedDocument, &schemas]() {
            parsedDocument->Validate(schemas);
        });

    RunBenchmarkAndPrint(
        "DOM Parse+Validate",
        xml.size(),
        iterations,
        [&xml, &schemas]() {
            const auto document = XmlDocument::Parse(xml);
            document->Validate(schemas);
        });
}

void RunReaderStreamingSuite(const std::string& label,
                             const std::string& xml,
                             const int iterations) {
    std::cout << "\n[reader-streaming] " << label << " bytes=" << xml.size() << " iterations=" << iterations << '\n';

    std::cout << "  step: Stream XmlReader::Create+Read\n";
    RunBenchmarkAndPrint(
        "Stream XmlReader::Create+Read",
        xml.size(),
        iterations,
        [&xml]() {
            std::istringstream stream(xml);
            auto reader = XmlReader::Create(stream);
            ConsumeReader(reader);
        });

    std::cout << "  step: Stream ReadElementContent\n";
    RunBenchmarkAndPrint(
        "Stream ReadElementContent",
        xml.size(),
        iterations,
        [&xml]() {
            std::istringstream stream(xml);
            auto reader = XmlReader::Create(stream);
            volatile std::size_t sink = 0;
            while (reader.Read()) {
                if (reader.NodeType() == XmlNodeType::Element && reader.Name() == "child") {
                    sink += reader.ReadElementContentAsString().size();
                    break;
                }
            }
            (void)sink;
        });

    std::cout << "  step: Stream ReadSubtree create\n";
    RunBenchmarkAndPrint(
        "Stream ReadSubtree create",
        xml.size(),
        iterations,
        [&xml]() {
            ConsumeReaderSubtreeCreateOnly(xml);
        });

    std::cout << "  step: Stream ReadSubtree create cached bounds\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream ReadSubtree create cached bounds",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareSubtreeCreateStateWithCachedBounds(xml);
        },
        [](PreparedSubtreeCreateState& state) {
            ConsumePreparedSubtreeCreateOnly(state);
        }));

    std::cout << "  step: Stream Parent advance\n";
    RunBenchmarkAndPrint(
        "Stream Parent advance",
        xml.size(),
        iterations,
        [&xml]() {
            ConsumeReaderParentAdvanceAfterSubtree(xml);
        });

    std::cout << "  step: Stream Parent child tail\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Parent child tail",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareParentAdvanceState(xml);
        },
        [](PreparedParentAdvanceState& state) {
            ConsumePreparedParentChildTailOnly(state);
        }));

    std::cout << "  step: Stream Parent first text node\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Parent first text node",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareParentAdvanceState(xml);
        },
        [](PreparedParentAdvanceState& state) {
            ConsumePreparedParentFirstTextNodeOnly(state);
        }));

    std::cout << "  step: Stream Parent text value\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Parent text value",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareParentAdvanceStateAtTextNode(xml);
        },
        [](PreparedParentAdvanceState& state) {
            ConsumePreparedParentTextValueOnly(state);
        }));

    std::cout << "  step: Stream Parent text tail\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Parent text tail",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareParentAdvanceStateAfterTextValue(xml);
        },
        [](PreparedParentAdvanceState& state) {
            ConsumePreparedParentTextTailOnly(state);
        }));

    std::cout << "  step: Stream Parent sibling tail\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Parent sibling tail",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareParentAdvanceStateAtFirstSibling(xml);
        },
        [](PreparedParentAdvanceState& state) {
            ConsumePreparedParentSiblingTailOnly(state);
        }));

    std::cout << "  step: Stream Subtree replay read\n";
    RunBenchmarkAndPrint(
        "Stream Subtree replay read",
        xml.size(),
        iterations,
        [&xml]() {
            ConsumeReaderSubtreeReplayReadOnly(xml);
        });

    std::cout << "  step: Stream Subtree first text node\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree first text node",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayState(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeFirstTextNode(state);
        }));

    std::cout << "  step: Stream Subtree root node\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree root node",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayState(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeRootNodeOnly(state);
        }));

    std::cout << "  step: Stream Subtree text node\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree text node",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAtRootNode(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeTextNodeOnly(state);
        }));

    std::cout << "  step: Stream Subtree text value\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree text value",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAtTextNode(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeTextValueOnly(state);
        }));

    std::cout << "  step: Stream Subtree cached text value\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree cached text value",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateWithCachedTextValue(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeCachedTextValueOnly(state);
        }));

    std::cout << "  step: Stream Subtree text read string\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree text read string",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAtTextNode(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeTextReadStringOnly(state);
        }));

    std::cout << "  step: Stream Subtree cached text read string\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree cached text read string",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateWithCachedTextValue(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeCachedTextReadStringOnly(state);
        }));

    std::cout << "  step: Stream Subtree text tail\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree text tail",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAfterTextValue(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeTextTailOnly(state);
        }));

    std::cout << "  step: Stream Subtree next node\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree next node",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAfterTextValue(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeNextNodeOnly(state);
        }));

    std::cout << "  step: Stream Subtree EOF read\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree EOF read",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAtEndElement(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeEofOnly(state);
        }));

    std::cout << "  step: Stream ReadSubtree replay\n";
    RunBenchmarkAndPrint(
        "Stream ReadSubtree replay",
        xml.size(),
        iterations,
        [&xml]() {
            ConsumeReaderSubtreeReplay(xml);
        });
}

void RunSegmentedReaderStreamingSuite(const std::string& label,
                                      const std::string& xml,
                                      const int iterations) {
    std::cout << "\n[reader-streaming-segmented] " << label << " bytes=" << xml.size() << " iterations=" << iterations << '\n';

    std::cout << "  step: Stream ReadElementContent\n";
    RunBenchmarkAndPrint(
        "Stream ReadElementContent",
        xml.size(),
        iterations,
        [&xml]() {
            std::istringstream stream(xml);
            auto reader = XmlReader::Create(stream);
            volatile std::size_t sink = 0;
            while (reader.Read()) {
                if (reader.NodeType() == XmlNodeType::Element && reader.Name() == "child") {
                    sink += reader.ReadElementContentAsString().size();
                    break;
                }
            }
            (void)sink;
        });

    std::cout << "  step: Stream ReadSubtree replay\n";
    RunBenchmarkAndPrint(
        "Stream ReadSubtree replay",
        xml.size(),
        iterations,
        [&xml]() {
            ConsumeReaderSubtreeReplay(xml);
        });

    std::cout << "  step: Stream Subtree first text node\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree first text node",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayState(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeFirstTextNode(state);
        }));

    std::cout << "  step: Stream Subtree root read string\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree root read string",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAtRootNode(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeRootReadStringOnly(state);
        }));

    std::cout << "  step: Stream Subtree root traverse\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree root traverse",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAtRootNode(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeRootTraverseOnly(state);
        }));

    std::cout << "  step: Stream Subtree root traverse value\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree root traverse value",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAtRootNode(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeRootTraverseWithValue(state);
        }));

    std::cout << "  step: Stream Subtree root element content\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree root element content",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAtRootNode(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeRootReadElementContentOnly(state);
        }));

    std::cout << "  step: Stream Subtree text to comment\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree text to comment",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAfterTextValue(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeNextNodeOnly(state);
        }));

    std::cout << "  step: Stream Subtree comment to next text\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree comment to next text",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAtFirstComment(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeNextNodeOnly(state);
        }));

    std::cout << "  step: Stream Subtree comment tail\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree comment tail",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAtFirstComment(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeCommentTailOnly(state);
        }));

    std::cout << "  step: Stream Subtree comment tail value\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree comment tail value",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAtFirstComment(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeCommentTailWithValue(state);
        }));

    std::cout << "  step: Stream Subtree text value\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree text value",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAtTextNode(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeTextValueOnly(state);
        }));

    std::cout << "  step: Stream Subtree cached text value\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree cached text value",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateWithCachedTextValue(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeCachedTextValueOnly(state);
        }));

    std::cout << "  step: Stream Subtree text read string\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree text read string",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateAtTextNode(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeTextReadStringOnly(state);
        }));

    std::cout << "  step: Stream Subtree cached text read string\n";
    PrintResult(RunBenchmarkWithSetup(
        "Stream Subtree cached text read string",
        xml.size(),
        iterations,
        [&xml]() {
            return PrepareReaderSubtreeReplayStateWithCachedTextValue(xml);
        },
        [](PreparedReaderSubtreeReplayState& state) {
            ConsumePreparedReaderSubtreeCachedTextReadStringOnly(state);
        }));
}

void RunExternalFileSuite(const ExternalFileSpec& spec) {
    const std::size_t payloadBytes = GetFileSizeOrZero(spec.path);
    std::cout << "\n[real-file] " << spec.label << " bytes=" << payloadBytes << " iterations=" << spec.iterations << '\n';
    std::cout << "  path: " << spec.path << '\n';
    std::cout << "  subtree element: " << spec.subtreeElementName << '\n' << std::flush;

    const bool exhaustiveMode = GetEnvironmentVariableOrEmpty("LIBXML_REAL_XML_EXHAUSTIVE") == "1";
    std::optional<ExternalFileObservation> capturedObservation;

    std::cout << "  step: File XmlDocument::Load(path eager)\n" << std::flush;
    RunBenchmarkAndPrintNoWarmup(
        "File XmlDocument::Load(path eager)",
        payloadBytes,
        spec.iterations,
        [&spec]() {
            ConsumeXmlDocumentLoadFromFile(spec.path);
        });

    std::cout << "  step: File XmlDocument::Load(stream)\n" << std::flush;
    RunBenchmarkAndPrintNoWarmup(
        "File XmlDocument::Load(stream)",
        payloadBytes,
        spec.iterations,
        [&spec]() {
            ConsumeXmlDocumentLoadFromIstreamFile(spec.path);
        });

    std::cout << "  step: File DOM builder events only\n" << std::flush;
    RunBenchmarkAndPrintNoWarmup(
        "File DOM builder events only",
        payloadBytes,
        spec.iterations,
        [&spec]() {
            ConsumeXmlDocumentBuildBreakdownFromFile(spec.path, DomBuildBreakdownMode::EventsOnly);
        });

    std::cout << "  step: File DOM create detached nodes\n" << std::flush;
    RunBenchmarkAndPrintNoWarmup(
        "File DOM create detached nodes",
        payloadBytes,
        spec.iterations,
        [&spec]() {
            ConsumeXmlDocumentBuildBreakdownFromFile(spec.path, DomBuildBreakdownMode::DetachedNodes);
        });

    std::cout << "  step: File DOM build tree\n" << std::flush;
    RunBenchmarkAndPrintNoWarmup(
        "File DOM build tree",
        payloadBytes,
        spec.iterations,
        [&spec]() {
            ConsumeXmlDocumentBuildBreakdownFromFile(spec.path, DomBuildBreakdownMode::BuildTree);
        });

    std::cout << "  step: File XmlReader::CreateFromFile(stream)+Read only\n" << std::flush;
    RunBenchmarkAndPrintNoWarmup(
        "File XmlReader::CreateFromFile(stream)+Read only",
        payloadBytes,
        spec.iterations,
        [&spec]() {
            ConsumeReaderReadOnlyFromFile(spec.path);
        });

    std::cout << "  step: File Read plus NodeType\n" << std::flush;
    RunBenchmarkAndPrintNoWarmup(
        "File Read plus NodeType",
        payloadBytes,
        spec.iterations,
        [&spec]() {
            ConsumeReaderNodeTypeFromFile(spec.path);
        });

    std::cout << "  step: File Read plus AttributeCount\n" << std::flush;
    RunBenchmarkAndPrintNoWarmup(
        "File Read plus AttributeCount",
        payloadBytes,
        spec.iterations,
        [&spec]() {
            ConsumeReaderAttributeCountFromFile(spec.path);
        });

    std::cout << "  step: File Read plus Depth\n" << std::flush;
    RunBenchmarkAndPrintNoWarmup(
        "File Read plus Depth",
        payloadBytes,
        spec.iterations,
        [&spec]() {
            ConsumeReaderDepthFromFile(spec.path);
        });

    std::cout << "  step: File Read plus Value\n" << std::flush;
    RunBenchmarkAndPrintNoWarmup(
        "File Read plus Value",
        payloadBytes,
        spec.iterations,
        [&spec]() {
            ConsumeReaderValueLengthFromFile(spec.path);
        });

    std::cout << "  step: File first subtree create\n" << std::flush;
    RunBenchmarkAndPrintNoWarmup(
        "File first subtree create",
        payloadBytes,
        spec.iterations,
        [&spec]() {
            ConsumeNamedSubtreeCreateFromFile(spec.path, spec.subtreeElementName);
        });

    std::cout << "  step: File first subtree read\n" << std::flush;
    RunBenchmarkAndPrintNoWarmup(
        "File first subtree read",
        payloadBytes,
        spec.iterations,
        [&spec]() {
            ConsumeNamedSubtreeOnlyFromFile(spec.path, spec.subtreeElementName);
        });

    if (exhaustiveMode) {
        std::cout << "  step: File scan with observation\n" << std::flush;
        RunBenchmarkAndPrintNoWarmup(
            "File scan with observation",
            payloadBytes,
            spec.iterations,
            [&spec, &capturedObservation]() {
                const auto observation = ObserveExternalFile(spec);
                if (!capturedObservation.has_value()) {
                    capturedObservation = observation;
                }
                volatile std::size_t sink = observation.nodeCount + observation.attributeCount + observation.maxValueBytes;
                (void)sink;
            });

        std::cout << "  step: File ifstream XmlReader::Create+Read\n" << std::flush;
        RunBenchmarkAndPrint(
            "File ifstream XmlReader::Create+Read",
            payloadBytes,
            spec.iterations,
            [&spec]() {
                ConsumeReaderFromIstreamFile(spec.path);
            });

        std::cout << "  step: File first subtree replay\n" << std::flush;
        RunBenchmarkAndPrint(
            "File first subtree replay",
            payloadBytes,
            spec.iterations,
            [&spec]() {
                ConsumeNamedSubtreeReplayFromFile(spec.path, spec.subtreeElementName);
            });
    }

    if (capturedObservation.has_value()) {
        PrintExternalFileObservation(spec, *capturedObservation);
    } else {
        try {
            PrintExternalFileObservation(spec, ObserveExternalFile(spec));
        } catch (const std::exception& exception) {
            std::cout << "  sample: observation_failed=" << exception.what() << '\n' << std::flush;
        }
    }
}

void RunSuite(const std::string& label, const std::string& xml, const int iterations) {
    std::cout << "\n[dataset] " << label << " bytes=" << xml.size() << " iterations=" << iterations << '\n';

    const auto parseResult = RunBenchmark(
        "XmlDocument::Parse",
        xml.size(),
        iterations,
        [&xml]() {
            const auto document = XmlDocument::Parse(xml);
            volatile bool hasRoot = document->DocumentElement() != nullptr;
            (void)hasRoot;
        });
    PrintResult(parseResult);

    const auto readerStringResult = RunBenchmark(
        "XmlReader::Create(string)+Read",
        xml.size(),
        iterations,
        [&xml]() {
            auto reader = XmlReader::Create(xml);
            ConsumeReader(reader);
        });
    PrintResult(readerStringResult);

    const auto readerStreamResult = RunBenchmark(
        "XmlReader::Create(stream)+Read",
        xml.size(),
        iterations,
        [&xml]() {
            std::istringstream stream(xml);
            auto reader = XmlReader::Create(stream);
            ConsumeReader(reader);
        });
    PrintResult(readerStreamResult);

    const auto document = XmlDocument::Parse(xml);
    const auto namespaces = BuildBenchmarkNamespaces();
    const std::string targetedXPath = "/bk:catalog/bk:book[@m:id='b42']/bk:title";
    const std::string broadXPath = "//bk:chapter[@m:index='3']";

    const auto targetedXPathResult = RunBenchmark(
        "XPath targeted SelectNodes",
        xml.size(),
        iterations,
        [&document, &namespaces, &targetedXPath]() {
            ConsumeNodeList(document->SelectNodes(targetedXPath, namespaces));
        });
    PrintResult(targetedXPathResult);

    const auto broadXPathResult = RunBenchmark(
        "XPath broad SelectNodes",
        xml.size(),
        iterations,
        [&document, &namespaces, &broadXPath]() {
            ConsumeNodeList(document->SelectNodes(broadXPath, namespaces));
        });
    PrintResult(broadXPathResult);
}

bool MatchesBenchmarkFilter(const std::string& filter, std::string_view group, const std::string& label) {
    if (filter.empty()) {
        return true;
    }

    return std::string(group).find(filter) != std::string::npos
        || label.find(filter) != std::string::npos;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string filter;
    for (int index = 1; index < argc; ++index) {
        if (!filter.empty()) {
            filter.push_back(' ');
        }
        filter += argv[index];
    }

    std::cout << "LibXML Release benchmark baseline\n";
    std::cout << "Scenarios: DOM parse, Reader sequential scan, XPath targeted/broad query, Schema validation, external real-file scan\n";
    if (!filter.empty()) {
        std::cout << "Filter: " << filter << '\n';
    }

    const std::vector<DataSetSpec> domSpecs{
        {"medium", 250, 8, 8, true},
        {"large", 2000, 8, 5, true},
        {"xlarge", 20000, 8, 2, true},
        {"xxlarge-100mb-plus", 110000, 8, 1, true},
    };

    const std::vector<SchemaDataSetSpec> schemaSpecs{
        {"nested-xsi-type baseline small", "urn:benchmark-schema-baseline", 120, 4, 5, true},
        {"direct-identity reader-safe small", "urn:benchmark-schema-direct-reader-safe", 120, 4, 5, true},
        {"nested-xsi-type reader-safe small", "urn:benchmark-schema-reader-safe", 120, 4, 5, true},
        {"direct-identity fallback small", "urn:benchmark-schema-direct-fallback", 120, 4, 5, true},
        {"nested-xsi-type fallback small", "urn:benchmark-schema-fallback", 120, 4, 5, true},
        {"nested-xsi-type baseline medium", "urn:benchmark-schema-baseline", 5000, 4, 3, true},
        {"direct-identity reader-safe medium", "urn:benchmark-schema-direct-reader-safe", 5000, 4, 3, true},
        {"nested-xsi-type reader-safe medium", "urn:benchmark-schema-reader-safe", 5000, 4, 3, true},
        {"direct-identity fallback medium", "urn:benchmark-schema-direct-fallback", 5000, 4, 3, true},
        {"nested-xsi-type fallback medium", "urn:benchmark-schema-fallback", 5000, 4, 3, true},
        {"nested-xsi-type baseline large", "urn:benchmark-schema-baseline", 50000, 4, 1, true},
        {"direct-identity reader-safe large", "urn:benchmark-schema-direct-reader-safe", 50000, 4, 1, true},
        {"nested-xsi-type reader-safe large", "urn:benchmark-schema-reader-safe", 50000, 4, 1, true},
        {"direct-identity fallback large", "urn:benchmark-schema-direct-fallback", 50000, 4, 1, true},
        {"nested-xsi-type fallback large", "urn:benchmark-schema-fallback", 50000, 4, 1, true},
        {"nested-xsi-type baseline xxlarge-100mb-plus", "urn:benchmark-schema-baseline", 720000, 4, 1, false},
        {"direct-identity reader-safe xxlarge-100mb-plus", "urn:benchmark-schema-direct-reader-safe", 720000, 4, 1, false},
        {"nested-xsi-type reader-safe xxlarge-100mb-plus", "urn:benchmark-schema-reader-safe", 720000, 4, 1, false},
        {"direct-identity fallback xxlarge-100mb-plus", "urn:benchmark-schema-direct-fallback", 720000, 4, 1, false},
        {"nested-xsi-type fallback xxlarge-100mb-plus", "urn:benchmark-schema-fallback", 720000, 4, 1, false},
    };

    const std::vector<ReaderStreamingDataSetSpec> readerStreamingSpecs{
        {"single-doc large-4mb", 4 * 1024 * 1024, 32, 5, 1},
        {"single-doc xlarge-16mb", 16 * 1024 * 1024, 32, 3, 1},
        {"single-doc xxlarge-64mb", 64 * 1024 * 1024, 32, 1, 1},
    };

    const std::vector<ReaderStreamingDataSetSpec> readerStreamingSegmentedSpecs{
        {"segmented-doc xlarge-16mb", 16 * 1024 * 1024, 32, 3, 64},
        {"segmented-doc xxlarge-64mb", 64 * 1024 * 1024, 32, 1, 64},
    };

    const std::vector<ExternalFileSpec> externalFileSpecs = GetExternalFileSpecs();

    XmlReaderSettings schemaBaselineSettings;
    schemaBaselineSettings.Validation = ValidationType::Schema;
    schemaBaselineSettings.Schemas = BuildBaselineNestedXsiTypeSchemas();

    XmlReaderSettings schemaReaderSafeSettings;
    schemaReaderSafeSettings.Validation = ValidationType::Schema;
    schemaReaderSafeSettings.Schemas = BuildReaderSafeNestedXsiTypeSchemas();

    XmlReaderSettings schemaDirectReaderSafeSettings;
    schemaDirectReaderSafeSettings.Validation = ValidationType::Schema;
    schemaDirectReaderSafeSettings.Schemas = BuildDirectReaderSafeIdentitySchemas();

    XmlReaderSettings schemaFallbackSettings;
    schemaFallbackSettings.Validation = ValidationType::Schema;
    schemaFallbackSettings.Schemas = BuildNestedXsiTypeFallbackSchemas();

    XmlReaderSettings schemaDirectFallbackSettings;
    schemaDirectFallbackSettings.Validation = ValidationType::Schema;
    schemaDirectFallbackSettings.Schemas = BuildDirectFallbackIdentitySchemas();

    for (const auto& spec : domSpecs) {
        if (!MatchesBenchmarkFilter(filter, "dataset", spec.label)) {
            continue;
        }
        const std::string xml = BuildCatalogXml(spec.primaryCount, spec.secondaryCount);
        RunSuite(spec.label, xml, spec.iterations);
    }

    for (const auto& spec : schemaSpecs) {
        if (!MatchesBenchmarkFilter(filter, "schema", spec.label)
            && !MatchesBenchmarkFilter(filter, "fallback-dom", spec.label)) {
            continue;
        }

        const XmlReaderSettings* settings = &schemaFallbackSettings;
        if (spec.label.find("baseline") != std::string::npos) {
            settings = &schemaBaselineSettings;
        } else if (spec.label.find("direct-identity reader-safe") != std::string::npos) {
            settings = &schemaDirectReaderSafeSettings;
        } else if (spec.label.find("reader-safe") != std::string::npos) {
            settings = &schemaReaderSafeSettings;
        } else if (spec.label.find("direct-identity fallback") != std::string::npos) {
            settings = &schemaDirectFallbackSettings;
        }

        const bool usesNestedXsiType = spec.label.find("nested-xsi-type") != std::string::npos;
        const std::string xml = usesNestedXsiType
            ? BuildNestedXsiTypeValidationXml(spec.namespaceUri, spec.sectionCount, spec.booksPerSection)
            : BuildDirectIdentityValidationXml(spec.namespaceUri, spec.sectionCount, spec.booksPerSection);
        RunSchemaValidationSuite(
            spec.label,
            xml,
            *settings,
            spec.iterations,
            spec.runStreamVariant);

        if (spec.label.find("nested-xsi-type fallback large") != std::string::npos
            || spec.label.find("nested-xsi-type fallback xxlarge-100mb-plus") != std::string::npos
            || spec.label.find("direct-identity fallback xxlarge-100mb-plus") != std::string::npos) {
            RunDomFallbackBreakdownSuite(spec.label, xml, *settings->Schemas, spec.iterations);
        }
    }

    for (const auto& spec : readerStreamingSpecs) {
        if (!MatchesBenchmarkFilter(filter, "reader-streaming", spec.label)) {
            continue;
        }
        const std::string xml = spec.segmentCount > 1
            ? BuildSegmentedReaderStreamingXml(spec.childTextBytes, spec.segmentCount, spec.trailingSiblingCount)
            : BuildReaderStreamingXml(spec.childTextBytes, spec.trailingSiblingCount);
        RunReaderStreamingSuite(spec.label, xml, spec.iterations);
    }

    for (const auto& spec : readerStreamingSegmentedSpecs) {
        if (!MatchesBenchmarkFilter(filter, "reader-streaming-segmented", spec.label)) {
            continue;
        }
        const std::string xml = BuildSegmentedReaderStreamingXml(spec.childTextBytes, spec.segmentCount, spec.trailingSiblingCount);
        RunSegmentedReaderStreamingSuite(spec.label, xml, spec.iterations);
    }

    for (const auto& spec : externalFileSpecs) {
        if (!MatchesBenchmarkFilter(filter, "real-file", spec.label)) {
            continue;
        }
        RunExternalFileSuite(spec);
    }

    return 0;
}