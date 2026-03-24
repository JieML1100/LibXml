#include "XmlUtilityInternal.h"

namespace System::Xml {

std::string XmlConvert::EncodeName(std::string_view name) {
    if (name.empty()) {
        return {};
    }

    std::string encoded;
    encoded.reserve(name.size());

    for (std::size_t index = 0; index < name.size(); ++index) {
        const char ch = name[index];
        if (index == 0 && !IsStartNameChar(ch)) {
            std::ostringstream hex;
            hex << "_x" << std::uppercase << std::setfill('0') << std::setw(4)
                << std::hex << static_cast<unsigned>(static_cast<unsigned char>(ch)) << "_";
            encoded += hex.str();
        } else if (!IsNameChar(ch)) {
            std::ostringstream hex;
            hex << "_x" << std::uppercase << std::setfill('0') << std::setw(4)
                << std::hex << static_cast<unsigned>(static_cast<unsigned char>(ch)) << "_";
            encoded += hex.str();
        } else {
            encoded.push_back(ch);
        }
    }

    return encoded;
}

std::string XmlConvert::DecodeName(std::string_view name) {
    if (name.empty()) {
        return {};
    }

    std::string decoded;
    decoded.reserve(name.size());

    for (std::size_t index = 0; index < name.size(); ++index) {
        if (index + 6 < name.size() && name[index] == '_' && name[index + 1] == 'x'
            && name[index + 6] == '_') {
            const auto hex = name.substr(index + 2, 4);
            bool validHex = hex.size() == 4;
            for (char h : hex) {
                if (!std::isxdigit(static_cast<unsigned char>(h))) {
                    validHex = false;
                    break;
                }
            }
            if (validHex) {
                unsigned int charCode = 0;
                for (char h : hex) {
                    charCode <<= 4;
                    if (h >= '0' && h <= '9') {
                        charCode |= static_cast<unsigned int>(h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        charCode |= static_cast<unsigned int>(10 + h - 'a');
                    } else if (h >= 'A' && h <= 'F') {
                        charCode |= static_cast<unsigned int>(10 + h - 'A');
                    }
                }
                decoded.push_back(static_cast<char>(charCode));
                index += 6;
                continue;
            }
        }
        decoded.push_back(name[index]);
    }

    return decoded;
}

std::string XmlConvert::EncodeLocalName(std::string_view name) {
    if (name.empty()) {
        return {};
    }

    std::string encoded;
    encoded.reserve(name.size());

    for (std::size_t index = 0; index < name.size(); ++index) {
        const char ch = name[index];
        if (ch == ':') {
            encoded += "_x003A_";
        } else if (index == 0 && !IsStartNameChar(ch)) {
            std::ostringstream hex;
            hex << "_x" << std::uppercase << std::setfill('0') << std::setw(4)
                << std::hex << static_cast<unsigned>(static_cast<unsigned char>(ch)) << "_";
            encoded += hex.str();
        } else if (!IsNameChar(ch)) {
            std::ostringstream hex;
            hex << "_x" << std::uppercase << std::setfill('0') << std::setw(4)
                << std::hex << static_cast<unsigned>(static_cast<unsigned char>(ch)) << "_";
            encoded += hex.str();
        } else {
            encoded.push_back(ch);
        }
    }

    return encoded;
}

std::string XmlConvert::EncodeNmToken(std::string_view name) {
    if (name.empty()) {
        return {};
    }

    std::string encoded;
    encoded.reserve(name.size());

    for (const char ch : name) {
        if (!IsNameChar(ch)) {
            std::ostringstream hex;
            hex << "_x" << std::uppercase << std::setfill('0') << std::setw(4)
                << std::hex << static_cast<unsigned>(static_cast<unsigned char>(ch)) << "_";
            encoded += hex.str();
        } else {
            encoded.push_back(ch);
        }
    }

    return encoded;
}

bool XmlConvert::IsXmlChar(char ch) {
    const auto uch = static_cast<unsigned char>(ch);
    return uch == 0x09 || uch == 0x0A || uch == 0x0D || uch >= 0x20;
}

bool XmlConvert::IsStartNameChar(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == ':';
}

bool XmlConvert::IsNCNameStartChar(char ch) {
    // NCName start char = letter or '_' (no colon)
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool XmlConvert::IsNameChar(char ch) {
    return IsStartNameChar(ch) || std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '-' || ch == '.';
}

std::string XmlConvert::VerifyName(std::string_view name) {
    if (name.empty()) {
        throw XmlException("The empty string is not a valid XML name");
    }

    if (!IsStartNameChar(name[0])) {
        throw XmlException("'" + std::string(name) + "' is not a valid XML name");
    }

    for (std::size_t index = 1; index < name.size(); ++index) {
        if (!IsNameChar(name[index])) {
            throw XmlException("'" + std::string(name) + "' is not a valid XML name");
        }
    }

    return std::string(name);
}

bool XmlConvert::IsNCNameChar(char ch) {
    // NCNameChar = NameChar minus ':'
    return IsNameChar(ch) && ch != ':';
}

bool XmlConvert::IsWhitespaceChar(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

std::string XmlConvert::VerifyNCName(std::string_view name) {
    if (name.empty()) {
        throw XmlException("The empty string is not a valid NCName");
    }
    // First char: letter or underscore (no colon, no digit)
    const char first = name[0];
    if (!std::isalpha(static_cast<unsigned char>(first)) && first != '_') {
        throw XmlException("'" + std::string(name) + "' is not a valid NCName");
    }
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (!IsNCNameChar(name[i])) {
            throw XmlException("'" + std::string(name) + "' is not a valid NCName");
        }
    }
    return std::string(name);
}

std::string XmlConvert::VerifyNmToken(std::string_view name) {
    if (name.empty()) {
        throw XmlException("The empty string is not a valid NMTOKEN");
    }

    for (const char ch : name) {
        if (!IsNameChar(ch)) {
            throw XmlException("'" + std::string(name) + "' is not a valid NMTOKEN");
        }
    }

    return std::string(name);
}

std::string XmlConvert::VerifyXmlChars(std::string_view content) {
    for (std::size_t index = 0; index < content.size(); ++index) {
        if (!IsXmlChar(content[index])) {
            throw XmlException(
                "Invalid XML character at position " + std::to_string(index));
        }
    }
    return std::string(content);
}

std::string XmlConvert::ToString(bool value) {
    return value ? "true" : "false";
}

std::string XmlConvert::ToString(int value) {
    return std::to_string(value);
}

std::string XmlConvert::ToString(long long value) {
    return std::to_string(value);
}

std::string XmlConvert::ToString(double value) {
    if (std::isnan(value)) {
        return "NaN";
    }
    if (std::isinf(value)) {
        return value > 0 ? "INF" : "-INF";
    }

    std::ostringstream stream;
    stream << std::setprecision(17) << value;
    return stream.str();
}

std::string XmlConvert::ToString(float value) {
    if (std::isnan(value)) {
        return "NaN";
    }
    if (std::isinf(value)) {
        return value > 0 ? "INF" : "-INF";
    }

    std::ostringstream stream;
    stream << std::setprecision(9) << value;
    return stream.str();
}

bool XmlConvert::ToBoolean(std::string_view value) {
    if (value == "true" || value == "1") {
        return true;
    }
    if (value == "false" || value == "0") {
        return false;
    }
    throw XmlException("'" + std::string(value) + "' is not a valid boolean value");
}

int XmlConvert::ToInt32(std::string_view value) {
    const std::string str(value);
    try {
        std::size_t pos = 0;
        const int result = std::stoi(str, &pos);
        if (pos != str.size()) {
            throw XmlException("'" + str + "' is not a valid Int32 value");
        }
        return result;
    } catch (const std::invalid_argument&) {
        throw XmlException("'" + str + "' is not a valid Int32 value");
    } catch (const std::out_of_range&) {
        throw XmlException("'" + str + "' is out of range for Int32");
    }
}

long long XmlConvert::ToInt64(std::string_view value) {
    const std::string str(value);
    try {
        std::size_t pos = 0;
        const long long result = std::stoll(str, &pos);
        if (pos != str.size()) {
            throw XmlException("'" + str + "' is not a valid Int64 value");
        }
        return result;
    } catch (const std::invalid_argument&) {
        throw XmlException("'" + str + "' is not a valid Int64 value");
    } catch (const std::out_of_range&) {
        throw XmlException("'" + str + "' is out of range for Int64");
    }
}

double XmlConvert::ToDouble(std::string_view value) {
    if (value == "NaN") {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (value == "INF" || value == "Infinity") {
        return std::numeric_limits<double>::infinity();
    }
    if (value == "-INF" || value == "-Infinity") {
        return -std::numeric_limits<double>::infinity();
    }
    const std::string str(value);
    try {
        std::size_t pos = 0;
        const double result = std::stod(str, &pos);
        if (pos != str.size()) {
            throw XmlException("'" + str + "' is not a valid Double value");
        }
        return result;
    } catch (const std::invalid_argument&) {
        throw XmlException("'" + str + "' is not a valid Double value");
    } catch (const std::out_of_range&) {
        throw XmlException("'" + str + "' is out of range for Double");
    }
}

float XmlConvert::ToSingle(std::string_view value) {
    if (value == "NaN") {
        return std::numeric_limits<float>::quiet_NaN();
    }
    if (value == "INF" || value == "Infinity") {
        return std::numeric_limits<float>::infinity();
    }
    if (value == "-INF" || value == "-Infinity") {
        return -std::numeric_limits<float>::infinity();
    }
    const std::string str(value);
    try {
        std::size_t pos = 0;
        const float result = std::stof(str, &pos);
        if (pos != str.size()) {
            throw XmlException("'" + str + "' is not a valid Single value");
        }
        return result;
    } catch (const std::invalid_argument&) {
        throw XmlException("'" + str + "' is not a valid Single value");
    } catch (const std::out_of_range&) {
        throw XmlException("'" + str + "' is out of range for Single");
    }
}


}  // namespace System::Xml
