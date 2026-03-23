#include "XmlInternal.h"

namespace System::Xml {

XmlWriter::XmlWriter(XmlWriterSettings settings) : settings_(std::move(settings)) {
    fragmentRoot_ = document_.CreateDocumentFragment();
}

XmlWriter::XmlWriter(std::ostream& stream, XmlWriterSettings settings)
    : settings_(std::move(settings)), directOutputStream_(&stream) {
    fragmentRoot_ = document_.CreateDocumentFragment();
}

bool XmlWriter::UsesDirectOutput() const noexcept {
    return directOutputStream_ != nullptr;
}

WriteState XmlWriter::GetWriteState() const noexcept {
    if (documentClosed_) return WriteState::Closed;
    if (currentAttributeName_.has_value()) return WriteState::Attribute;
    if (UsesDirectOutput()) {
        if (!directElementStack_.empty()) {
            if (directElementStack_.back().startTagOpen) return WriteState::Element;
            return WriteState::Content;
        }
        if (startDocumentWritten_ || directHasTopLevelContent_) return WriteState::Prolog;
        return WriteState::Start;
    }
    if (!elementStack_.empty()) {
        if (!startTagOpenStack_.empty() && startTagOpenStack_.back()) return WriteState::Element;
        return WriteState::Content;
    }
    if (startDocumentWritten_ || !document_.ChildNodes().empty()) return WriteState::Prolog;
    return WriteState::Start;
}

std::string XmlWriter::LookupPrefix(const std::string& namespaceUri) const {
    if (UsesDirectOutput()) {
        return LookupDirectNamespacePrefix(namespaceUri);
    }
    return LookupNamespacePrefix(namespaceUri);
}

void XmlWriter::EnsureDocumentOpen(const std::string& operation) const {
    if (documentClosed_) {
        throw XmlException("Cannot " + operation + " after the XML document has been closed");
    }
}

void XmlWriter::EnsureOpenElement(const std::string& operation) const {
    EnsureDocumentOpen(operation);
    const bool hasOpenElement = UsesDirectOutput() ? !directElementStack_.empty() : !elementStack_.empty();
    if (!hasOpenElement) {
        throw XmlException("Cannot " + operation + " outside of an element");
    }
}

void XmlWriter::EnsureOpenStartElement(const std::string& operation) const {
    EnsureOpenElement(operation);
    const bool startTagOpen = UsesDirectOutput()
        ? directElementStack_.back().startTagOpen
        : startTagOpenStack_.back();
    if (!startTagOpen) {
        throw XmlException("Cannot " + operation + " after the current element start tag has been closed");
    }
}

void XmlWriter::EnsureNoOpenAttribute(const std::string& operation) const {
    if (currentAttributeName_.has_value()) {
        throw XmlException("Cannot " + operation + " while an attribute is still open");
    }
}

void XmlWriter::EnsureOutputReady() const {
    EnsureNoOpenAttribute("produce XML output");
    const bool hasOpenElements = UsesDirectOutput() ? !directElementStack_.empty() : !elementStack_.empty();
    if (hasOpenElements) {
        throw XmlException("Cannot produce XML output while elements are still open");
    }
    if (settings_.Conformance == ConformanceLevel::Fragment) {
        return;
    }
    const bool hasRootElement = UsesDirectOutput()
        ? directHasRootElement_
        : document_.DocumentElement() != nullptr;
    if (!hasRootElement) {
        throw XmlException("Cannot produce XML output without a root element");
    }
}

void XmlWriter::MarkCurrentElementContentStarted() {
    if (UsesDirectOutput()) {
        if (!directElementStack_.empty()) {
            directElementStack_.back().startTagOpen = false;
        }
        return;
    }
    if (!startTagOpenStack_.empty()) {
        startTagOpenStack_.back() = false;
    }
}

std::string XmlWriter::LookupNamespacePrefix(const std::string& namespaceUri) const {
    if (namespaceUri.empty()) {
        return {};
    }

    if (namespaceUri == "http://www.w3.org/XML/1998/namespace") {
        return "xml";
    }
    if (elementStack_.empty()) {
        return {};
    }

    for (const XmlElement* current = elementStack_.back().get(); current != nullptr; ) {
        for (const auto& attribute : current->Attributes()) {
            if (!IsNamespaceDeclarationName(attribute->Name())) {
                continue;
            }
            if (attribute->Value() == namespaceUri) {
                return NamespaceDeclarationPrefix(attribute->Name());
            }
        }

        const XmlNode* parent = current->ParentNode();
        current = parent != nullptr && parent->NodeType() == XmlNodeType::Element
            ? static_cast<const XmlElement*>(parent)
            : nullptr;
    }

    return {};
}

void XmlWriter::WriteDirectTopLevelSeparatorIfNeeded() {
    if (settings_.Indent && directHasTopLevelContent_) {
        *directOutputStream_ << settings_.NewLineChars;
    }
}

void XmlWriter::EnsureDirectCurrentStartTagClosed() {
    if (directElementStack_.empty() || !directElementStack_.back().startTagOpen) {
        return;
    }
    *directOutputStream_ << '>';
    directElementStack_.back().startTagOpen = false;
}

void XmlWriter::PrepareDirectParentForChild(bool textLikeChild) {
    if (directElementStack_.empty()) {
        return;
    }

    auto& parent = directElementStack_.back();
    EnsureDirectCurrentStartTagClosed();
    if (!textLikeChild && settings_.Indent && !parent.hasTextLikeChild) {
        *directOutputStream_ << settings_.NewLineChars;
        std::string indentation;
        AppendIndent(indentation, static_cast<int>(directElementStack_.size()), settings_);
        *directOutputStream_ << indentation;
    }
    parent.hasAnyChild = true;
    if (textLikeChild) {
        parent.hasTextLikeChild = true;
    } else {
        parent.hasNonTextChild = true;
    }
}

void XmlWriter::WriteDirectAttribute(const std::string& name, const std::string& value) {
    *directOutputStream_ << ' ' << name << "=\"" << EscapeAttribute(value, settings_) << '"';
}

bool XmlWriter::HasDirectNamespaceBinding(const std::string& prefix, const std::string& namespaceUri) const {
    if (namespaceUri.empty()) {
        return prefix.empty();
    }

    if (prefix == "xml") {
        return namespaceUri == "http://www.w3.org/XML/1998/namespace";
    }

    for (auto it = directElementStack_.rbegin(); it != directElementStack_.rend(); ++it) {
        const auto found = it->namespaceDeclarations.find(prefix);
        if (found != it->namespaceDeclarations.end()) {
            return found->second == namespaceUri;
        }
    }

    return false;
}

std::string XmlWriter::LookupDirectNamespacePrefix(const std::string& namespaceUri) const {
    if (namespaceUri.empty()) {
        return {};
    }
    if (namespaceUri == "http://www.w3.org/XML/1998/namespace") {
        return "xml";
    }

    for (auto it = directElementStack_.rbegin(); it != directElementStack_.rend(); ++it) {
        for (const auto& [prefix, uri] : it->namespaceDeclarations) {
            if (uri == namespaceUri) {
                return prefix;
            }
        }
    }

    return {};
}

void XmlWriter::DeclareDirectNamespaceIfNeeded(const std::string& prefix, const std::string& namespaceUri) {
    if (namespaceUri.empty() || directElementStack_.empty() || HasDirectNamespaceBinding(prefix, namespaceUri)) {
        return;
    }

    auto& current = directElementStack_.back();
    const std::string attributeName = prefix.empty() ? "xmlns" : "xmlns:" + prefix;
    WriteDirectAttribute(attributeName, namespaceUri);
    current.namespaceDeclarations[prefix] = namespaceUri;
}

void XmlWriter::WriteDirectTopLevelNode(const XmlNode& node) {
    if (settings_.Conformance == ConformanceLevel::Fragment) {
        WriteDirectTopLevelSeparatorIfNeeded();
        SerializeNodeToStream(node, settings_, *directOutputStream_, 0);
        directHasTopLevelContent_ = true;
        return;
    }

    switch (node.NodeType()) {
    case XmlNodeType::XmlDeclaration:
        if (directHasTopLevelContent_) {
            throw XmlException("XML declaration must be the first node in the document");
        }
        break;
    case XmlNodeType::DocumentType:
        if (directHasDocumentType_) {
            throw XmlException("XML document can only contain a single DOCTYPE declaration");
        }
        if (directHasRootElement_) {
            throw XmlException("DOCTYPE must appear before the root element");
        }
        break;
    case XmlNodeType::Element:
        if (directHasRootElement_) {
            throw XmlException("The XML document already has a root element");
        }
        break;
    default:
        break;
    }

    WriteDirectTopLevelSeparatorIfNeeded();
    SerializeNodeToStream(node, settings_, *directOutputStream_, 0);
    directHasTopLevelContent_ = true;
    if (node.NodeType() == XmlNodeType::DocumentType) {
        directHasDocumentType_ = true;
    } else if (node.NodeType() == XmlNodeType::Element) {
        directHasRootElement_ = true;
    }
}

void XmlWriter::WriteStartDocument(const std::string& version, const std::string& encoding, const std::string& standalone) {
    EnsureDocumentOpen("write the XML declaration");
    EnsureNoOpenAttribute("write the XML declaration");
    if (settings_.Conformance == ConformanceLevel::Fragment) {
        throw XmlException("XML declaration is only allowed in document conformance mode");
    }
    if (UsesDirectOutput()) {
        if (startDocumentWritten_) {
            throw XmlException("XML declaration has already been written");
        }
        if (!directElementStack_.empty()) {
            throw XmlException("Cannot write XML declaration inside an element");
        }
        if (directHasTopLevelContent_) {
            throw XmlException("XML declaration must be the first node in the document");
        }

        const std::string effectiveEncoding = encoding.empty() ? settings_.Encoding : encoding;
        *directOutputStream_ << "<?xml version=\"" << EscapeAttribute(version, settings_) << '\"';
        if (!effectiveEncoding.empty()) {
            *directOutputStream_ << " encoding=\"" << EscapeAttribute(effectiveEncoding, settings_) << '\"';
        }
        if (!standalone.empty()) {
            *directOutputStream_ << " standalone=\"" << EscapeAttribute(standalone, settings_) << '\"';
        }
        *directOutputStream_ << "?>";
        startDocumentWritten_ = true;
        directHasTopLevelContent_ = true;
        return;
    }
    if (document_.Declaration() != nullptr) {
        throw XmlException("XML declaration has already been written");
    }
    if (!elementStack_.empty()) {
        throw XmlException("Cannot write XML declaration inside an element");
    }
    if (!document_.ChildNodes().empty()) {
        throw XmlException("XML declaration must be the first node in the document");
    }

    const std::string effectiveEncoding = encoding.empty() ? settings_.Encoding : encoding;
    document_.AppendChild(document_.CreateXmlDeclaration(version, effectiveEncoding, standalone));
    startDocumentWritten_ = true;
}

void XmlWriter::WriteDocType(
    const std::string& name,
    const std::string& publicId,
    const std::string& systemId,
    const std::string& internalSubset) {
    EnsureDocumentOpen("write a document type declaration");
    EnsureNoOpenAttribute("write a document type declaration");
    if (settings_.Conformance == ConformanceLevel::Fragment) {
        throw XmlException("DOCTYPE is only allowed in document conformance mode");
    }
    if (UsesDirectOutput()) {
        if (!directElementStack_.empty()) {
            throw XmlException("Cannot write DOCTYPE inside an element");
        }
        if (directHasRootElement_) {
            throw XmlException("DOCTYPE must appear before the root element");
        }
        if (directHasDocumentType_) {
            throw XmlException("XML document can only contain a single DOCTYPE declaration");
        }

        WriteDirectTopLevelSeparatorIfNeeded();
        *directOutputStream_ << "<!DOCTYPE " << name;
        if (!publicId.empty()) {
            *directOutputStream_ << " PUBLIC \"" << publicId << "\" \"" << systemId << "\"";
        } else if (!systemId.empty()) {
            *directOutputStream_ << " SYSTEM \"" << systemId << "\"";
        }
        if (!internalSubset.empty()) {
            *directOutputStream_ << " [" << internalSubset << ']';
        }
        *directOutputStream_ << '>';
        directHasDocumentType_ = true;
        directHasTopLevelContent_ = true;
        return;
    }
    if (!elementStack_.empty()) {
        throw XmlException("Cannot write DOCTYPE inside an element");
    }
    if (document_.DocumentElement() != nullptr) {
        throw XmlException("DOCTYPE must appear before the root element");
    }

    document_.AppendChild(document_.CreateDocumentType(name, publicId, systemId, internalSubset));
}

void XmlWriter::WriteStartElement(const std::string& name) {
    EnsureDocumentOpen("write a start element");
    EnsureNoOpenAttribute("write a start element");
    if (UsesDirectOutput()) {
        if (directElementStack_.empty()) {
            if (settings_.Conformance != ConformanceLevel::Fragment && directHasRootElement_) {
                throw XmlException("The XML document already has a root element");
            }
            WriteDirectTopLevelSeparatorIfNeeded();
            if (settings_.Conformance != ConformanceLevel::Fragment) {
                directHasRootElement_ = true;
            }
            directHasTopLevelContent_ = true;
        } else {
            PrepareDirectParentForChild(false);
        }

        directElementStack_.push_back(DirectElementState{});
        directElementStack_.back().name = name;
        *directOutputStream_ << '<' << name;
        return;
    }
    if (settings_.Conformance != ConformanceLevel::Fragment
        && elementStack_.empty() && document_.DocumentElement() != nullptr) {
        throw XmlException("The XML document already has a root element");
    }

    auto element = document_.CreateElement(name);
    if (!elementStack_.empty()) {
        MarkCurrentElementContentStarted();
    }
    if (elementStack_.empty()) {
        AppendDocumentLevelNode(element);
    } else {
        elementStack_.back()->AppendChild(element);
    }
    elementStack_.push_back(element);
    startTagOpenStack_.push_back(true);
}

void XmlWriter::WriteStartElement(const std::string& prefix, const std::string& localName, const std::string& namespaceUri) {
    WriteStartElement(ComposeQualifiedName(prefix, localName));
    if (UsesDirectOutput()) {
        if (!namespaceUri.empty()) {
            DeclareDirectNamespaceIfNeeded(prefix, namespaceUri);
        }
        return;
    }
    if (!namespaceUri.empty()) {
        const std::string xmlnsName = prefix.empty() ? "xmlns" : "xmlns:" + prefix;
        if (!elementStack_.back()->HasAttribute(xmlnsName)) {
            elementStack_.back()->SetAttribute(xmlnsName, namespaceUri);
        }
    }
}

void XmlWriter::WriteStartAttribute(const std::string& name) {
    EnsureOpenStartElement("write an attribute");
    EnsureNoOpenAttribute("write an attribute");
    currentAttributeName_ = name;
    currentAttributeValue_.clear();
}

void XmlWriter::WriteStartAttribute(
    const std::string& prefix,
    const std::string& localName,
    const std::string& namespaceUri) {
    WriteStartAttribute(ComposeQualifiedName(prefix, localName));

    if (UsesDirectOutput()) {
        if (!prefix.empty() && !namespaceUri.empty()) {
            DeclareDirectNamespaceIfNeeded(prefix, namespaceUri);
        }
        return;
    }

    if (!prefix.empty() && !namespaceUri.empty()) {
        const std::string xmlnsName = "xmlns:" + prefix;
        if (!elementStack_.back()->HasAttribute(xmlnsName)) {
            elementStack_.back()->SetAttribute(xmlnsName, namespaceUri);
        }
    }
}

void XmlWriter::WriteEndAttribute() {
    EnsureDocumentOpen("end an attribute");
    if (!currentAttributeName_.has_value()) {
        throw XmlException("Cannot end an attribute when no attribute is open");
    }

    if (UsesDirectOutput()) {
        WriteDirectAttribute(*currentAttributeName_, currentAttributeValue_);
        currentAttributeName_.reset();
        currentAttributeValue_.clear();
        return;
    }

    elementStack_.back()->SetAttribute(*currentAttributeName_, currentAttributeValue_);
    currentAttributeName_.reset();
    currentAttributeValue_.clear();
}

void XmlWriter::WriteAttributeString(const std::string& name, const std::string& value) {
    WriteStartAttribute(name);
    WriteString(value);
    WriteEndAttribute();
}

void XmlWriter::WriteAttributeString(
    const std::string& prefix,
    const std::string& localName,
    const std::string& namespaceUri,
    const std::string& value) {
    WriteStartAttribute(prefix, localName, namespaceUri);
    WriteString(value);
    WriteEndAttribute();
}

void XmlWriter::WriteAttributes(const XmlAttributeCollection& attributes) {
    EnsureOpenStartElement("write attributes");
    EnsureNoOpenAttribute("write attributes");
    for (const auto& attribute : attributes) {
        if (attribute != nullptr) {
            WriteAttributeString(attribute->Name(), attribute->Value());
        }
    }
}

void XmlWriter::WriteAttributes(const XmlElement& element) {
    EnsureOpenStartElement("write attributes");
    EnsureNoOpenAttribute("write attributes");
    for (const auto& attribute : element.attributes_) {
        if (attribute != nullptr) {
            WriteAttributeString(attribute->Name(), attribute->Value());
        }
    }
    for (const auto& pendingAttribute : element.pendingLoadAttributes_) {
        WriteAttributeString(
            std::string(element.PendingLoadAttributeNameView(pendingAttribute)),
            std::string(element.PendingLoadAttributeValueView(pendingAttribute)));
    }
}

void XmlWriter::WriteCharEntity(unsigned int codePoint) {
    EnsureNoOpenAttribute("write a character entity");
    EnsureOpenElement("write a character entity");
    if (codePoint == 0 || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
        throw XmlException("WriteCharEntity requires a valid Unicode scalar value");
    }

    std::ostringstream name;
    name << "#x" << std::uppercase << std::hex << codePoint;
    if (UsesDirectOutput()) {
        PrepareDirectParentForChild(true);
        *directOutputStream_ << '&' << name.str() << ';';
        return;
    }
    MarkCurrentElementContentStarted();
    elementStack_.back()->AppendChild(document_.CreateEntityReference(name.str()));
}

void XmlWriter::WriteEntityRef(const std::string& name) {
    EnsureNoOpenAttribute("write an entity reference");
    EnsureOpenElement("write an entity reference");
    if (name.empty()) {
        throw XmlException("WriteEntityRef requires an entity name");
    }

    if (UsesDirectOutput()) {
        PrepareDirectParentForChild(true);
        *directOutputStream_ << '&' << name << ';';
        return;
    }

    MarkCurrentElementContentStarted();
    elementStack_.back()->AppendChild(document_.CreateEntityReference(name));
}

void XmlWriter::WriteString(const std::string& text) {
    if (currentAttributeName_.has_value()) {
        currentAttributeValue_ += text;
        return;
    }

    EnsureOpenElement("write text");

    if (UsesDirectOutput()) {
        PrepareDirectParentForChild(true);
        *directOutputStream_ << EscapeText(text, settings_);
        return;
    }

    MarkCurrentElementContentStarted();
    elementStack_.back()->AppendChild(document_.CreateTextNode(text));
}

void XmlWriter::WriteValue(const std::string& value) {
    WriteString(value);
}

void XmlWriter::WriteValue(bool value) {
    WriteString(value ? "true" : "false");
}

void XmlWriter::WriteValue(int value) {
    WriteString(std::to_string(value));
}

void XmlWriter::WriteValue(double value) {
    std::ostringstream stream;
    stream << value;
    WriteString(stream.str());
}

void XmlWriter::WriteName(const std::string& name) {
    if (!IsValidXmlQualifiedName(name)) {
        throw XmlException("WriteName requires a valid XML name");
    }
    WriteString(name);
}

void XmlWriter::WriteQualifiedName(const std::string& localName, const std::string& namespaceUri) {
    if (!IsValidXmlNameToken(localName)) {
        throw XmlException("WriteQualifiedName requires a valid XML local name");
    }

    if (namespaceUri.empty()) {
        WriteName(localName);
        return;
    }

    const auto prefix = UsesDirectOutput()
        ? LookupDirectNamespacePrefix(namespaceUri)
        : LookupNamespacePrefix(namespaceUri);
    if (!prefix.empty()) {
        WriteName(ComposeQualifiedName(prefix, localName));
        return;
    }

    if (!UsesDirectOutput() && !elementStack_.empty() && LookupNamespaceUriOnElement(elementStack_.back().get(), {}) == namespaceUri) {
        WriteName(localName);
        return;
    }

    if (UsesDirectOutput() && HasDirectNamespaceBinding({}, namespaceUri)) {
        WriteName(localName);
        return;
    }

    throw XmlException("WriteQualifiedName requires a namespace prefix bound to URI: " + namespaceUri);
}

void XmlWriter::WriteWhitespace(const std::string& whitespace) {
    EnsureDocumentOpen("write whitespace");
    EnsureNoOpenAttribute("write whitespace");
    if (UsesDirectOutput()) {
        if (directElementStack_.empty()) {
            WriteDirectTopLevelSeparatorIfNeeded();
            *directOutputStream_ << EscapeText(whitespace, settings_);
            directHasTopLevelContent_ = true;
        } else {
            PrepareDirectParentForChild(true);
            *directOutputStream_ << EscapeText(whitespace, settings_);
        }
        return;
    }
    auto node = document_.CreateWhitespace(whitespace);
    if (elementStack_.empty()) {
        AppendDocumentLevelNode(node);
    } else {
        MarkCurrentElementContentStarted();
        elementStack_.back()->AppendChild(node);
    }
}

void XmlWriter::WriteSignificantWhitespace(const std::string& whitespace) {
    EnsureDocumentOpen("write significant whitespace");
    EnsureNoOpenAttribute("write significant whitespace");
    if (UsesDirectOutput()) {
        if (directElementStack_.empty()) {
            WriteDirectTopLevelSeparatorIfNeeded();
            *directOutputStream_ << EscapeText(whitespace, settings_);
            directHasTopLevelContent_ = true;
        } else {
            PrepareDirectParentForChild(true);
            *directOutputStream_ << EscapeText(whitespace, settings_);
        }
        return;
    }
    auto node = document_.CreateSignificantWhitespace(whitespace);
    if (elementStack_.empty()) {
        AppendDocumentLevelNode(node);
    } else {
        MarkCurrentElementContentStarted();
        elementStack_.back()->AppendChild(node);
    }
}

void XmlWriter::WriteCData(const std::string& text) {
    EnsureNoOpenAttribute("write CDATA");
    EnsureOpenElement("write CDATA");

    if (UsesDirectOutput()) {
        PrepareDirectParentForChild(true);
        *directOutputStream_ << "<![CDATA[" << text << "]]>";
        return;
    }

    MarkCurrentElementContentStarted();
    elementStack_.back()->AppendChild(document_.CreateCDataSection(text));
}

void XmlWriter::WriteComment(const std::string& text) {
    EnsureDocumentOpen("write a comment");
    EnsureNoOpenAttribute("write a comment");
    if (UsesDirectOutput()) {
        if (directElementStack_.empty()) {
            WriteDirectTopLevelSeparatorIfNeeded();
            *directOutputStream_ << "<!--" << text << "-->";
            directHasTopLevelContent_ = true;
        } else {
            PrepareDirectParentForChild(false);
            *directOutputStream_ << "<!--" << text << "-->";
        }
        return;
    }
    auto comment = document_.CreateComment(text);
    if (elementStack_.empty()) {
        AppendDocumentLevelNode(comment);
    } else {
        MarkCurrentElementContentStarted();
        elementStack_.back()->AppendChild(comment);
    }
}

void XmlWriter::WriteProcessingInstruction(const std::string& name, const std::string& text) {
    EnsureDocumentOpen("write a processing instruction");
    EnsureNoOpenAttribute("write a processing instruction");
    if (UsesDirectOutput()) {
        if (directElementStack_.empty()) {
            WriteDirectTopLevelSeparatorIfNeeded();
            *directOutputStream_ << "<?" << name;
            if (!text.empty()) {
                *directOutputStream_ << ' ' << text;
            }
            *directOutputStream_ << "?>";
            directHasTopLevelContent_ = true;
        } else {
            PrepareDirectParentForChild(false);
            *directOutputStream_ << "<?" << name;
            if (!text.empty()) {
                *directOutputStream_ << ' ' << text;
            }
            *directOutputStream_ << "?>";
        }
        return;
    }
    auto instruction = document_.CreateProcessingInstruction(name, text);
    if (elementStack_.empty()) {
        AppendDocumentLevelNode(instruction);
    } else {
        MarkCurrentElementContentStarted();
        elementStack_.back()->AppendChild(instruction);
    }
}

void XmlWriter::WriteRaw(const std::string& xml) {
    EnsureDocumentOpen("write raw XML");
    EnsureNoOpenAttribute("write raw XML");
    if (xml.empty()) {
        return;
    }

    if (UsesDirectOutput()) {
        if (!directElementStack_.empty()) {
            XmlDocument fragmentDocument;
            auto fragment = fragmentDocument.CreateDocumentFragment();
            fragment->SetInnerXml(xml);
            for (const auto& child : fragment->ChildNodes()) {
                if (child != nullptr) {
                    WriteNode(*child);
                }
            }
            return;
        }

        try {
            XmlDocument scratch;
            scratch.LoadXml(xml);
            for (const auto& child : scratch.ChildNodes()) {
                if (child != nullptr) {
                    WriteNode(*child);
                }
            }
            return;
        } catch (const XmlException&) {
            XmlDocument fragmentDocument;
            auto fragment = fragmentDocument.CreateDocumentFragment();
            fragment->SetInnerXml(xml);
            for (const auto& child : fragment->ChildNodes()) {
                if (child != nullptr) {
                    WriteNode(*child);
                }
            }
            return;
        }
    }

    if (!elementStack_.empty()) {
        auto fragment = document_.CreateDocumentFragment();
        fragment->SetInnerXml(xml);
        MarkCurrentElementContentStarted();
        elementStack_.back()->AppendChild(fragment);
        return;
    }

    try {
        XmlDocument scratch;
        scratch.SetPreserveWhitespace(document_.PreserveWhitespace());
        scratch.LoadXml(xml);
        for (const auto& child : scratch.ChildNodes()) {
            if (child != nullptr) {
                AppendDocumentLevelNode(document_.ImportNode(*child, true));
            }
        }
        return;
    } catch (const XmlException&) {
        auto fragment = document_.CreateDocumentFragment();
        fragment->SetInnerXml(xml);
        AppendDocumentLevelNode(fragment);
    }
}

void XmlWriter::WriteBase64(const unsigned char* data, std::size_t length) {
    EnsureDocumentOpen("write base64 content");
    EnsureNoOpenAttribute("write base64 content");
    if (data == nullptr && length != 0) {
        throw XmlException("WriteBase64 requires a non-null buffer when length is non-zero");
    }
    WriteString(EncodeBase64(data, length));
}

void XmlWriter::WriteElementString(const std::string& name, const std::string& value) {
    EnsureDocumentOpen("write an element string");
    EnsureNoOpenAttribute("write an element string");
    WriteStartElement(name);
    WriteString(value);
    WriteEndElement();
}

void XmlWriter::WriteElementString(const std::string& prefix, const std::string& localName, const std::string& namespaceUri, const std::string& value) {
    EnsureDocumentOpen("write an element string");
    EnsureNoOpenAttribute("write an element string");
    WriteStartElement(prefix, localName, namespaceUri);
    WriteString(value);
    WriteEndElement();
}

void XmlWriter::WriteNode(const XmlNode& node) {
    EnsureDocumentOpen("write a node");
    EnsureNoOpenAttribute("write a node");

    if (node.NodeType() == XmlNodeType::Document || node.NodeType() == XmlNodeType::DocumentFragment) {
        for (const auto& child : node.ChildNodes()) {
            if (child != nullptr) {
                WriteNode(*child);
            }
        }
        return;
    }

    if (node.NodeType() == XmlNodeType::Attribute) {
        EnsureOpenStartElement("write an attribute node");
        if (UsesDirectOutput()) {
            WriteAttributeString(node.Name(), node.Value());
            return;
        }
        elementStack_.back()->SetAttribute(node.Name(), node.Value());
        return;
    }

    if (UsesDirectOutput()) {
        if (directElementStack_.empty()) {
            if (node.NodeType() == XmlNodeType::XmlDeclaration || node.NodeType() == XmlNodeType::DocumentType || node.NodeType() == XmlNodeType::Element
                || node.NodeType() == XmlNodeType::Comment || node.NodeType() == XmlNodeType::ProcessingInstruction
                || node.NodeType() == XmlNodeType::Whitespace || node.NodeType() == XmlNodeType::SignificantWhitespace
                || node.NodeType() == XmlNodeType::Text || node.NodeType() == XmlNodeType::CDATA || node.NodeType() == XmlNodeType::EntityReference) {
                WriteDirectTopLevelNode(node);
                return;
            }
            throw XmlException("Unsupported node type for serialization");
        }

        if (node.NodeType() == XmlNodeType::XmlDeclaration || node.NodeType() == XmlNodeType::DocumentType) {
            throw XmlException("Cannot write document-level nodes inside an element");
        }

        const bool textLike = node.NodeType() == XmlNodeType::Text
            || node.NodeType() == XmlNodeType::Whitespace
            || node.NodeType() == XmlNodeType::SignificantWhitespace
            || node.NodeType() == XmlNodeType::CDATA
            || node.NodeType() == XmlNodeType::EntityReference;
        PrepareDirectParentForChild(textLike);
        SerializeNodeToStream(node, settings_, *directOutputStream_, static_cast<int>(directElementStack_.size()));
        return;
    }

    auto imported = document_.ImportNode(node, true);
    if (elementStack_.empty()) {
        AppendDocumentLevelNode(imported);
        return;
    }

    if (node.NodeType() == XmlNodeType::XmlDeclaration || node.NodeType() == XmlNodeType::DocumentType) {
        throw XmlException("Cannot write document-level nodes inside an element");
    }

    MarkCurrentElementContentStarted();
    elementStack_.back()->AppendChild(imported);
}

void XmlWriter::WriteNode(XmlReader& reader, bool defattr) {
    EnsureDocumentOpen("write a node");
    EnsureNoOpenAttribute("write a node");

    if (reader.GetReadState() == ReadState::Initial) {
        reader.Read();
    }

    int startDepth = reader.Depth();
    do {
        switch (reader.NodeType()) {
        case XmlNodeType::Element: {
            WriteStartElement(reader.Name());
            if (defattr && reader.MoveToFirstAttribute()) {
                do {
                    WriteAttributeString(reader.Name(), reader.Value());
                } while (reader.MoveToNextAttribute());
                reader.MoveToElement();
            }
            if (reader.IsEmptyElement()) {
                WriteEndElement();
            }
            break;
        }
        case XmlNodeType::Text:
            WriteString(reader.Value());
            break;
        case XmlNodeType::CDATA:
            WriteCData(reader.Value());
            break;
        case XmlNodeType::Comment:
            WriteComment(reader.Value());
            break;
        case XmlNodeType::ProcessingInstruction:
            WriteProcessingInstruction(reader.Name(), reader.Value());
            break;
        case XmlNodeType::Whitespace:
            WriteWhitespace(reader.Value());
            break;
        case XmlNodeType::SignificantWhitespace:
            WriteSignificantWhitespace(reader.Value());
            break;
        case XmlNodeType::EntityReference:
            WriteEntityRef(reader.Name());
            break;
        case XmlNodeType::EndElement:
            WriteFullEndElement();
            break;
        case XmlNodeType::XmlDeclaration: {
            std::string version = reader.GetAttribute("version");
            std::string encoding = reader.GetAttribute("encoding");
            std::string standalone = reader.GetAttribute("standalone");
            WriteStartDocument(
                version.empty() ? "1.0" : version,
                encoding,
                standalone);
            break;
        }
        case XmlNodeType::DocumentType:
            // DocumentType cannot be written via WriteNode from reader inside elements
            break;
        default:
            break;
        }
    } while (reader.Read() && reader.Depth() > startDepth);

    // If we stopped at the EndElement that matches startDepth, close the element
    if (reader.NodeType() == XmlNodeType::EndElement && reader.Depth() == startDepth) {
        WriteFullEndElement();
        reader.Read(); // advance past the end element
    }
}

void XmlWriter::AppendDocumentLevelNode(const std::shared_ptr<XmlNode>& node) {
    if (node == nullptr) {
        return;
    }

    if (node->NodeType() == XmlNodeType::DocumentFragment) {
        for (const auto& child : node->ChildNodes()) {
            if (child != nullptr) {
                AppendDocumentLevelNode(document_.ImportNode(*child, true));
            }
        }
        return;
    }

    if (settings_.Conformance == ConformanceLevel::Fragment) {
        if (node->NodeType() == XmlNodeType::XmlDeclaration) {
            throw XmlException("XML declaration is only allowed in document conformance mode");
        }
        if (node->NodeType() == XmlNodeType::DocumentType) {
            throw XmlException("DOCTYPE is only allowed in document conformance mode");
        }
        fragmentRoot_->AppendChild(node);
        return;
    }

    if (node->NodeType() == XmlNodeType::XmlDeclaration && !document_.ChildNodes().empty()) {
        throw XmlException("XML declaration must be the first node in the document");
    }

    if (node->NodeType() == XmlNodeType::DocumentType && document_.DocumentElement() != nullptr) {
        throw XmlException("DOCTYPE must appear before the root element");
    }

    document_.AppendChild(node);
}

void XmlWriter::WriteEndElement() {
    EnsureNoOpenAttribute("close an element");
    EnsureOpenElement("close an element");

    if (UsesDirectOutput()) {
        auto& current = directElementStack_.back();
        if (current.startTagOpen && !current.forceFullEnd) {
            *directOutputStream_ << "/>";
            directElementStack_.pop_back();
            return;
        }

        if (current.startTagOpen) {
            *directOutputStream_ << '>';
            current.startTagOpen = false;
        }
        if (current.hasNonTextChild && !current.hasTextLikeChild && settings_.Indent) {
            *directOutputStream_ << settings_.NewLineChars;
            std::string indentation;
            AppendIndent(indentation, static_cast<int>(directElementStack_.size()) - 1, settings_);
            *directOutputStream_ << indentation;
        }
        *directOutputStream_ << "</" << current.name << ">";
        directElementStack_.pop_back();
        return;
    }

    elementStack_.pop_back();
    startTagOpenStack_.pop_back();
}

void XmlWriter::WriteFullEndElement() {
    EnsureNoOpenAttribute("close an element");
    EnsureOpenElement("close an element");

    if (UsesDirectOutput()) {
        directElementStack_.back().forceFullEnd = true;
        WriteEndElement();
        return;
    }

    elementStack_.back()->writeFullEndElement_ = true;
    elementStack_.pop_back();
    startTagOpenStack_.pop_back();
}

void XmlWriter::WriteEndDocument() {
    EnsureDocumentOpen("end the document");
    EnsureNoOpenAttribute("end the document");
    while (UsesDirectOutput() ? !directElementStack_.empty() : !elementStack_.empty()) {
        WriteEndElement();
    }
    documentClosed_ = true;
    if (UsesDirectOutput()) {
        directOutputStream_->flush();
    }
}

void XmlWriter::Flush() const {
    EnsureDocumentOpen("flush the writer");
    EnsureNoOpenAttribute("flush the writer");
    if (UsesDirectOutput()) {
        directOutputStream_->flush();
    }
}

void XmlWriter::Close() {
    EnsureDocumentOpen("close the writer");
    EnsureNoOpenAttribute("close the writer");
    WriteEndDocument();
}

std::string XmlWriter::GetString() const {
    EnsureOutputReady();
    if (UsesDirectOutput()) {
        const auto* stringStream = dynamic_cast<const std::ostringstream*>(directOutputStream_);
        if (stringStream == nullptr) {
            throw XmlException("GetString is only available for in-memory writers");
        }
        return stringStream->str();
    }
    return settings_.Conformance == ConformanceLevel::Fragment
        ? fragmentRoot_->InnerXml(settings_)
        : document_.ToString(settings_);
}

void XmlWriter::Save(std::ostream& stream) const {
    EnsureOutputReady();
    if (UsesDirectOutput()) {
        if (directOutputStream_ != &stream) {
            throw XmlException("Save is not available for stream-backed writers bound to a different output stream");
        }
        directOutputStream_->flush();
        return;
    }
    if (settings_.Conformance == ConformanceLevel::Fragment) {
        WriteToStream(*fragmentRoot_, stream, settings_);
        return;
    }
    WriteToStream(document_, stream, settings_);
}

void XmlWriter::Save(const std::string& path) const {
    EnsureOutputReady();
    if (UsesDirectOutput()) {
        throw XmlException("Save(path) is not available for stream-backed writers");
    }
    std::ofstream stream(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw XmlException("Failed to write XML file: " + path);
    }
    Save(stream);
}

std::string XmlWriter::WriteToString(const XmlNode& node, const XmlWriterSettings& settings) {
    std::string output;
    SerializeNode(node, settings, output, 0);
    return output;
}

void XmlWriter::WriteToStream(const XmlNode& node, std::ostream& stream, const XmlWriterSettings& settings) {
    SerializeNodeToStream(node, settings, stream, 0);
}

void XmlWriter::WriteToFile(const XmlNode& node, const std::string& path, const XmlWriterSettings& settings) {
    std::ofstream stream(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw XmlException("Failed to write XML file: " + path);
    }

    WriteToStream(node, stream, settings);
}

// ============================================================================
// XmlConvert
// ============================================================================


}  // namespace System::Xml
