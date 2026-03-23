#pragma once

#include <string>

namespace System::Xml {

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

}  // namespace System::Xml
