#include "System/Xml/Xml.h"

#include <iostream>
#include <string>

using namespace System::Xml;

int main() {
    try {
        XmlSchemaSet schemas;
        schemas.AddXml(
            "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:tns=\"urn:restriction-legality\" targetNamespace=\"urn:restriction-legality\">"
            "  <xs:complexType name=\"BaseRecord\">"
            "    <xs:sequence>"
            "      <xs:element name=\"id\" type=\"xs:int\"/>"
            "      <xs:element name=\"summary\" type=\"xs:string\" minOccurs=\"0\"/>"
            "    </xs:sequence>"
            "  </xs:complexType>"
            "  <xs:complexType name=\"InvalidRecord\">"
            "    <xs:complexContent>"
            "      <xs:restriction base=\"tns:BaseRecord\">"
            "        <xs:sequence>"
            "          <xs:element name=\"id\" type=\"xs:int\" minOccurs=\"0\"/>"
            "          <xs:element name=\"summary\" type=\"xs:string\" minOccurs=\"0\"/>"
            "        </xs:sequence>"
            "      </xs:restriction>"
            "    </xs:complexContent>"
            "  </xs:complexType>"
            "</xs:schema>");
        std::cout << "NO_EXCEPTION\n";
    } catch (const XmlException& exception) {
        std::cout << exception.what() << "\n";
    }
    return 0;
}