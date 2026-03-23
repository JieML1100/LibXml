#include "XmlInternal.h"

namespace System::Xml {

std::string XmlResolver::ResolveUri(const std::string& baseUri, const std::string& relativeUri) const {
    if (relativeUri.empty()) {
        return baseUri;
    }
    if (baseUri.empty()) {
        return relativeUri;
    }
    return (std::filesystem::path(baseUri).parent_path() / std::filesystem::path(relativeUri)).lexically_normal().string();
}

std::string XmlResolver::GetEntity(const std::string& absoluteUri) const {
    std::ifstream stream(std::filesystem::path(absoluteUri), std::ios::binary);
    if (!stream) {
        throw XmlException("Failed to resolve XML entity: " + absoluteUri);
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string XmlUrlResolver::ResolveUri(const std::string& baseUri, const std::string& relativeUri) const {
    return XmlResolver::ResolveUri(baseUri, relativeUri);
}

std::string XmlUrlResolver::GetEntity(const std::string& absoluteUri) const {
    return XmlResolver::GetEntity(absoluteUri);
}

}  // namespace System::Xml
