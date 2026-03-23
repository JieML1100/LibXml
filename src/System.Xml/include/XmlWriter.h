#pragma once

#include "XmlDocument.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace System::Xml {

class XmlReader;

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

}  // namespace System::Xml
