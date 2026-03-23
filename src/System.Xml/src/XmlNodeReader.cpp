#include "XmlInternal.h"

namespace System::Xml {

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

}  // namespace System::Xml
