#include "System/Xml/Xml.h"

#include <iostream>
#include <chrono>

using namespace System::Xml;

int main() {
    auto doc = std::make_shared<XmlDocument>();
    auto start1 = std::chrono::steady_clock::now();
    doc->Load(xmlPath);
    auto end1 = std::chrono::steady_clock::now();
    auto elapsed1 = std::chrono::duration<double, std::milli>(end1 - start1).count();
    std::cout << "load: " << elapsed1 << " ms\n";
    std::cout << doc->DocumentElement()->Name() << "\n";
    const auto document = XmlDocument::Parse(
        "<lib:catalog xmlns:lib=\"urn:books\" xmlns:meta=\"urn:meta\">"
        "<lib:book meta:id=\"b1\"><lib:title>XML Fundamentals</lib:title></lib:book>"
        "<lib:book meta:id=\"b2\"><lib:title>Advanced XPath</lib:title></lib:book>"
        "</lib:catalog>");

    XmlNamespaceManager namespaces;
    namespaces.AddNamespace("bk", "urn:books");
    namespaces.AddNamespace("m", "urn:meta");

    const auto firstBook = document->SelectSingleNode("/bk:catalog/bk:book[@m:id='b1']", namespaces);
    const auto allBooks = document->SelectNodes("//bk:book", namespaces);

    std::cout << "Document element: " << document->DocumentElement()->Name() << "\n";
    std::cout << "Book count via XPath: " << allBooks.Count() << "\n";

    if (firstBook != nullptr && firstBook->NodeType() == XmlNodeType::Element) {
        const auto title = static_cast<XmlElement*>(firstBook.get())->SelectSingleNode("bk:title", namespaces);
        std::cout << "First title: " << (title != nullptr ? title->InnerText() : "<missing>") << "\n";
    }

    XmlWriter writer(XmlWriterSettings{true, false, "  ", "\n", XmlNewLineHandling::None});
    writer.WriteStartDocument("1.0", "utf-8", {});
    writer.WriteStartElement("report");
    writer.WriteAttributeString("generated", "true");
    writer.WriteElementString("summary", "QuickStart sample completed");
    writer.WriteEndDocument();

    std::cout << "Generated XML:\n" << writer.GetString() << std::endl;
    return 0;
}