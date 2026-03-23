#include "XmlInternal.h"

namespace System::Xml {

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
