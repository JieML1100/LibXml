#pragma once

#include <string>

namespace System::Xml {

class XmlConvert final {
public:
    XmlConvert() = delete;

    static std::string EncodeName(const std::string& name);
    static std::string DecodeName(const std::string& name);
    static std::string EncodeLocalName(const std::string& name);
    static std::string EncodeNmToken(const std::string& name);
    static bool IsXmlChar(char ch);
    static bool IsWhitespaceChar(char ch);
    static bool IsStartNameChar(char ch);
    static bool IsNameChar(char ch);
    static bool IsNCNameStartChar(char ch);
    static bool IsNCNameChar(char ch);
    static std::string VerifyName(const std::string& name);
    static std::string VerifyNCName(const std::string& name);
    static std::string VerifyNmToken(const std::string& name);
    static std::string VerifyXmlChars(const std::string& content);

    static std::string ToString(bool value);
    static std::string ToString(int value);
    static std::string ToString(long long value);
    static std::string ToString(double value);
    static std::string ToString(float value);

    static bool ToBoolean(const std::string& value);
    static int ToInt32(const std::string& value);
    static long long ToInt64(const std::string& value);
    static double ToDouble(const std::string& value);
    static float ToSingle(const std::string& value);
};

}  // namespace System::Xml
