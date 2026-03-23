# LibXML 真流式准备

## 目标

超大型文档支持的前提，不是继续压缩当前 DOM 路径，而是先把 Reader 和 Writer 的“整文/整串物化”依赖拆开。

这份文档记录当前已完成的准备工作，以及后续应如何把 Reader 和 Writer 推到真正流式。

## 当前阻塞

### Reader

当前 `XmlReader` 仍依赖完整输入字符串：

- `XmlReader::Create(const std::string&, ...)` 直接持有完整 `source_`
- `XmlReader::Create(std::istream&, ...)` 仍先把流完整读到字符串
- 大量词法逻辑直接基于 `source_[position_]`、`source_.find(...)`、`source_.substr(...)`

这意味着现在只是“前向读取 API”，还不是真流式实现。

### Writer

当前 `XmlWriter` 的实例式写出接口仍先构造内部 DOM：

- 写入过程通过 `document_` / `fragmentRoot_` 累积节点
- `GetString()` 在最终阶段整串序列化
- 过去的文件输出和 `ostream` 输出也依赖中间完整字符串

这意味着现在只是“顺序式 API”，还不是真正边写边输出的 Writer。

## 本轮已完成的准备

### 1. Reader 热点清理

- 非空元素不再在 `ParseElement()` 中默认预扫子树来构造 `ReadInnerXml` / `ReadOuterXml`
- 元素 markup 改为首次调用时惰性捕获并缓存
- `bufferedNodes_` 改成 `std::deque`，消费头节点不再线性前移

这两步虽然不是“真流式”，但去掉了 Reader 默认路径上的明显浪费。

### 2. Writer 已具备真正的 `ostream` 直写路径

新增/调整：

- `XmlWriter::WriteToStream(const XmlNode&, std::ostream&, settings)`
- `XmlWriter::Save(std::ostream&)`
- `XmlDocument::Save(std::ostream&)` 现在走直写流而不是先构造完整字符串
- `XmlWriter::WriteToFile(...)` 和实例 `Save(path)` 也改为复用流式序列化路径

这一步的意义是：

- 输出端已经有了“无整串中间结果”的基础设施
- 后续若 Writer 内部状态机脱离 DOM，也能直接复用同一输出汇聚点

### 3. Reader 的非流式兜底入口已集中

当前 `istream -> string` 的过渡逻辑已收口到统一的 `ReadAllStream(...)` 帮助函数。

这不是最终解，但它把将来替换成“分块缓冲读取”的切口固定到了单一入口。

### 4. Reader 状态机已收口输入访问边界

当前 `XmlReader` 的核心状态机路径，已经基本不再直接使用：

- `source_[position]`
- `source_.find(...)`
- `source_.substr(...)`

而是统一经由内部 helper：

- `SourceText()`
- `SourceSize()`
- `SourceCharAt(...)`
- `FindInSource(...)`
- `SourceSubstr(...)`

这一步仍然没有引入真正的流式输入，但它把“解析状态机”和“底层字符串存储”之间加上了清晰边界。后续无论是引入 `InputSource` 还是 tokenizer，都不需要再回头扫一遍 Reader 主逻辑去替换裸字符串访问。

### 5. Reader 已切到输入源对象承载字符串输入

当前 `XmlReader` 已不再直接持有裸 `std::string source_` 作为核心输入载体，而是持有只读输入源对象。

本轮先落地的是：

- `XmlReaderInputSource`
- `StringXmlReaderInputSource`

也就是说，现在仍然是“字符串后端”，但 Reader 看到的已经是 source object，而不是具体 string 成员。这一步的价值在于：后续引入 `StreamInputSource` 时，不需要重改 Reader 的 helper 边界和主状态机，只需要补新的 source 实现并逐步替换建构路径。

### 6. Reader 已接入 StreamInputSource 原型

当前 `XmlReader::Create(std::istream&, ...)` 与普通 `CreateFromFile(...)` 路径，已经不再默认走 `ReadAllStream(...)`，而是接到 `StreamXmlReaderInputSource`：

- 按块增量缓冲输入
- `CharAt/Find/Slice` 基于当前缓冲区按需扩展
- 文件路径输入在确认 EOF 后会立即关闭底层文件句柄，只保留已缓冲内容

这意味着 Reader 现在在普通流输入路径上，已经具备“不是构造期整流物化”的基础形态。

当前仍保留两个明确 fallback：

- `ValidationType::Schema`
- `MaxCharactersInDocument != 0`

这两条路径暂时仍回退到整串输入，因为它们当前依赖既有的整文语义与异常时机。这是刻意保留的兼容层，不是回退设计目标。

## 后续 Reader 改造顺序

### 阶段 A：抽出输入源接口

目标：让 `XmlReader` 不再直接依赖 `std::string source_` 这个单一载体。

现在这一步已经具备可实施前提，因为 Reader 主状态机已经优先依赖 `Source*` helper，而不是分散的裸 `source_` 访问。

其中字符串后端也已经先通过 `StringXmlReaderInputSource` 落到了这层抽象上，因此下一步可以直接开始补 `StreamInputSource`，而不是先做一次“string -> source object”的机械迁移。

而现在 `StreamXmlReaderInputSource` 原型也已经落地，所以下一阶段最合理的重点已经从“输入源替换”转到“去掉对整串查找的词法耦合”，也就是 tokenizer 拆分。

建议引入内部抽象：

- `XmlReaderInputSource` 或等价概念
- `StringInputSource`
- `StreamInputSource`

最少要支持：

- `Peek(offset)`
- `Read()`
- `Consume(count)`
- `Slice(start, count)` 或“把当前 token 落地成 string”
- 行列号推进

重点不是一开始支持任意回溯，而是定义清楚：哪些路径必须顺序消费，哪些路径确实需要 lookahead。

### 阶段 B：把词法辅助从 `source_.find/substr` 改成 tokenizer

当前以下能力强依赖完整字符串搜索：

- 查找 `?>`
- 查找 `-->`
- 查找 `]]>`
- 查找 `;`
- 元素边界捕获

后续应拆成：

- `XmlTokenizer`
- 对注释 / CDATA / PI / 引用值 / 文本段的有限状态扫描
- 面向流的最小缓冲区管理

当前已经完成第一阶段落地：

- 引入了 Reader 内部 `XmlReaderTokenizer` 扫描器原型
- `ParseQuotedValue()` 已改走 tokenizer 的引用值扫描
- `ParseProcessingInstruction()` / `ParseComment()` / `ParseCData()` 已改走 tokenizer 的分隔符扫描
- `ParseText()` 的实体引用切分已改走 tokenizer 的实体引用扫描
- `CaptureElementXml()` 的注释 / CDATA / PI / closing-tag 边界捕获已开始复用 tokenizer 扫描
- tokenizer 现已内聚 `ParseNameAt` / `SkipTag` 这类元素 tag 名称与边界跳过能力，`CaptureElementXml()` 不再直接依赖外部 text-view tag helper

这一步还不是完整 tokenizer 分层，因为 Reader 仍然直接做 token 语义解释与部分名称解析，但“扫描边界在哪里结束”已经开始从状态机主体中抽离出来。下一阶段应继续把元素 tag 扫描、声明扫描以及文本段消费进一步下沉到 tokenizer 层。

原则：

1. Tokenizer 负责把跨块 token 拼完整
2. Reader 状态机只消费 token，不直接知道底层来自 string 还是 stream

### 阶段 C：重新定义 `ReadInnerXml` / `ReadOuterXml` 的流式语义

这些 API 对真流式 Reader 天然昂贵。

推荐策略：

- 普通 `Read()` 绝不为它们预付成本
- 调用时基于当前元素事件，临时启动一个子树复制/重放过程
- 文档里明确说明这两个 API 在流式模式下会额外消费与序列化当前子树

### 阶段 D：让 DOM 构建复用 Reader 事件

当 Reader 真流式后，`XmlDocument::Load(std::istream&)` 最合理的路径应是：

- `istream -> XmlReader(tokenizer)`
- `Reader events -> DOM builder`

这样全量 DOM 仍然可能占大内存，但不会再额外保留一整份源码字符串副本。

## 后续 Writer 改造顺序

### 阶段 A：保留现有 DOM-backed Writer，对外补齐流输出边界

这一步已开始完成：Writer/Document/静态序列化都已支持 `ostream` 直写。

### 阶段 B：抽出输出汇聚接口

建议后续引入内部抽象：

- `XmlWriterOutputTarget`
- `StringOutputTarget`
- `OstreamOutputTarget`

这样当前 `GetString()` 和 `Save(std::ostream&)` 就只是两种 target，而不是两套逻辑。

### 阶段 C：把实例 Writer 从 DOM-backed 改成真正状态机输出

当前 `WriteStartElement/WriteString/WriteEndElement` 都是在改内部 DOM。

真正流式 Writer 需要：

- 仅保留元素栈、命名空间栈、属性暂存和状态机
- 在 `WriteString` / `WriteComment` / `WriteEndElement` 时直接写到 output target
- 仅在必要时保留最小状态，而不是保留整个文档树

这一步才是超大型输出场景真正有意义的拐点。

### 阶段 D：把 `WriteNode(XmlReader&)` 变成真正的 Reader->Writer 管道

一旦 Reader 和 Writer 都是流式，`WriteNode(XmlReader&)` 可以成为超大文档处理的核心路径：

- Reader 顺序读
- 业务逻辑按需过滤/变换
- Writer 顺序写

这会是后续“超大文档选择性复制、过滤、投影输出”的基础。

## 推荐实施顺序

1. Reader：先抽输入源接口和 tokenizer，不急着一次改完整个状态机
2. Writer：再抽输出 target，并逐步把实例 Writer 从 DOM-backed 改为 direct-write
3. 最后再把 DOM Load/Save 接到新的 Reader/Writer 管线

## 当前结论

现在最值得继续投入的是：

1. Reader 输入源抽象
2. Tokenizer 分层
3. Writer 输出 target 抽象

其中 Writer 直写 `ostream` 已经就位，下一步的真正重头戏是 Reader。Reader 一旦没有“整文字符串”依赖，超大型文档支持才算真正进入实现阶段。