# LibXML

这是一个面向 Windows 和 Visual Studio 的 C++ XML 类库，目标是尽量靠近 C# 的 System.Xml 结构和使用方式。

构建环境：

- Visual Studio 2022 或 MSBuild v143 工具链
- Windows x64
- C++20

快速构建：

```powershell
msbuild LibXML.sln /p:Configuration=Debug /p:Platform=x64
```

测试入口：

- `build\System.Xml.Tests\Debug\System.Xml.Tests.exe`
- `build\System.Xml.Tests\Debug\System.Xml.Tests.exe --xmlconf xmlconf\xmlconf.xml`

示例入口：

- `build\System.Xml.QuickStart\Debug\System.Xml.QuickStart.exe`

规范状态：

- XML 1.0 第五版主线能力已完成，当前默认按第五版语义收口名称字符、XML declaration、注释 / PI、命名空间声明与 DTD 主要校验路径
- xmlconf 入口默认以 XML 1.0 第五版为目标，可通过 `--xml10-edition 5` 显式指定，或用 `--xml10-edition all` 跑全部 XML 1.0 edition 过滤

当前实现包含：

- XmlDocument、XmlDocumentFragment、XmlElement、XmlAttribute、XmlText、XmlEntityReference、XmlWhitespace、XmlSignificantWhitespace、XmlComment、XmlCDataSection
- XmlDeclaration、XmlDocumentType、XmlProcessingInstruction
- XmlReader 前向流式读取游标接口
- XmlNodeReader DOM 节点读取游标接口
- XmlWriter 实例式写出接口
- XmlWriter 写入状态校验与 WriteEndDocument 收口
- XmlNamespaceManager 命名空间作用域管理
- NamespaceURI 解析与 xmlns 作用域传播
- 文档空白保留、PI 过滤、写出换行处理配置
- DOM 风格节点树构建与遍历
- XmlNodeList / XmlAttributeCollection 集合视图
- 节点插入、替换、移除、兄弟节点导航与 DocumentFragment 展开插入
- 统一清空入口 `XmlNode::RemoveAll`，元素会同时清除属性与子节点
- 文档间节点导入 `XmlDocument::ImportNode`
- 节点复制 `XmlNode::CloneNode`
- 通用节点工厂 `XmlDocument::CreateNode`
- 实体引用节点 `XmlEntityReference` 与 `XmlDocument::CreateEntityReference`
- QName 前缀 / 本地名拆分辅助
- GetElementsByTagName 递归查询
- XPath 子集查询，支持绝对路径、相对路径、`//` 后代轴、`*` 通配、`[@attr]`、`[@attr='value']`、`[child='value']`、`[child/grand='value']`、`[text()]`、`[text()='value']`、`[n]`、`[last()]`、`[position()=n]`、`[position()>n]`、`[last()-1=position()]`、`text()`、`true()`、`false()`、`lang()`、`boolean()`、`number()`、`string()`、`concat()`、`sum()`、`translate()`、`floor()`、`ceiling()`、`round()`，以及谓词中的 `+`、`-`、`*`、`div`、`mod` 数值表达式与基于 `XmlNamespaceManager` 的前缀解析
- XPath 公开求值入口：`XmlNode::Evaluate(...)`、`XPathNavigator::Evaluate(...)`、`XPathExpression::Compile(...)`
- XML 1.0 第五版名称字符、PI target、字符引用、注释 / CDATA、XML declaration 与命名空间声明约束
- DTD 声明与校验：`ELEMENT`、`ATTLIST`、`ENTITY`、`NOTATION`、外部子集、external parameter entity、默认属性物化、元素内容模型、`ID/IDREF`、`ENTITY/ENTITIES`、`NMTOKEN/NMTOKENS`、枚举与 `xml:space`
- 外部实体解析与 `XmlResolver` 语义，覆盖 Reader / DOM settings 路径
- XML 文档解析与生成
- 实体解码与转义
- 文件加载与保存
- Visual Studio `.sln` / `.vcxproj` 工程组织

当前未覆盖：

- XML 1.1、XSLT、完整 XPath 2.x / 3.x 能力与更高阶验证器
- 更完整的命名空间一致性和高级 API
- 更低内存、支持更完整规范细节的 XmlReader/XmlWriter 管线
- 全量 xmlconf 一致性收口与少量 `.NET System.Xml` 兼容细节

快速上手

```cpp
#include "System/Xml/Xml.h"

using namespace System::Xml;

const auto document = XmlDocument::Parse(
	"<root><item id=\"1\">alpha</item><item id=\"2\">beta</item></root>");

const auto item = document->SelectSingleNode("/root/item[@id='2']");
const auto items = document->SelectNodes("//item");

XmlNamespaceManager namespaces;
namespaces.AddNamespace("bk", "urn:books");

const auto namespaced = XmlDocument::Parse(
	"<lib:catalog xmlns:lib=\"urn:books\"><lib:book/></lib:catalog>");
const auto book = namespaced->SelectSingleNode("/bk:catalog/bk:book", namespaces);

XmlWriter writer(XmlWriterSettings{true, false, "  ", "\n", XmlNewLineHandling::None});
writer.WriteStartDocument("1.0", "utf-8", {});
writer.WriteStartElement("report");
writer.WriteElementString("count", std::to_string(items.Count()));
writer.WriteEndDocument();
```

主要 API 分层

- `XmlDocument`：装载、保存、DOM 创建、文档级 XPath 查询
- `XmlDocument`：装载、保存、DOM 创建、文档级 XPath 查询，以及跨文档 `ImportNode`
- `XmlDocument`：装载、保存、DOM 创建、文档级 XPath 查询，以及 `CreateNode` / `ImportNode`
- `XmlNode`：支持 `CloneNode` 深浅复制，保留当前 DOM 节点类型语义
- `XmlElement`：属性访问、`HasAttributes`、`SetAttributeNode`、子节点编辑、元素范围 XPath 查询
- `XmlAttribute`：支持通过 `OwnerElement` 反查所属元素
- `XmlNode`：支持 `RemoveAll` 统一清空节点内容，元素节点会同时清空属性与子节点
- `XmlDocumentFragment`：用于批量拼装节点、通过 `SetInnerXml` 装载片段并一次性插入 DOM
- `XmlEntityReference`：实体引用节点，当前支持显式创建与预定义 XML 实体的 `InnerText`
- `XmlWhitespace` / `XmlSignificantWhitespace`：DOM 空白节点，可由 `PreserveWhitespace` 装载或通过工厂方法显式创建
- `XmlReader`：前向只读流式游标
- `XmlNodeReader`：基于现有 DOM 节点的只读游标
- `XmlWriter`：带状态校验的顺序写出器，当前已覆盖 `WriteStartAttribute` / `WriteEndAttribute` / `WriteAttributes` / `WriteValue` / `WriteName` / `WriteQualifiedName` / `Flush` / `Close`
- `XmlNamespaceManager`：命名空间作用域表

路径装载语义

- `XmlDocument::Load(path)` 复用 file-backed `XmlReader` 路径：打开文件并通过 reader 构建 DOM，不再固定走整文件 one-shot materialization。
- `XmlReader::CreateFromFile(path)` 保持文件流语义：打开文件并通过 stream-backed reader 读取，不隐藏整文件 materialization。
- `XmlReader::Create(stream)` 与 `XmlReader::CreateFromFile(path)` 都属于流式 reader 路径；`XmlReader::Create(xml)` 属于内存文本路径。
- 因为入口语义不同，`XmlDocument::Load(path)` 与 `XmlReader::CreateFromFile(path)` 的 benchmark 数字不能直接当成同一种成本模型比较。

当前支持的 XPath 子集

- 绝对路径：`/root/group/item`
- 相对路径：`group/item`
- 后代轴：`//item`
- 显式轴：`child::`、`attribute::`、`self::`、`parent::`、`descendant::`、`descendant-or-self::`、`ancestor::`、`ancestor-or-self::`、`following-sibling::`、`preceding-sibling::`、`following::`、`preceding::`、`namespace::`
- 通配符：`*` 与 `@*`
- 节点类型测试：`node()`、`text()`、`comment()`、`processing-instruction()`、`processing-instruction('target')`
- 属性存在过滤：`item[@id]`
- 属性过滤：`item[@id='2']`
- 文本存在过滤：`item[text()]`
- 文本过滤：`item[text()='beta']`
- 子元素文本过滤：`book[title='Two']`
- 子路径文本过滤：`book[meta/title='Two']`
- 位置过滤：`item[2]`
- 位置函数：`item[position()=2]`、`item[position()>1]`
- 末项函数：`item[last()]`、`item[last()-1=position()]`
- 多谓词链：`item[@id][2]`
- 属性节点选择：`/root/@id`
- 文本节点选择：`/root/item/text()`
- 命名空间前缀解析：`SelectNodes("//bk:title", namespaces)`

XPath 函数矩阵

已支持：

- 布尔与位置：`not()`、`true()`、`false()`、`lang()`、`boolean()`、`last()`、`position()`，其中 `position()` / `last()` 可参与比较与算术表达式
- 字符串与名称：`string()`、`concat()`、`translate()`、`contains()`、`starts-with()`、`normalize-space()`、`string-length()`、`substring()`、`substring-before()`、`substring-after()`、`name()`、`local-name()`、`namespace-uri()`
- 数字与计数：`number()`、`count()`、`sum()`、`floor()`、`ceiling()`、`round()`，以及谓词中的 `+`、`-`、`*`、`div`、`mod`

已支持的比较语义：

- 节点集与字符串表达式比较，例如 `code = concat(substring(title, 1, 1), '-', string(@id))`
- 节点集与数字表达式比较，例如 `@price = number(concat('2', '0'))`
- 简单布尔上下文，例如 `boolean(@id)`、`string(title)`、`boolean(number(@price))`、`true()`、`false()`、`lang('en')`
- 算术表达式参与比较，例如 `number(@price) + 10 = 30`、`number(@price) div 10 = 2`
- 位置表达式参与比较，例如 `position()>1`、`last()-1=position()`
- 函数与谓词参数支持当前节点 `.`、终结属性路径和终结 `text()` 路径，例如 `count(.)`、`meta/@id`、`meta/title/text()`
- 上述语义与命名空间查询组合使用

暂未支持：

- 完整 XPath 1.0 全覆盖、少量低频边角语义，以及更高版本 XPath 能力

DTD 支持边界

当前支持：

- `DOCTYPE` 的 `PUBLIC` / `SYSTEM` 头
- 内部 / 外部子集中的 `ENTITY`、`NOTATION`、`ELEMENT`、`ATTLIST` 声明
- external parameter entity，以及外部 DTD 路径上的 `INCLUDE` / `IGNORE` conditional section
- 默认属性物化、`EMPTY` / 元素内容模型校验、`ID` / `IDREF(S)` / `ENTITY(IES)` / `NMTOKEN(S)` / `NOTATION` 约束
- 以上声明与校验在 DOM / Writer / Reader 中的可见性、保留与主要 round-trip 路径

限制说明

- 当前 XPath 仍是以 XPath 1.0 常用能力为主的子集实现，已覆盖常见轴 / 谓词 / 函数与公开 `Evaluate` / `Compile` 入口，但尚未完全复刻 .NET 行为
- 命名空间感知 XPath 需要显式传入 `XmlNamespaceManager`；未传入时，带前缀名称按字面 QName / 字面前缀匹配，不再退化为按 `LocalName` 或任意前缀通配的宽松匹配
- XmlReader / XmlWriter / DTD 验证已覆盖 XML 1.0 第五版常见 DOM 与顺序读写场景，但还没有完全复刻 .NET `System.Xml`

建议后续扩展方向：

- 补齐剩余 XPath 低频函数、边角语义与公开 API 兼容细节
- 继续补齐 `System.Xml` 兼容面与异常细节
- 增加基准测试和更系统的单元测试