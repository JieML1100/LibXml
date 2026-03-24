#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace System::Xml {

class XmlNode;
class XmlDocument;
class XmlNamespaceManager;
enum class XmlNodeType;

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

    XPathNavigator SelectSingleNode(std::string_view xpath) const;
    XPathNavigator SelectSingleNode(std::string_view xpath, const XmlNamespaceManager& namespaces) const;
    std::vector<XPathNavigator> Select(std::string_view xpath) const;
    std::vector<XPathNavigator> Select(std::string_view xpath, const XmlNamespaceManager& namespaces) const;

    const XmlNode* UnderlyingNode() const noexcept;

private:
    const XmlNode* node_ = nullptr;
};

class XPathDocument final {
public:
    XPathDocument();
    explicit XPathDocument(std::string_view xml);

    static std::shared_ptr<XPathDocument> Parse(std::string_view xml);

    void LoadXml(std::string_view xml);
    void Load(const std::string& path);
    void Load(std::istream& stream);

    XPathNavigator CreateNavigator() const;
    const XmlDocument& Document() const noexcept;

private:
    std::shared_ptr<XmlDocument> document_;
};

}  // namespace System::Xml
