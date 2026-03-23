#pragma once

#include "XmlNode.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace System::Xml {

class XmlSchemaSet;
class XmlReader;
class XmlWriter;

class XmlDocument final : public XmlNode {
public:
    XmlDocument();

    static std::shared_ptr<XmlDocument> Parse(const std::string& xml);

    void LoadXml(const std::string& xml);
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

}  // namespace System::Xml
