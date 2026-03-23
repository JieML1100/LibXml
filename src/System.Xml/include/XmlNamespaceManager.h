#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace System::Xml {

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

}  // namespace System::Xml
