#pragma once

#include "XmlTypes.h"
#include "XmlNameTable.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace System::Xml {

class XmlNode;
class XmlDocument;
class XmlElement;
class XmlReaderInputSource;

class XmlReader final {
public:
    static XmlReader Create(const std::string& xml, const XmlReaderSettings& settings = {});
    static XmlReader Create(std::istream& stream, const XmlReaderSettings& settings = {});
    static XmlReader CreateFromFile(const std::string& path, const XmlReaderSettings& settings = {});

    bool Read();
    bool IsEOF() const noexcept;
    ReadState GetReadState() const noexcept;

    XmlNodeType NodeType() const;
    const std::string& Name() const;
    std::string LocalName() const;
    std::string Prefix() const;
    const std::string& NamespaceURI() const;
    const std::string& Value() const;
    int Depth() const noexcept;
    bool IsEmptyElement() const noexcept;
    bool HasValue() const noexcept;
    int AttributeCount() const noexcept;
    bool HasAttributes() const noexcept;

    std::string GetAttribute(std::string_view name) const;
    std::string GetAttribute(int index) const;
    std::string GetAttribute(std::string_view localName, std::string_view namespaceUri) const;
    bool MoveToAttribute(std::string_view name);
    bool MoveToAttribute(int index);
    bool MoveToAttribute(std::string_view localName, std::string_view namespaceUri);
    bool MoveToFirstAttribute();
    bool MoveToNextAttribute();
    bool MoveToElement();
    std::string LookupNamespace(std::string_view prefix) const;

    std::string ReadInnerXml() const;
    std::string ReadOuterXml() const;
    std::string ReadContentAsString();
    int ReadContentAsInt();
    long long ReadContentAsLong();
    double ReadContentAsDouble();
    bool ReadContentAsBoolean();
    std::string ReadString();
    int ReadBase64(std::vector<unsigned char>& buffer);

    XmlNodeType MoveToContent();
    bool IsStartElement();
    bool IsStartElement(std::string_view name);
    void ReadStartElement();
    void ReadStartElement(std::string_view name);
    void ReadEndElement();
    std::string ReadElementContentAsString();
    std::string ReadElementString();
    std::string ReadElementString(std::string_view name);
    void Skip();
    bool ReadToFollowing(std::string_view name);
    bool ReadToDescendant(std::string_view name);
    bool ReadToNextSibling(std::string_view name);
    XmlReader ReadSubtree();
    void Close();
    const XmlNameTable& NameTable() const noexcept;

    bool HasLineInfo() const noexcept;
    std::size_t LineNumber() const noexcept;
    std::size_t LinePosition() const noexcept;

private:
    friend class XmlDocument;
    friend void ValidateXmlReaderInputAgainstSchemas(const std::shared_ptr<const std::string>& xml, const XmlReaderSettings& settings);

    struct BufferedNode {
        XmlNodeType nodeType = XmlNodeType::None;
        std::string name;
        std::string namespaceUri;
        std::string value;
        std::size_t valueStart = std::string::npos;
        std::size_t valueEnd = std::string::npos;
        int depth = 0;
        bool isEmptyElement = false;
        std::string innerXml;
        std::string outerXml;
        std::size_t nodeStart = std::string::npos;
        std::size_t nodeEnd = std::string::npos;
        std::vector<std::pair<std::string, std::string>> attributes;
        std::vector<std::string> attributeNamespaceUris;
        std::size_t elementStart = std::string::npos;
        std::size_t contentStart = std::string::npos;
        std::size_t closeStart = std::string::npos;
        std::size_t closeEnd = std::string::npos;
    };

    struct AttributeValueMetadata {
        std::size_t valueStart = std::string::npos;
        std::size_t valueEnd = std::string::npos;
        unsigned char flags = 0;
    };

    struct DtdState {
        std::unordered_map<std::string, std::string> entityDeclarations;
        std::unordered_set<std::string> declaredEntityNames;
        std::unordered_set<std::string> notationDeclarationNames;
        std::unordered_set<std::string> unparsedEntityDeclarationNames;
        std::unordered_map<std::string, std::string> externalEntitySystemIds;
        std::vector<std::shared_ptr<XmlNode>> parsedEntities;
        std::vector<std::shared_ptr<XmlNode>> parsedNotations;
        std::string externalSubsetSystemId;
        bool externalSubsetDeclarationsLoaded = false;
    };

    explicit XmlReader(XmlReaderSettings settings = {});

    char Peek() const noexcept;
    char ReadChar();
    bool StartsWith(std::string_view token) const noexcept;
    void SkipWhitespace();
    std::string ParseName();
    std::string ParseQuotedValue(bool decodeEntities = true);
    std::string DecodeEntities(std::string_view value) const;
    void QueueNode(
        XmlNodeType nodeType,
        std::string name,
        std::string namespaceUri,
        std::string value,
        int depth,
        bool isEmptyElement,
        std::string innerXml,
        std::string outerXml,
        std::size_t valueStart = std::string::npos,
        std::size_t valueEnd = std::string::npos,
        std::size_t nodeStart = std::string::npos,
        std::size_t nodeEnd = std::string::npos,
        std::vector<std::pair<std::string, std::string>> attributes = {},
        std::vector<std::string> attributeNamespaceUris = {},
        std::size_t elementStart = std::string::npos,
        std::size_t contentStart = std::string::npos,
        std::size_t closeStart = std::string::npos,
        std::size_t closeEnd = std::string::npos);
    bool TryConsumeBufferedNode();
    void ResetCurrentNode();
    char SourceCharAt(std::size_t position) const noexcept;
    const char* SourcePtrAt(std::size_t position, std::size_t& available) const noexcept;
    bool HasSourceChar(std::size_t position) const noexcept;
    std::size_t FindInSource(std::string_view token, std::size_t position) const noexcept;
    std::string SourceSubstr(std::size_t start, std::size_t count = std::string::npos) const;
    bool SourceRangeContains(std::size_t start, std::size_t end, char value) const noexcept;
    void AppendSourceSubstrTo(std::string& target, std::size_t start, std::size_t count) const;
    std::size_t AppendDecodedSourceRangeTo(std::string& target, std::size_t start, std::size_t end) const;
    void EnsureCurrentAttributeValueDecoded(std::size_t index) const;
    void AppendCurrentValueTo(std::string& target) const;
    void DecodeAndAppendCurrentBase64(std::vector<unsigned char>& buffer, unsigned int& accumulator, int& bits) const;
    std::size_t EarliestRetainedSourceOffset() const noexcept;
    void MaybeDiscardSourcePrefix() const;
    DtdState& EnsureDtdState();
    void RegisterDtdDeclarations(
        const std::vector<std::shared_ptr<XmlNode>>& entities,
        const std::vector<std::shared_ptr<XmlNode>>& notations);
    void LoadExternalSubsetDeclarations(std::string_view systemLiteral);
    void EnsureExternalSubsetDeclarationsLoaded();
    [[noreturn]] void ThrowUnknownEntityReference(std::string_view entity) const;
    void FinalizeSuccessfulRead();
    std::string CurrentLocalName() const;
    std::string CurrentPrefix() const;
    const std::string& CurrentAttributeNameRef(std::size_t index) const noexcept;
    std::string CurrentAttributeLocalName(std::size_t index) const;
    std::string CurrentAttributePrefix(std::size_t index) const;
    const std::string& CurrentNameRef() const noexcept;
    const std::string& CurrentAttributeNamespaceUri(std::size_t index) const;
    const std::string& CurrentAttributeValue(std::size_t index) const;
    void RefreshCurrentEarliestRetainedAttributeValueStart() const noexcept;
    void AppendCurrentAttributesForLoad(XmlElement& element);
    void MaterializeValue();
    void SetCurrentNode(
        XmlNodeType nodeType,
        std::string name,
        std::string namespaceUri,
        std::string value,
        int depth,
        bool isEmptyElement,
        std::string innerXml,
        std::string outerXml,
        std::size_t valueStart = std::string::npos,
        std::size_t valueEnd = std::string::npos,
        std::size_t nodeStart = std::string::npos,
        std::size_t nodeEnd = std::string::npos,
        std::vector<std::pair<std::string, std::string>> attributes = {},
        std::vector<std::string> attributeNamespaceUris = {},
        std::size_t elementStart = std::string::npos,
        std::size_t contentStart = std::string::npos,
        std::size_t closeStart = std::string::npos,
        std::size_t closeEnd = std::string::npos);
    void SetCurrentNameParts(std::string_view prefix, std::string_view localName);
    void SetCurrentAttributeNameParts(std::vector<std::string> prefixes, std::vector<std::string> localNames);
    std::pair<std::size_t, std::size_t> ComputeLineColumn(std::size_t position) const noexcept;
    [[noreturn]] void Throw(std::string_view message) const;
    void ParseDeclaration();
    void ParseDocumentType();
    void ParseProcessingInstruction();
    void ParseComment();
    void ParseCData();
    void ParseText();
    void ParseElement();
    void ParseEndElement();
    bool TryReadSimpleElementContentAsString(std::string& result, std::size_t& closeStart, std::size_t& closeEnd);
    std::pair<std::size_t, std::size_t> EnsureCurrentElementXmlBounds() const;
    std::pair<std::size_t, std::size_t> FindElementXmlBounds(
        std::size_t,
        std::size_t contentStart,
        std::string_view elementName) const;
    std::pair<std::string, std::string> CaptureElementXml(
        std::size_t elementStart,
        std::size_t contentStart) const;
    std::string LookupNamespaceUri(std::string_view prefix) const;
    const std::vector<std::pair<std::string, std::string>>& CurrentAttributes() const;
    void InitializeInputState();
    const std::string& EnsureResolvedBaseUri();
    static XmlReader CreateFromValidatedString(
        std::shared_ptr<const std::string> xml,
        const XmlReaderSettings& settings);

    XmlReaderSettings settings_;
    std::shared_ptr<const XmlReaderInputSource> inputSource_;
    std::size_t position_ = 0;
    XmlNodeType currentNodeType_ = XmlNodeType::None;
    std::string currentName_;
    const std::string* currentNameInterned_ = nullptr;
    std::string currentNamespaceUri_;
    mutable std::string currentValue_;
    mutable std::string currentInnerXml_;
    mutable std::string currentOuterXml_;
    mutable std::vector<std::pair<std::string, std::string>> currentAttributes_;
    mutable std::vector<const std::string*> currentAttributeNamesInterned_;
    mutable std::string currentLocalName_;
    mutable std::string currentPrefix_;
    mutable bool currentNamePartsResolved_ = false;
    mutable std::vector<std::string> currentAttributeLocalNames_;
    mutable std::vector<std::string> currentAttributePrefixes_;
    mutable std::vector<unsigned char> currentAttributeNamePartsResolved_;
    mutable std::vector<unsigned char> currentAttributeNamespaceUrisResolved_;
    mutable std::vector<AttributeValueMetadata> currentAttributeValueMetadata_;
    mutable std::vector<std::string> currentAttributeNamespaceUris_;
    std::vector<std::pair<std::string, std::string>> currentLocalNamespaceDeclarations_;
    mutable std::size_t currentEarliestRetainedAttributeValueStart_ = std::string::npos;
    std::string currentDeclarationVersion_;
    std::string currentDeclarationEncoding_;
    std::string currentDeclarationStandalone_;
    std::size_t currentValueStart_ = std::string::npos;
    std::size_t currentValueEnd_ = std::string::npos;
    std::size_t currentNodeStart_ = std::string::npos;
    std::size_t currentNodeEnd_ = std::string::npos;
    std::size_t currentElementStart_ = std::string::npos;
    std::size_t currentContentStart_ = std::string::npos;
    mutable std::size_t currentCloseStart_ = std::string::npos;
    mutable std::size_t currentCloseEnd_ = std::string::npos;
    std::deque<BufferedNode> bufferedNodes_;
    std::vector<std::string> elementStack_;
    std::vector<std::vector<std::pair<std::string, std::string>>> namespaceScopes_;
    std::vector<bool> namespaceScopeFramePushedStack_;
    std::vector<bool> xmlSpacePreserveStack_;
    std::vector<bool> xmlSpacePreserveFramePushedStack_;

    std::unique_ptr<DtdState> dtdState_;
    std::string pendingEndElementName_;
    int pendingEndElementDepth_ = 0;
    bool pendingEndElement_ = false;
    int currentDepth_ = 0;
    bool currentIsEmptyElement_ = false;
    bool sawDocumentType_ = false;
    bool sawRootElement_ = false;
    bool completedRootElement_ = false;
    bool xmlDeclarationAllowed_ = true;
    bool started_ = false;
    bool eof_ = false;
    bool closed_ = false;
    int attributeIndex_ = -1;
    std::size_t totalCharactersRead_ = 0;
    std::size_t entityCharactersRead_ = 0;
    std::size_t lineNumber_ = 1;
    std::size_t linePosition_ = 1;
    mutable std::size_t discardedSourceOffset_ = 0;
    mutable std::size_t discardedLineNumber_ = 1;
    mutable std::size_t discardedLinePosition_ = 1;
    std::string baseUri_;
    bool baseUriNeedsResolution_ = false;
    XmlNameTable nameTable_;

    friend void LoadXmlDocumentFromReader(XmlReader& reader, XmlDocument& document);
};

}  // namespace System::Xml
