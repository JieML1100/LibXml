#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace System::Xml {

class XmlNode;
class XmlResolver;
class XmlSchemaSet;
class XmlReaderInputSource;
class XmlReader;
class XmlDocument;
class XmlElement;

void LoadXmlDocumentFromReader(XmlReader& reader, XmlDocument& document);
std::string LookupNamespaceUriOnElement(const XmlElement* element, std::string_view prefix);

enum class XmlNodeType {
    None,
    Element,
    Attribute,
    Text,
    CDATA,
    EntityReference,
    Entity,
    ProcessingInstruction,
    Comment,
    Document,
    DocumentType,
    DocumentFragment,
    Notation,
    Whitespace,
    SignificantWhitespace,
    EndElement,
    EndEntity,
    XmlDeclaration,
};

enum class XmlNewLineHandling {
    None,
    Replace,
    Entitize,
};

enum class DtdProcessing {
    Prohibit,
    Ignore,
    Parse,
};

enum class ConformanceLevel {
    Auto,
    Fragment,
    Document,
};

enum class ValidationType {
    None,
    Schema,
};

enum class ReadState {
    Initial,
    Interactive,
    EndOfFile,
    Closed,
    Error,
};

enum class WriteState {
    Start,
    Prolog,
    Element,
    Attribute,
    Content,
    Closed,
    Error,
};

struct XmlWriterSettings {
    bool Indent = false;
    bool OmitXmlDeclaration = false;
    std::string IndentChars = "  ";
    std::string NewLineChars = "\r\n";
    XmlNewLineHandling NewLineHandling = XmlNewLineHandling::None;
    std::string Encoding = "utf-8";
    ConformanceLevel Conformance = ConformanceLevel::Document;
};

struct XmlReaderSettings {
    bool IgnoreComments = false;
    bool IgnoreWhitespace = false;
    bool IgnoreProcessingInstructions = false;
    DtdProcessing DtdProcessing = DtdProcessing::Parse;
    ConformanceLevel Conformance = ConformanceLevel::Document;
    ValidationType Validation = ValidationType::None;
    std::size_t MaxCharactersInDocument = 0;
    std::size_t MaxCharactersFromEntities = 0;
    std::shared_ptr<XmlSchemaSet> Schemas;
    std::shared_ptr<XmlResolver> Resolver;
};

class XmlException : public std::runtime_error {
public:
    XmlException(const std::string& message, std::size_t line = 0, std::size_t column = 0);

    std::size_t Line() const noexcept;
    std::size_t Column() const noexcept;

private:
    std::size_t line_;
    std::size_t column_;
};

class XmlResolver {
public:
    virtual ~XmlResolver() = default;
    virtual std::string ResolveUri(const std::string& baseUri, const std::string& relativeUri) const;
    virtual std::string GetEntity(const std::string& absoluteUri) const;
};

class XmlUrlResolver final : public XmlResolver {
public:
    std::string ResolveUri(const std::string& baseUri, const std::string& relativeUri) const override;
    std::string GetEntity(const std::string& absoluteUri) const override;
};

class XmlNameTable final {
public:
    XmlNameTable();
    ~XmlNameTable();

    XmlNameTable(const XmlNameTable&) = delete;
    XmlNameTable& operator=(const XmlNameTable&) = delete;

    XmlNameTable(XmlNameTable&& other) noexcept;
    XmlNameTable& operator=(XmlNameTable&& other) noexcept;

    const std::string& Add(std::string_view value);
    const std::string* Get(std::string_view value) const;
    std::size_t Count() const noexcept;

private:
    struct StringPtrHash {
        using is_transparent = void;
        std::size_t operator()(const std::string* s) const noexcept {
            return std::hash<std::string_view>{}(*s);
        }
        std::size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
    };

    struct StringPtrEqual {
        using is_transparent = void;
        bool operator()(const std::string* lhs, const std::string* rhs) const noexcept {
            return *lhs == *rhs;
        }
        bool operator()(const std::string* lhs, std::string_view rhs) const noexcept {
            return *lhs == rhs;
        }
        bool operator()(std::string_view lhs, const std::string* rhs) const noexcept {
            return lhs == *rhs;
        }
    };

    std::unordered_set<const std::string*, StringPtrHash, StringPtrEqual> values_;

    struct ArenaBlock {
        static constexpr std::size_t Capacity = 1024;
        alignas(std::string) unsigned char storage[Capacity * sizeof(std::string)];
        std::size_t used = 0;
        ArenaBlock* next = nullptr;
    };
    ArenaBlock* firstBlock_ = nullptr;
    ArenaBlock* currentBlock_ = nullptr;

    const std::string& AllocateString(std::string_view value);
};

enum class XmlNodeChangedAction {
    Insert,
    Remove,
    Change,
};

struct XmlNodeChangedEventArgs {
    XmlNodeChangedAction Action;
    XmlNode* Node;
    XmlNode* OldParent;
    XmlNode* NewParent;
    std::string OldValue;
    std::string NewValue;
};

using XmlNodeChangedEventHandler = std::function<void(const XmlNodeChangedEventArgs&)>;

class XmlDocument;
class XmlAttribute;
class XmlDeclaration;
class XmlDocumentType;
class XmlDocumentFragment;
class XmlEntity;
class XmlElement;
class XmlNamespaceManager;
class XmlNameTable;
class XmlNodeReader;
class XmlNotation;
class XPathNavigator;
class XPathDocument;

class XmlNodeList final {
public:
    XmlNodeList() = default;
    explicit XmlNodeList(std::vector<std::shared_ptr<XmlNode>> nodes);

    std::size_t Count() const noexcept;
    std::shared_ptr<XmlNode> Item(std::size_t index) const;
    bool Empty() const noexcept;

    std::vector<std::shared_ptr<XmlNode>>::const_iterator begin() const noexcept;
    std::vector<std::shared_ptr<XmlNode>>::const_iterator end() const noexcept;

private:
    std::vector<std::shared_ptr<XmlNode>> nodes_;
};

class XmlAttributeCollection final {
public:
    XmlAttributeCollection() = default;
    explicit XmlAttributeCollection(std::vector<std::shared_ptr<XmlAttribute>> attributes);

    std::size_t Count() const noexcept;
    std::shared_ptr<XmlAttribute> Item(std::size_t index) const;
    std::shared_ptr<XmlAttribute> Item(const std::string& name) const;
    bool Empty() const noexcept;

    std::vector<std::shared_ptr<XmlAttribute>>::const_iterator begin() const noexcept;
    std::vector<std::shared_ptr<XmlAttribute>>::const_iterator end() const noexcept;

private:
    std::vector<std::shared_ptr<XmlAttribute>> attributes_;
};

class XmlNamedNodeMap final {
public:
    XmlNamedNodeMap() = default;
    explicit XmlNamedNodeMap(std::vector<std::shared_ptr<XmlNode>> nodes);

    std::size_t Count() const noexcept;
    std::shared_ptr<XmlNode> Item(std::size_t index) const;
    std::shared_ptr<XmlNode> GetNamedItem(const std::string& name) const;
    bool Empty() const noexcept;

    std::vector<std::shared_ptr<XmlNode>>::const_iterator begin() const noexcept;
    std::vector<std::shared_ptr<XmlNode>>::const_iterator end() const noexcept;

private:
    std::vector<std::shared_ptr<XmlNode>> nodes_;
};

class XmlNode {
public:
    virtual ~XmlNode() = default;

    XmlNodeType NodeType() const noexcept;
    const std::string& Name() const noexcept;
    std::string LocalName() const;
    std::string Prefix() const;
    std::string NamespaceURI() const;
    std::string GetNamespaceOfPrefix(const std::string& prefix) const;
    std::string GetPrefixOfNamespace(const std::string& namespaceUri) const;
    virtual const std::string& Value() const noexcept;
    virtual void SetValue(const std::string& value);

    virtual void Normalize();

    XmlNode* ParentNode() noexcept;
    const XmlNode* ParentNode() const noexcept;
    XmlDocument* OwnerDocument() noexcept;
    const XmlDocument* OwnerDocument() const noexcept;

    const std::vector<std::shared_ptr<XmlNode>>& ChildNodes() const noexcept;
    XmlNodeList ChildNodeList() const;
    std::shared_ptr<XmlNode> FirstChild() const;
    std::shared_ptr<XmlNode> LastChild() const;
    std::shared_ptr<XmlNode> PreviousSibling() const;
    std::shared_ptr<XmlNode> NextSibling() const;
    bool HasChildNodes() const noexcept;

    virtual std::shared_ptr<XmlNode> AppendChild(const std::shared_ptr<XmlNode>& child);
    virtual std::shared_ptr<XmlNode> InsertBefore(
        const std::shared_ptr<XmlNode>& newChild,
        const std::shared_ptr<XmlNode>& referenceChild);
    virtual std::shared_ptr<XmlNode> InsertAfter(
        const std::shared_ptr<XmlNode>& newChild,
        const std::shared_ptr<XmlNode>& referenceChild);
    virtual std::shared_ptr<XmlNode> ReplaceChild(
        const std::shared_ptr<XmlNode>& newChild,
        const std::shared_ptr<XmlNode>& oldChild);
    virtual std::shared_ptr<XmlNode> RemoveChild(const std::shared_ptr<XmlNode>& child);
    void RemoveAllChildren();
    virtual void RemoveAll();

    std::string InnerText() const;
    virtual void SetInnerText(const std::string& text);
    std::string InnerXml(const XmlWriterSettings& settings = {}) const;
    virtual void SetInnerXml(const std::string& xml);
    std::string OuterXml(const XmlWriterSettings& settings = {}) const;
    std::shared_ptr<XmlNode> CloneNode(bool deep) const;
    XPathNavigator CreateNavigator() const;

    std::shared_ptr<XmlNode> SelectSingleNode(const std::string& xpath) const;
    std::shared_ptr<XmlNode> SelectSingleNode(const std::string& xpath, const XmlNamespaceManager& namespaces) const;
    XmlNodeList SelectNodes(const std::string& xpath) const;
    XmlNodeList SelectNodes(const std::string& xpath, const XmlNamespaceManager& namespaces) const;

protected:
    XmlNode(XmlNodeType nodeType, std::string name = {}, std::string value = {});

    virtual void ValidateChildInsertion(const std::shared_ptr<XmlNode>& child, const XmlNode* replacingChild = nullptr) const;
    void SetOwnerDocument(XmlDocument* ownerDocument) noexcept;
    void SetOwnerDocumentRecursive(XmlDocument* ownerDocument);
    void SetParent(XmlNode* parent) noexcept;
    std::vector<std::shared_ptr<XmlNode>>& MutableChildNodes() noexcept;
    std::shared_ptr<XmlNode> AppendChildForLoad(const std::shared_ptr<XmlNode>& child);
    std::shared_ptr<XmlNode> AppendChildForOwnedLoad(const std::shared_ptr<XmlNode>& child);

private:
    XmlNodeType nodeType_;
    std::string name_;
    std::string value_;
    XmlNode* parent_;
    XmlDocument* ownerDocument_;
    std::vector<std::shared_ptr<XmlNode>> childNodes_;

    friend class XmlDocument;
    friend class XmlElement;
    friend class XmlWriter;
    friend void LoadXmlDocumentFromReader(XmlReader& reader, XmlDocument& document);
};

class XmlAttribute final : public XmlNode {
public:
    XmlAttribute(std::string name, std::string value = {});

    XmlElement* OwnerElement() noexcept;
    const XmlElement* OwnerElement() const noexcept;
};

class XmlCharacterData : public XmlNode {
public:
    const std::string& Data() const noexcept;
    void SetData(const std::string& data);
    std::size_t Length() const noexcept;
    void AppendData(const std::string& strData);
    void DeleteData(std::size_t offset, std::size_t count);
    void InsertData(std::size_t offset, const std::string& strData);
    void ReplaceData(std::size_t offset, std::size_t count, const std::string& strData);
    std::string Substring(std::size_t offset, std::size_t count) const;

protected:
    XmlCharacterData(XmlNodeType nodeType, std::string name, std::string value = {});
};

class XmlText final : public XmlCharacterData {
public:
    explicit XmlText(std::string text);

    std::shared_ptr<XmlText> SplitText(std::size_t offset);
};

class XmlEntityReference final : public XmlNode {
public:
    explicit XmlEntityReference(std::string name, std::string resolvedValue = {});
};

class XmlEntity final : public XmlNode {
public:
    XmlEntity(
        std::string name,
        std::string replacementText = {},
        std::string publicId = {},
        std::string systemId = {},
        std::string notationName = {});

    const std::string& PublicId() const noexcept;
    const std::string& SystemId() const noexcept;
    const std::string& NotationName() const noexcept;

private:
    std::string publicId_;
    std::string systemId_;
    std::string notationName_;
};

class XmlNotation final : public XmlNode {
public:
    XmlNotation(std::string name, std::string publicId = {}, std::string systemId = {});

    const std::string& PublicId() const noexcept;
    const std::string& SystemId() const noexcept;

private:
    std::string publicId_;
    std::string systemId_;
};

class XmlWhitespace final : public XmlCharacterData {
public:
    explicit XmlWhitespace(std::string whitespace);

    void SetValue(const std::string& value) override;
};

class XmlSignificantWhitespace final : public XmlCharacterData {
public:
    explicit XmlSignificantWhitespace(std::string whitespace);

    void SetValue(const std::string& value) override;
};

class XmlCDataSection final : public XmlCharacterData {
public:
    explicit XmlCDataSection(std::string data);
};

class XmlComment final : public XmlCharacterData {
public:
    explicit XmlComment(std::string comment);
};

class XmlProcessingInstruction final : public XmlNode {
public:
    XmlProcessingInstruction(std::string target, std::string data = {});

    const std::string& Target() const noexcept;
    const std::string& Data() const noexcept;
    void SetData(const std::string& data);
};

class XmlDeclaration final : public XmlNode {
public:
    XmlDeclaration(std::string version = "1.0", std::string encoding = {}, std::string standalone = {});

    const std::string& Version() const noexcept;
    const std::string& Encoding() const noexcept;
    const std::string& Standalone() const noexcept;

private:
    std::string version_;
    std::string encoding_;
    std::string standalone_;
};

class XmlDocumentType final : public XmlNode {
public:
    XmlDocumentType(
        std::string name,
        std::string publicId = {},
        std::string systemId = {},
        std::string internalSubset = {});

    const std::string& PublicId() const noexcept;
    const std::string& SystemId() const noexcept;
    const std::string& InternalSubset() const noexcept;
    XmlNamedNodeMap Entities() const;
    XmlNamedNodeMap Notations() const;

private:
    std::string publicId_;
    std::string systemId_;
    std::string internalSubset_;
    std::vector<std::shared_ptr<XmlNode>> entities_;
    std::vector<std::shared_ptr<XmlNode>> notations_;

    friend class XmlNode;
};

class XmlDocumentFragment final : public XmlNode {
public:
    XmlDocumentFragment();

    void SetInnerXml(const std::string& xml);
};

class XmlElement final : public XmlNode {
public:
    explicit XmlElement(std::string name);

    const std::vector<std::shared_ptr<XmlAttribute>>& Attributes() const;
    XmlAttributeCollection AttributeNodes() const;
    bool HasAttributes() const noexcept;
    bool HasAttribute(const std::string& name) const;
    bool HasAttribute(const std::string& localName, const std::string& namespaceUri) const;
    std::string GetAttribute(const std::string& name) const;
    std::string GetAttribute(const std::string& localName, const std::string& namespaceUri) const;
    std::shared_ptr<XmlAttribute> GetAttributeNode(const std::string& name) const;
    std::shared_ptr<XmlAttribute> GetAttributeNode(const std::string& localName, const std::string& namespaceUri) const;
    std::shared_ptr<XmlAttribute> SetAttribute(const std::string& name, const std::string& value);
    void SetAttribute(const std::string& localName, const std::string& namespaceUri, const std::string& value);
    std::shared_ptr<XmlAttribute> SetAttributeNode(const std::shared_ptr<XmlAttribute>& attribute);
    bool RemoveAttribute(const std::string& name);
    bool RemoveAttribute(const std::string& localName, const std::string& namespaceUri);
    std::shared_ptr<XmlAttribute> RemoveAttributeNode(const std::shared_ptr<XmlAttribute>& attribute);
    void RemoveAllAttributes();
    void SetInnerXml(const std::string& xml);
    bool IsEmpty() const noexcept;
    std::vector<std::shared_ptr<XmlElement>> GetElementsByTagName(const std::string& name) const;
    XmlNodeList GetElementsByTagNameList(const std::string& name) const;
    std::vector<std::shared_ptr<XmlElement>> GetElementsByTagName(const std::string& localName, const std::string& namespaceUri) const;
    XmlNodeList GetElementsByTagNameList(const std::string& localName, const std::string& namespaceUri) const;
    bool WritesFullEndElement() const noexcept;
    XmlNodeReader CreateReader() const;
    std::string FindNamespaceDeclarationValue(const std::string& prefix) const;
    bool HasNamespaceDeclaration(const std::string& prefix) const;
    std::string FindNamespaceDeclarationPrefix(const std::string& namespaceUri) const;

private:
    struct PendingLoadAttribute {
        std::size_t nameOffset = 0;
        std::size_t nameLength = 0;
        std::size_t valueOffset = 0;
        std::size_t valueLength = 0;
    };

    bool TryFindNamespaceDeclarationValueView(std::string_view prefix, std::string_view& value) const noexcept;
    std::string_view PendingLoadAttributeNameView(const PendingLoadAttribute& attribute) const noexcept;
    std::string_view PendingLoadAttributeValueView(const PendingLoadAttribute& attribute) const noexcept;
    std::shared_ptr<XmlAttribute> MaterializePendingLoadAttribute(std::size_t index) const;
    std::size_t FindPendingLoadAttributeIndex(const std::string& name) const noexcept;
    std::size_t FindPendingLoadAttributeIndex(const std::string& localName, const std::string& namespaceUri) const;
    void EnsureAttributesMaterialized() const;
    void ReserveAttributesForLoad(std::size_t count, std::size_t totalStorageBytes = 0);
    void AppendAttributeForLoad(std::string name, std::string value);
    mutable std::vector<std::shared_ptr<XmlAttribute>> attributes_;
    mutable std::vector<PendingLoadAttribute> pendingLoadAttributes_;
    std::string pendingLoadAttributeStorage_;
    bool writeFullEndElement_ = false;

    friend void LoadXmlDocumentFromReader(XmlReader& reader, XmlDocument& document);
    friend std::string LookupNamespaceUriOnElement(const XmlElement* element, std::string_view prefix);
    friend bool TryGetAttributeValueViewInternal(const XmlElement& element, std::string_view name, std::string_view& value) noexcept;
    friend bool AttributeValueEqualsInternal(const XmlElement& element, std::string_view name, std::string_view expectedValue) noexcept;
    friend class XmlReader;
    friend class XmlWriter;
    friend class XmlNode;
};

class XmlDocument final : public XmlNode {
public:
    XmlDocument();

    static std::shared_ptr<XmlDocument> Parse(const std::string& xml);

    void LoadXml(const std::string& xml);
    // One-shot document load: eagerly reads the file and builds the DOM from in-memory text.
    void Load(const std::string& path);
    void Load(std::istream& stream);
    void Save(const std::string& path, const XmlWriterSettings& settings = {}) const;
    void Save(std::ostream& stream, const XmlWriterSettings& settings = {}) const;
    void Validate(const XmlSchemaSet& schemas) const;

    bool PreserveWhitespace() const noexcept;
    void SetPreserveWhitespace(bool value) noexcept;

    std::string ToString(const XmlWriterSettings& settings = {}) const;
    void RemoveAll() override;

    std::shared_ptr<XmlDeclaration> Declaration() const;
    std::shared_ptr<XmlDocumentType> DocumentType() const;
    std::shared_ptr<XmlElement> DocumentElement() const;
    std::vector<std::shared_ptr<XmlElement>> GetElementsByTagName(const std::string& name) const;
    XmlNodeList GetElementsByTagNameList(const std::string& name) const;
    std::vector<std::shared_ptr<XmlElement>> GetElementsByTagName(const std::string& localName, const std::string& namespaceUri) const;
    XmlNodeList GetElementsByTagNameList(const std::string& localName, const std::string& namespaceUri) const;

    std::shared_ptr<XmlDocumentFragment> CreateDocumentFragment() const;
    std::shared_ptr<XmlElement> CreateElement(const std::string& name) const;
    std::shared_ptr<XmlElement> CreateElement(const std::string& prefix, const std::string& localName, const std::string& namespaceUri = {}) const;
    std::shared_ptr<XmlAttribute> CreateAttribute(const std::string& name, const std::string& value = {}) const;
    std::shared_ptr<XmlAttribute> CreateAttribute(const std::string& prefix, const std::string& localName, const std::string& namespaceUri, const std::string& value = {}) const;
    std::shared_ptr<XmlText> CreateTextNode(const std::string& value) const;
    std::shared_ptr<XmlEntityReference> CreateEntityReference(const std::string& name) const;
    std::shared_ptr<XmlWhitespace> CreateWhitespace(const std::string& value) const;
    std::shared_ptr<XmlSignificantWhitespace> CreateSignificantWhitespace(const std::string& value) const;
    std::shared_ptr<XmlCDataSection> CreateCDataSection(const std::string& value) const;
    std::shared_ptr<XmlComment> CreateComment(const std::string& value) const;
    std::shared_ptr<XmlProcessingInstruction> CreateProcessingInstruction(const std::string& target, const std::string& data = {}) const;
    std::shared_ptr<XmlDeclaration> CreateXmlDeclaration(
        const std::string& version = "1.0",
        const std::string& encoding = {},
        const std::string& standalone = {}) const;
    std::shared_ptr<XmlDocumentType> CreateDocumentType(
        const std::string& name,
        const std::string& publicId = {},
        const std::string& systemId = {},
        const std::string& internalSubset = {}) const;
    std::shared_ptr<XmlNode> CreateNode(
        XmlNodeType nodeType,
        const std::string& name = {},
        const std::string& value = {}) const;
    std::shared_ptr<XmlNode> ImportNode(const XmlNode& node, bool deep) const;

    std::shared_ptr<XmlNode> AppendChild(const std::shared_ptr<XmlNode>& child) override;

    void SetNodeInserting(XmlNodeChangedEventHandler handler);
    void SetNodeInserted(XmlNodeChangedEventHandler handler);
    void SetNodeRemoving(XmlNodeChangedEventHandler handler);
    void SetNodeRemoved(XmlNodeChangedEventHandler handler);
    void SetNodeChanging(XmlNodeChangedEventHandler handler);
    void SetNodeChanged(XmlNodeChangedEventHandler handler);

protected:
    void ValidateChildInsertion(const std::shared_ptr<XmlNode>& child, const XmlNode* replacingChild = nullptr) const override;

private:
    bool preserveWhitespace_ = false;
    XmlNodeChangedEventHandler onNodeInserting_;
    XmlNodeChangedEventHandler onNodeInserted_;
    XmlNodeChangedEventHandler onNodeRemoving_;
    XmlNodeChangedEventHandler onNodeRemoved_;
    XmlNodeChangedEventHandler onNodeChanging_;
    XmlNodeChangedEventHandler onNodeChanged_;

    void FireNodeInserting(XmlNode* node, XmlNode* newParent) const;
    void FireNodeInserted(XmlNode* node, XmlNode* newParent) const;
    void FireNodeRemoving(XmlNode* node, XmlNode* oldParent) const;
    void FireNodeRemoved(XmlNode* node, XmlNode* oldParent) const;
    void FireNodeChanging(XmlNode* node, const std::string& oldValue, const std::string& newValue) const;
    void FireNodeChanged(XmlNode* node, const std::string& oldValue, const std::string& newValue) const;
    bool HasNodeChangeHandlers() const noexcept;
    void ValidateReaderAgainstSchemas(XmlReader& reader, const XmlSchemaSet& schemas) const;
    void ValidateIdentityConstraints(const XmlSchemaSet& schemas) const;

    friend void ValidateXmlReaderInputAgainstSchemas(const std::string& xml, const XmlReaderSettings& settings);
    friend void ValidateXmlReaderInputAgainstSchemas(XmlReader& reader, const XmlReaderSettings& settings);
    friend void LoadXmlDocumentFromReader(XmlReader& reader, XmlDocument& document);

    friend class XmlNode;
};

class XmlNamespaceManager final {
public:
    XmlNamespaceManager();

    void PushScope();
    bool PopScope();
    void AddNamespace(const std::string& prefix, const std::string& uri);
    std::string LookupNamespace(const std::string& prefix) const;
    std::string LookupPrefix(const std::string& uri) const;
    bool HasNamespace(const std::string& prefix) const;

private:
    std::vector<std::unordered_map<std::string, std::string>> scopes_;
};

class XmlReader final {
public:
    // Parses XML that is already materialized in memory.
    static XmlReader Create(const std::string& xml, const XmlReaderSettings& settings = {});
    // Wraps the supplied stream and preserves stream-backed reader semantics.
    static XmlReader Create(std::istream& stream, const XmlReaderSettings& settings = {});
    // Opens a file and parses it through the stream-backed reader path.
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

    std::string GetAttribute(const std::string& name) const;
    std::string GetAttribute(int index) const;
    std::string GetAttribute(const std::string& localName, const std::string& namespaceUri) const;
    bool MoveToAttribute(const std::string& name);
    bool MoveToAttribute(int index);
    bool MoveToAttribute(const std::string& localName, const std::string& namespaceUri);
    bool MoveToFirstAttribute();
    bool MoveToNextAttribute();
    bool MoveToElement();
    std::string LookupNamespace(const std::string& prefix) const;

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
    bool IsStartElement(const std::string& name);
    void ReadStartElement();
    void ReadStartElement(const std::string& name);
    void ReadEndElement();
    std::string ReadElementContentAsString();
    std::string ReadElementString();
    std::string ReadElementString(const std::string& name);
    void Skip();
    bool ReadToFollowing(const std::string& name);
    bool ReadToDescendant(const std::string& name);
    bool ReadToNextSibling(const std::string& name);
    XmlReader ReadSubtree();
    void Close();
    const XmlNameTable& NameTable() const noexcept;

    bool HasLineInfo() const noexcept;
    std::size_t LineNumber() const noexcept;
    std::size_t LinePosition() const noexcept;

private:
    friend class XmlDocument;

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

    explicit XmlReader(XmlReaderSettings settings = {});

    char Peek() const noexcept;
    char ReadChar();
    bool StartsWith(const std::string& token) const noexcept;
    void SkipWhitespace();
    std::string ParseName();
    std::string ParseQuotedValue(bool decodeEntities = true);
    std::string DecodeEntities(const std::string& value) const;
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
    std::size_t FindInSource(const std::string& token, std::size_t position) const noexcept;
    std::string SourceSubstr(std::size_t start, std::size_t count = std::string::npos) const;
    bool SourceRangeContains(std::size_t start, std::size_t end, char value) const noexcept;
    void AppendSourceSubstrTo(std::string& target, std::size_t start, std::size_t count) const;
    std::size_t AppendDecodedSourceRangeTo(std::string& target, std::size_t start, std::size_t end) const;
    void EnsureCurrentAttributeValueDecoded(std::size_t index) const;
    void AppendCurrentValueTo(std::string& target) const;
    void DecodeAndAppendCurrentBase64(std::vector<unsigned char>& buffer, unsigned int& accumulator, int& bits) const;
    std::size_t EarliestRetainedSourceOffset() const noexcept;
    void MaybeDiscardSourcePrefix() const;
    void FinalizeSuccessfulRead();
    std::string CurrentLocalName() const;
    std::string CurrentPrefix() const;
    std::string CurrentAttributeLocalName(std::size_t index) const;
    std::string CurrentAttributePrefix(std::size_t index) const;
    const std::string& CurrentAttributeNamespaceUri(std::size_t index) const;
    const std::string& CurrentAttributeValue(std::size_t index) const;
    void RefreshCurrentEarliestRetainedAttributeValueStart() const noexcept;
    void AppendCurrentAttributesForLoad(XmlElement& element);
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
    std::pair<std::size_t, std::size_t> ComputeLineColumn(std::size_t position) const noexcept;
    [[noreturn]] void Throw(const std::string& message) const;
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
        const std::string& elementName) const;
    std::pair<std::string, std::string> CaptureElementXml(
        std::size_t elementStart,
        std::size_t contentStart) const;
    std::string LookupNamespaceUri(const std::string& prefix) const;
    const std::vector<std::pair<std::string, std::string>>& CurrentAttributes() const;
    void InitializeInputState();
    static XmlReader CreateFromValidatedString(std::shared_ptr<const std::string> xml, const XmlReaderSettings& settings);

    XmlReaderSettings settings_;
    std::shared_ptr<const XmlReaderInputSource> inputSource_;
    std::size_t position_ = 0;
    XmlNodeType currentNodeType_ = XmlNodeType::None;
    std::string currentName_;
    std::string currentNamespaceUri_;
    mutable std::string currentValue_;
    mutable std::string currentInnerXml_;
    mutable std::string currentOuterXml_;
    mutable std::vector<std::pair<std::string, std::string>> currentAttributes_;
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
    std::vector<std::unordered_map<std::string, std::string>> namespaceScopes_;
    std::vector<bool> namespaceScopeFramePushedStack_;
    std::vector<bool> xmlSpacePreserveStack_;
    std::vector<bool> xmlSpacePreserveFramePushedStack_;
    std::unordered_map<std::string, std::string> entityDeclarations_;
    std::unordered_set<std::string> declaredEntityNames_;
    std::unordered_set<std::string> notationDeclarationNames_;
    std::unordered_set<std::string> unparsedEntityDeclarationNames_;
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
    std::unordered_map<std::string, std::string> externalEntitySystemIds_;
    XmlNameTable nameTable_;

    friend void LoadXmlDocumentFromReader(XmlReader& reader, XmlDocument& document);
};

class XmlNodeReader final {
public:
    explicit XmlNodeReader(const XmlNode& node, const XmlReaderSettings& settings = {});

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

    std::string GetAttribute(const std::string& name) const;
    std::string GetAttribute(int index) const;
    std::string GetAttribute(const std::string& localName, const std::string& namespaceUri) const;
    bool MoveToAttribute(const std::string& name);
    bool MoveToAttribute(int index);
    bool MoveToAttribute(const std::string& localName, const std::string& namespaceUri);
    bool MoveToFirstAttribute();
    bool MoveToNextAttribute();
    bool MoveToElement();
    std::string LookupNamespace(const std::string& prefix) const;

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
    bool IsStartElement(const std::string& name);
    void ReadStartElement();
    void ReadStartElement(const std::string& name);
    void ReadEndElement();
    std::string ReadElementContentAsString();
    std::string ReadElementString();
    std::string ReadElementString(const std::string& name);
    void Skip();
    bool ReadToFollowing(const std::string& name);
    bool ReadToDescendant(const std::string& name);
    bool ReadToNextSibling(const std::string& name);
    const XmlNameTable& NameTable() const noexcept;

private:
    struct NodeEvent {
        XmlNodeType nodeType = XmlNodeType::None;
        std::string name;
        std::string namespaceUri;
        std::string value;
        int depth = 0;
        bool isEmptyElement = false;
        std::string innerXml;
        std::string outerXml;
        std::vector<std::pair<std::string, std::string>> attributes;
        std::vector<std::string> attributeNamespaceUris;
    };

    void BuildEvents(const XmlNode& node, int depth, bool preserveSpace);
    void AppendEvent(
        XmlNodeType nodeType,
        std::string name,
        std::string namespaceUri,
        std::string value,
        int depth,
        bool isEmptyElement,
        std::string innerXml,
        std::string outerXml,
        std::vector<std::pair<std::string, std::string>> attributes = {},
        std::vector<std::string> attributeNamespaceUris = {});
    const NodeEvent* CurrentEvent() const noexcept;
    const std::vector<std::pair<std::string, std::string>>& CurrentAttributes() const noexcept;

    XmlReaderSettings settings_;
    std::vector<NodeEvent> events_;
    std::size_t currentIndex_ = 0;
    bool started_ = false;
    bool eof_ = false;
    int attributeIndex_ = -1;
    XmlNameTable nameTable_;
};

class XmlWriter final {
public:
    explicit XmlWriter(XmlWriterSettings settings = {});
    XmlWriter(std::ostream& stream, XmlWriterSettings settings = {});

    WriteState GetWriteState() const noexcept;
    std::string LookupPrefix(const std::string& namespaceUri) const;

    void WriteStartDocument(
        const std::string& version = "1.0",
        const std::string& encoding = {},
        const std::string& standalone = {});
    void WriteDocType(
        const std::string& name,
        const std::string& publicId = {},
        const std::string& systemId = {},
        const std::string& internalSubset = {});
    void WriteStartElement(const std::string& name);
    void WriteStartElement(const std::string& prefix, const std::string& localName, const std::string& namespaceUri = {});
    void WriteStartAttribute(const std::string& name);
    void WriteStartAttribute(const std::string& prefix, const std::string& localName, const std::string& namespaceUri = {});
    void WriteEndAttribute();
    void WriteAttributeString(const std::string& name, const std::string& value);
    void WriteAttributeString(
        const std::string& prefix,
        const std::string& localName,
        const std::string& namespaceUri,
        const std::string& value);
    void WriteAttributes(const XmlAttributeCollection& attributes);
    void WriteAttributes(const XmlElement& element);
    void WriteCharEntity(unsigned int codePoint);
    void WriteEntityRef(const std::string& name);
    void WriteString(const std::string& text);
    void WriteValue(const std::string& value);
    void WriteValue(bool value);
    void WriteValue(int value);
    void WriteValue(double value);
    void WriteName(const std::string& name);
    void WriteQualifiedName(const std::string& localName, const std::string& namespaceUri = {});
    void WriteWhitespace(const std::string& whitespace);
    void WriteSignificantWhitespace(const std::string& whitespace);
    void WriteCData(const std::string& text);
    void WriteComment(const std::string& text);
    void WriteProcessingInstruction(const std::string& name, const std::string& text);
    void WriteRaw(const std::string& xml);
    void WriteBase64(const unsigned char* data, std::size_t length);
    void WriteElementString(const std::string& name, const std::string& value);
    void WriteElementString(const std::string& prefix, const std::string& localName, const std::string& namespaceUri, const std::string& value);
    void WriteNode(const XmlNode& node);
    void WriteNode(XmlReader& reader, bool defattr = true);
    void WriteEndElement();
    void WriteFullEndElement();
    void WriteEndDocument();
    void Flush() const;
    void Close();
    std::string GetString() const;
    void Save(std::ostream& stream) const;
    void Save(const std::string& path) const;

    static std::string WriteToString(const XmlNode& node, const XmlWriterSettings& settings = {});
    static void WriteToStream(const XmlNode& node, std::ostream& stream, const XmlWriterSettings& settings = {});
    static void WriteToFile(const XmlNode& node, const std::string& path, const XmlWriterSettings& settings = {});

private:
    struct DirectElementState {
        std::string name;
        bool startTagOpen = true;
        bool hasAnyChild = false;
        bool hasTextLikeChild = false;
        bool hasNonTextChild = false;
        bool forceFullEnd = false;
        std::unordered_map<std::string, std::string> namespaceDeclarations;
    };

    void EnsureDocumentOpen(const std::string& operation) const;
    void EnsureOpenElement(const std::string& operation) const;
    void EnsureOpenStartElement(const std::string& operation) const;
    void EnsureNoOpenAttribute(const std::string& operation) const;
    void EnsureOutputReady() const;
    void MarkCurrentElementContentStarted();
    std::string LookupNamespacePrefix(const std::string& namespaceUri) const;
    void AppendDocumentLevelNode(const std::shared_ptr<XmlNode>& node);
    bool UsesDirectOutput() const noexcept;
    void WriteDirectTopLevelSeparatorIfNeeded();
    void EnsureDirectCurrentStartTagClosed();
    void PrepareDirectParentForChild(bool textLikeChild);
    void WriteDirectAttribute(const std::string& name, const std::string& value);
    bool HasDirectNamespaceBinding(const std::string& prefix, const std::string& namespaceUri) const;
    std::string LookupDirectNamespacePrefix(const std::string& namespaceUri) const;
    void DeclareDirectNamespaceIfNeeded(const std::string& prefix, const std::string& namespaceUri);
    void WriteDirectTopLevelNode(const XmlNode& node);

    XmlDocument document_;
    std::shared_ptr<XmlDocumentFragment> fragmentRoot_;
    XmlWriterSettings settings_;
    std::vector<std::shared_ptr<XmlElement>> elementStack_;
    std::vector<bool> startTagOpenStack_;
    std::ostream* directOutputStream_ = nullptr;
    std::vector<DirectElementState> directElementStack_;
    std::optional<std::string> currentAttributeName_;
    std::string currentAttributeValue_;
    bool startDocumentWritten_ = false;
    bool documentClosed_ = false;
    bool directHasRootElement_ = false;
    bool directHasDocumentType_ = false;
    bool directHasTopLevelContent_ = false;
};

class XmlConvert final {
public:
    XmlConvert() = delete;

    static std::string EncodeName(const std::string& name);
    static std::string DecodeName(const std::string& name);
    static std::string EncodeLocalName(const std::string& name);
    static std::string EncodeNmToken(const std::string& name);
    static bool IsXmlChar(char ch);
    static bool IsWhitespaceChar(char ch);
    static bool IsStartNameChar(char ch);
    static bool IsNameChar(char ch);
    static bool IsNCNameStartChar(char ch);
    static bool IsNCNameChar(char ch);
    static std::string VerifyName(const std::string& name);
    static std::string VerifyNCName(const std::string& name);
    static std::string VerifyNmToken(const std::string& name);
    static std::string VerifyXmlChars(const std::string& content);

    static std::string ToString(bool value);
    static std::string ToString(int value);
    static std::string ToString(long long value);
    static std::string ToString(double value);
    static std::string ToString(float value);

    static bool ToBoolean(const std::string& value);
    static int ToInt32(const std::string& value);
    static long long ToInt64(const std::string& value);
    static double ToDouble(const std::string& value);
    static float ToSingle(const std::string& value);
};

class XmlSchemaSet final {
public:
    struct AnnotationEntry {
        std::string Source;
        std::string Language;
        std::string Content;
    };

    struct Annotation {
        std::vector<AnnotationEntry> AppInfo;
        std::vector<AnnotationEntry> Documentation;

        bool Empty() const noexcept {
            return AppInfo.empty() && Documentation.empty();
        }
    };

    XmlSchemaSet() = default;

    void AddXml(const std::string& xml);
    void AddFile(const std::string& path);
    std::size_t Count() const noexcept;
    bool HasIdentityConstraints() const noexcept;
    const Annotation* FindSchemaAnnotation(const std::string& namespaceUri) const;
    const Annotation* FindElementAnnotation(const std::string& localName, const std::string& namespaceUri) const;
    const Annotation* FindSimpleTypeAnnotation(const std::string& localName, const std::string& namespaceUri) const;
    const Annotation* FindComplexTypeAnnotation(const std::string& localName, const std::string& namespaceUri) const;
    const Annotation* FindAttributeAnnotation(const std::string& localName, const std::string& namespaceUri) const;
    const Annotation* FindGroupAnnotation(const std::string& localName, const std::string& namespaceUri) const;
    const Annotation* FindAttributeGroupAnnotation(const std::string& localName, const std::string& namespaceUri) const;
    const Annotation* FindIdentityConstraintAnnotation(const std::string& localName, const std::string& namespaceUri) const;

private:
    enum class ContentModel {
        Empty,
        Sequence,
        Choice,
    };

    struct SimpleTypeRule {
        enum class Variety {
            Atomic,
            List,
            Union,
        };

        enum class DerivationMethod {
            None,
            Restriction,
            List,
            Union,
        };

        Variety variety = Variety::Atomic;
        std::string baseType = "xs:string";
        std::string namedTypeName;
        std::string namedTypeNamespaceUri;
        DerivationMethod derivationMethod = DerivationMethod::None;
        std::string derivationBaseName;
        std::string derivationBaseNamespaceUri;
        std::shared_ptr<SimpleTypeRule> itemType;
        std::vector<SimpleTypeRule> memberTypes;
        std::vector<std::string> enumerationValues;
        std::optional<std::string> whiteSpace;
        std::optional<std::string> pattern;
        std::optional<std::size_t> length;
        std::optional<std::size_t> minLength;
        std::optional<std::size_t> maxLength;
        std::optional<std::string> minInclusive;
        std::optional<std::string> maxInclusive;
        std::optional<std::string> minExclusive;
        std::optional<std::string> maxExclusive;
        std::optional<std::size_t> totalDigits;
        std::optional<std::size_t> fractionDigits;
        Annotation annotation;
        bool finalRestriction = false;
        bool finalList = false;
        bool finalUnion = false;
    };

    struct AttributeUse {
        std::string name;
        std::string namespaceUri;
        bool required = false;
        std::optional<SimpleTypeRule> type;
        std::optional<std::string> defaultValue;
        std::optional<std::string> fixedValue;
        bool isAny = false;
        std::string namespaceConstraint = "##any";
        std::string processContents = "strict";
        Annotation annotation;
    };

    struct ComplexTypeRule;

    struct ChildUse {
        std::string name;
        std::string namespaceUri;
        std::optional<SimpleTypeRule> declaredSimpleType;
        std::shared_ptr<ComplexTypeRule> declaredComplexType;
        bool isNillable = false;
        std::optional<std::string> defaultValue;
        std::optional<std::string> fixedValue;
        bool blockRestriction = false;
        bool blockExtension = false;
        bool finalRestriction = false;
        bool finalExtension = false;
        std::size_t minOccurs = 1;
        std::size_t maxOccurs = 1;
    };

    struct Particle {
        enum class Kind {
            Element,
            Any,
            Sequence,
            Choice,
            All,
        };

        Kind kind = Kind::Element;
        std::string name;
        std::string namespaceUri;
        std::optional<SimpleTypeRule> elementSimpleType;
        std::shared_ptr<ComplexTypeRule> elementComplexType;
        bool elementIsNillable = false;
        std::optional<std::string> elementDefaultValue;
        std::optional<std::string> elementFixedValue;
        bool elementBlockRestriction = false;
        bool elementBlockExtension = false;
        bool elementFinalRestriction = false;
        bool elementFinalExtension = false;
        std::string processContents = "strict";
        std::size_t minOccurs = 1;
        std::size_t maxOccurs = 1;
        std::vector<Particle> children;
    };

    struct ComplexTypeRule {
        enum class DerivationMethod {
            None,
            Restriction,
            Extension,
        };

        std::string namedTypeName;
        std::string namedTypeNamespaceUri;
        DerivationMethod derivationMethod = DerivationMethod::None;
        std::string derivationBaseName;
        std::string derivationBaseNamespaceUri;
        bool allowsText = false;
        std::optional<SimpleTypeRule> textType;
        ContentModel contentModel = ContentModel::Empty;
        std::vector<AttributeUse> attributes;
        std::vector<ChildUse> children;
        std::optional<Particle> particle;
        bool anyAttributeAllowed = false;
        std::string anyAttributeNamespaceConstraint = "##any";
        std::string anyAttributeProcessContents = "strict";
        bool blockRestriction = false;
        bool blockExtension = false;
        bool finalRestriction = false;
        bool finalExtension = false;
        Annotation annotation;
    };

    struct ElementRule {
        struct IdentityConstraint {
            enum class Kind {
                Key,
                Unique,
                KeyRef,
            };

            struct CompiledPathStep {
                enum class Kind {
                    Self,
                    Element,
                    DescendantElement,
                    Attribute,
                };

                Kind kind = Kind::Element;
                std::string localName;
                std::string namespaceUri;
                std::string predicateAttributeLocalName;
                std::string predicateAttributeNamespaceUri;
                std::optional<std::string> predicateAttributeValue;
            };

            struct CompiledPath {
                std::vector<CompiledPathStep> steps;
            };

            Kind kind = Kind::Key;
            std::string name;
            std::string namespaceUri;
            std::string selectorXPath;
            std::optional<CompiledPath> compiledSelectorPath;
            std::vector<std::string> fieldXPaths;
            std::vector<std::optional<CompiledPath>> compiledFieldPaths;
            std::vector<std::pair<std::string, std::string>> namespaceBindings;
            std::string referName;
            std::string referNamespaceUri;
            Annotation annotation;
        };

        std::string name;
        std::string namespaceUri;
        bool isAbstract = false;
        bool isNillable = false;
        std::optional<std::string> defaultValue;
        std::optional<std::string> fixedValue;
        bool blockSubstitution = false;
        bool blockRestriction = false;
        bool blockExtension = false;
        bool finalSubstitution = false;
        bool finalRestriction = false;
        bool finalExtension = false;
        std::string substitutionGroupHeadName;
        std::string substitutionGroupHeadNamespaceUri;
        std::optional<SimpleTypeRule> declaredSimpleType;
        std::optional<ComplexTypeRule> declaredComplexType;
        bool allowsText = true;
        std::optional<SimpleTypeRule> textType;
        ContentModel contentModel = ContentModel::Empty;
        std::vector<AttributeUse> attributes;
        std::vector<ChildUse> children;
        std::optional<Particle> particle;
        std::vector<IdentityConstraint> identityConstraints;
        bool anyAttributeAllowed = false;
        std::string anyAttributeNamespaceConstraint = "##any";
        std::string anyAttributeProcessContents = "strict";
        Annotation annotation;
    };

    struct NamedSimpleTypeRule {
        std::string name;
        std::string namespaceUri;
        SimpleTypeRule rule;
    };

    struct NamedComplexTypeRule {
        std::string name;
        std::string namespaceUri;
        ComplexTypeRule rule;
    };

    struct NamedAttributeRule {
        std::string name;
        std::string namespaceUri;
        AttributeUse rule;
    };

    struct NamedGroupRule {
        std::string name;
        std::string namespaceUri;
        Particle rule;
    };

    struct AttributeGroupRule {
        std::vector<AttributeUse> attributes;
        bool anyAttributeAllowed = false;
        std::string anyAttributeNamespaceConstraint = "##any";
        std::string anyAttributeProcessContents = "strict";
    };

    struct NamedAttributeGroupRule {
        std::string name;
        std::string namespaceUri;
        AttributeGroupRule rule;
    };

    struct NamedSchemaAnnotation {
        std::string namespaceUri;
        Annotation annotation;
    };

    struct NamedGroupAnnotation {
        std::string name;
        std::string namespaceUri;
        Annotation annotation;
    };

    struct NamedAttributeGroupAnnotation {
        std::string name;
        std::string namespaceUri;
        Annotation annotation;
    };

    friend std::string NormalizeSchemaSimpleTypeValue(const SimpleTypeRule& type, const std::string& lexicalValue);
    friend void ValidateSchemaSimpleValueWithQNameResolver(
        const SimpleTypeRule& type,
        const std::string& value,
        const std::string& label,
        const std::function<std::string(const std::string&)>& resolveNamespaceUri,
        const std::function<bool(const std::string&)>& hasNotationDeclaration,
        const std::function<bool(const std::string&)>& hasUnparsedEntityDeclaration);
    friend void ValidateSchemaSimpleValue(
        const SimpleTypeRule& type,
        const std::string& value,
        const std::string& label,
        const XmlElement* contextElement);
    friend void ValidateSchemaSimpleValue(
        const SimpleTypeRule& type,
        const std::string& value,
        const std::string& label,
        const std::vector<std::pair<std::string, std::string>>& namespaceBindings,
        const std::unordered_set<std::string>& notationDeclarationNames,
        const std::unordered_set<std::string>& unparsedEntityDeclarationNames);

    std::vector<ElementRule> elements_;
    std::vector<NamedSimpleTypeRule> simpleTypes_;
    std::vector<NamedComplexTypeRule> complexTypes_;
    std::vector<NamedAttributeRule> attributes_;
    std::vector<NamedGroupRule> groupRules_;
    std::vector<NamedAttributeGroupRule> attributeGroupRules_;
    std::vector<NamedSchemaAnnotation> schemaAnnotations_;
    std::vector<NamedGroupAnnotation> groups_;
    std::vector<NamedAttributeGroupAnnotation> attributeGroups_;
    std::vector<ElementRule::IdentityConstraint> identityConstraints_;
    std::unordered_set<std::string> loadedSchemaFiles_;
    std::unordered_set<std::string> activeSchemaFiles_;

    const ElementRule* FindElementRule(const std::string& localName, const std::string& namespaceUri) const;
    const SimpleTypeRule* FindSimpleTypeRule(const std::string& localName, const std::string& namespaceUri) const;
    const ComplexTypeRule* FindComplexTypeRule(const std::string& localName, const std::string& namespaceUri) const;
    const AttributeUse* FindAttributeRule(const std::string& localName, const std::string& namespaceUri) const;
    const Particle* FindGroupRule(const std::string& localName, const std::string& namespaceUri) const;
    const AttributeGroupRule* FindAttributeGroupRule(const std::string& localName, const std::string& namespaceUri) const;
    const Annotation* FindStoredSchemaAnnotation(const std::string& namespaceUri) const;
    const Annotation* FindStoredGroupAnnotation(const std::string& localName, const std::string& namespaceUri) const;
    const Annotation* FindStoredAttributeGroupAnnotation(const std::string& localName, const std::string& namespaceUri) const;
    const ElementRule::IdentityConstraint* FindIdentityConstraint(const std::string& localName, const std::string& namespaceUri) const;

    friend class XmlDocument;
};

class XPathNavigator final {
public:
    XPathNavigator() = default;
    explicit XPathNavigator(const XmlNode* node);

    bool IsEmpty() const noexcept;
    XmlNodeType NodeType() const;
    std::string Name() const;
    std::string LocalName() const;
    std::string Prefix() const;
    std::string NamespaceURI() const;
    std::string Value() const;
    std::string InnerXml() const;
    std::string OuterXml() const;

    XPathNavigator Clone() const;

    bool MoveToFirstChild();
    bool MoveToNext();
    bool MoveToPrevious();
    bool MoveToParent();
    bool MoveToFirstAttribute();
    bool MoveToNextAttribute();
    void MoveToRoot();

    XPathNavigator SelectSingleNode(const std::string& xpath) const;
    XPathNavigator SelectSingleNode(const std::string& xpath, const XmlNamespaceManager& namespaces) const;
    std::vector<XPathNavigator> Select(const std::string& xpath) const;
    std::vector<XPathNavigator> Select(const std::string& xpath, const XmlNamespaceManager& namespaces) const;

    const XmlNode* UnderlyingNode() const noexcept;

private:
    const XmlNode* node_ = nullptr;
};

class XPathDocument final {
public:
    XPathDocument();
    explicit XPathDocument(const std::string& xml);

    static std::shared_ptr<XPathDocument> Parse(const std::string& xml);

    void LoadXml(const std::string& xml);
    void Load(const std::string& path);
    void Load(std::istream& stream);

    XPathNavigator CreateNavigator() const;
    const XmlDocument& Document() const noexcept;

private:
    std::shared_ptr<XmlDocument> document_;
};

}  // namespace System::Xml