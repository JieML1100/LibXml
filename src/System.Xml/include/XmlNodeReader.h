#pragma once

#include "XmlTypes.h"
#include "XmlNameTable.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace System::Xml {

class XmlNode;

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

}  // namespace System::Xml
