# LibXML 优化路线图

## 目标

本轮优化分两阶段推进：

1. 先做现有实现的性能优化，在不破坏现有 API 与行为回归的前提下，把常见解析、查询、遍历路径中的明显热点压下去。
2. 再为超大文档支持做结构性准备，重点解决当前“必须整文读入内存、Reader 也会提前构造大块字符串视图”的根限制。

## 代码现状判断

当前库的主要实现几乎集中在 [src/System.Xml/src/Xml.cpp](src/System.Xml/src/Xml.cpp) 中，特点是：

- 功能面很宽，DOM、Reader、Writer、XPath、Schema 验证都在同一个翻译单元内。
- 运行时数据结构大量依赖 `std::string`、`std::vector`、`std::shared_ptr`。
- 已有测试覆盖偏功能一致性与异常语义，几乎没有专门的性能基线或大文档场景基线。
- 现有 `Release|x64` 已启用编译器优化，但没有独立 benchmark 工程和规模化输入数据。

这意味着当前最合理的策略不是直接“到处微调”，而是先建立测量基线，再做一轮低风险热点优化，然后再切到支持大文档的架构调整。

## 已确认的热点与限制

### 1. Reader 并不是真正流式

以下路径都会把完整输入先读入一个大字符串：

- [src/System.Xml/src/Xml.cpp#L6520](src/System.Xml/src/Xml.cpp#L6520)
- [src/System.Xml/src/Xml.cpp#L6531](src/System.Xml/src/Xml.cpp#L6531)
- [src/System.Xml/src/Xml.cpp#L9714](src/System.Xml/src/Xml.cpp#L9714)
- [src/System.Xml/src/Xml.cpp#L9720](src/System.Xml/src/Xml.cpp#L9720)

直接影响：

- 文档大小至少会在输入缓冲上完整占一份内存。
- `XmlReader::Create(std::istream&)` 语义上接受流，但实现上仍是整流加载，不适合超大文档。
- DOM 加载和 Reader 加载目前共享了同一类“全量字符串源”的限制。

### 2. Reader 为了 ReadInnerXml/ReadOuterXml 预览，解析元素时会向前扫描整段子树

在 [src/System.Xml/src/Xml.cpp#L9543](src/System.Xml/src/Xml.cpp#L9543) 的 `XmlReader::ParseElement()` 中，每次遇到非空元素都会调用 `CaptureElementXml(...)`；而 `CaptureElementXml(...)` 位于 [src/System.Xml/src/Xml.cpp#L9421](src/System.Xml/src/Xml.cpp#L9421)，它会从当前位置继续向前扫描，直到配对闭合标签为止。

直接影响：

- 普通 `Read()` 即使调用方根本不需要 `ReadInnerXml()` / `ReadOuterXml()`，也要先支付一次子树预扫描成本。
- 深层嵌套文档下容易退化成接近 $O(n^2)$ 的扫描模式。
- 同时会构造 `innerXml` / `outerXml` 字符串副本，放大分配和拷贝成本。

这是当前最优先的 Reader 性能热点，也是超大文档支持前必须先处理的结构问题。

### 3. Reader 缓冲队列前端弹出是线性复杂度

`TryConsumeBufferedNode()` 在 [src/System.Xml/src/Xml.cpp#L8993](src/System.Xml/src/Xml.cpp#L8993) 通过：

- 读取 `bufferedNodes_.front()`
- 再 `erase(bufferedNodes_.begin())`

这在实体展开或一次排入多个事件时会触发前移拷贝，复杂度是 $O(n)$。

### 4. DOM 上存在较多线性扫描型基础操作

典型例子：

- `PreviousSibling()` / `NextSibling()` 通过遍历父节点子数组查找自己，见 [src/System.Xml/src/Xml.cpp#L5470](src/System.Xml/src/Xml.cpp#L5470)
- 属性查找 `GetAttributeNode(...)` 通过 `find_if` 线性查找，见 [src/System.Xml/src/Xml.cpp#L6312](src/System.Xml/src/Xml.cpp#L6312)
- `RemoveAttributeNode(...)` 也是线性移除，见 [src/System.Xml/src/Xml.cpp#L6443](src/System.Xml/src/Xml.cpp#L6443)

这些操作对小文档影响有限，但在大量兄弟节点、大量属性、频繁 XPath/遍历组合访问时会形成明显累计成本。

### 5. XPath 路径每次都重新解析，并构造大量临时向量

核心路径：

- `ParseXPathSteps(...)` 见 [src/System.Xml/src/Xml.cpp#L3465](src/System.Xml/src/Xml.cpp#L3465)
- `ApplyXPathStep(...)` 见 [src/System.Xml/src/Xml.cpp#L3735](src/System.Xml/src/Xml.cpp#L3735)
- `EvaluateXPathFromDocument(...)` 见 [src/System.Xml/src/Xml.cpp#L3973](src/System.Xml/src/Xml.cpp#L3973)

直接表现：

- 每次 `SelectNodes` / `SelectSingleNode` 都重新解析 XPath 文本。
- 多个轴实现会重复收集候选节点到新向量，再做过滤和去重。
- `preceding` / `following` 这类轴会扫描整段文档顺序并构造中间集合。
- 联合运算 `|` 还会对每个子表达式重新走一遍解析与去重。

这块在查询密集场景里很容易成为第二梯队热点。

### 6. 共享所有权和字符串复制偏重，DOM 内存密度不高

当前 DOM 节点、属性、集合广泛使用 `std::shared_ptr`，并频繁复制字符串结果，例如：

- `InnerText()` 递归拼接字符串，见 [src/System.Xml/src/Xml.cpp#L5792](src/System.Xml/src/Xml.cpp#L5792)
- `InnerXml()` / `OuterXml()` 每次重新序列化，见 [src/System.Xml/src/Xml.cpp#L5837](src/System.Xml/src/Xml.cpp#L5837)

这对常规场景是可接受的，但对超大 DOM 来说，节点对象头、引用计数、离散分配和重复字符串构造都会放大内存占用和 cache miss。

## 优化总原则

1. 先测量，再优化。没有 benchmark 的优化很容易把代码复杂度抬高，但没有稳定收益。
2. 先处理“默认路径上的结构性浪费”，尤其是 Reader 预扫描和整流加载。
3. 优先不破坏现有 API、测试和行为一致性；必要时新增能力，而不是直接改变旧语义。
4. 性能优化与超大文档支持要解耦：前者先做低风险提速，后者接受更明确的架构调整。

## 阶段规划

## 阶段 0：建立性能基线

目标：回答“慢在哪、慢多少、优化后有没有回退”。

建议新增一个独立 benchmark 可执行项目，而不是把性能测试塞进当前单元测试主程序。

首批基线场景：

1. DOM 解析小文档、中等文档、大文档
2. `XmlReader` 顺序读取同一批文档
3. 高频 XPath 查询
4. 大量属性元素构建与属性访问
5. 大量兄弟节点上的插入、替换、兄弟导航

基线指标：

- 总耗时
- 每次操作平均耗时
- 峰值 RSS 或进程工作集
- 输入大小与耗时增长曲线

阶段产物：

- benchmark 工程
- 固定样本 XML 数据集
- 记录 Debug/Release 差异的基线结果

## 阶段 1：低风险性能优化

目标：不改 public API，优先消掉明显的默认路径浪费。

### 当前阶段状态

阶段 1 里与 Reader subtree replay 直接相关的工作，当前已经收敛到一组可保留状态：

1. `ReadSubtree()` 对 `<= 20 MiB` 的子树优先走 string materialization fast path。
2. 只有共享 subrange 路径才启用 replay，避免把 replay 成本带到普通小子树上。
3. replay 临时文件保持 `256 KiB` 持久 stream buffer，搜索块大小保持 `64 KiB`。
4. `ReadElementContentAsString()` / `ReadElementString()` 对 simple-content element 增加 direct-consume fast path，直接聚合文本并消费匹配 end tag，不再走“聚合后再 Skip 一遍子树”的失败方案。
5. `ParseText()` 已改成单次扫描同时处理 `\n`、`<`、`&`，并同步推进 line/column；replay 搜索也为 `"\n<&"` 增加了热分支。

这组改动已经过 Release tests 和大子树 replay benchmark 回归验证，现阶段应视为新的稳定基线，而不是继续反复试验的区域。

同时，本轮也明确排除了两类不可保留方案：

1. “先聚合 simple content，再调用 `Skip()` 消费子树”的 fast path。这个版本会把已经省下的扫描成本重新付回去，segmented 场景明显回退。
2. 任何为了维护 line/column 而对 stream 输入做无界前瞻扫描的 helper。它会破坏 `XmlReader::Create(std::istream&)` 的按需缓冲语义，并触发 declaration 读取后的整流缓冲。

后续阶段 1 的 Reader 工作，应优先集中在剩余的节点推进成本，而不是再扰动上述稳定项。

### 1. Reader 改为惰性生成 InnerXml/OuterXml

优先级：P0

建议：

- `ParseElement()` 默认不要立即调用 `CaptureElementXml(...)`
- 仅在调用 `ReadInnerXml()` / `ReadOuterXml()` 时，再按当前位置和元素边界进行惰性捕获
- 若必须缓存，也只缓存边界位置，而不是提前复制完整字符串

预期收益：

- 顺序 `Read()` 的常见路径显著降本
- 深嵌套文档的 Reader 扫描复杂度下降
- 为超大文档版本的 Reader 打基础

### 2. Reader 缓冲队列改成真正的队列结构

优先级：P0

建议：

- 将 `bufferedNodes_` 从“vector + erase(begin)”改为 `std::deque` 或“vector + 头索引”

预期收益：

- 实体展开、多事件排队场景去掉无谓前移成本

### 3. 减少 XPath 执行期的中间分配

优先级：P1

建议：

- 为解析后的 XPath steps 引入编译结果对象，支持复用
- 将常见候选集合改成“复用输出缓冲区”而不是多层临时 vector
- 审视 `preceding/following` 等轴的全量收集策略，改为更接近迭代式过滤

预期收益：

- 查询密集场景下耗时下降
- 降低临时内存分配

### 4. 为 DOM 热点操作加轻量索引或更好的局部结构

优先级：P1

建议：

- 兄弟导航可考虑引入前后兄弟弱链接，或缓存最近索引
- 属性集合可先做“小规模保留 vector，大规模再建 name index”的混合策略
- 高频 `GetAttribute(name)` / `HasAttribute(name)` 场景避免反复线性扫描

预期收益：

- 大属性表、大兄弟列表场景更稳
- 不必立即把所有结构改成哈希表，避免小对象膨胀

### 5. 避免非必要的字符串复制

优先级：P1

建议：

- 内部解析路径尽量使用 `std::string_view` 表示源片段
- 只在需要持久化到 DOM 或对外返回时再落地成 `std::string`
- Name/Namespace 可继续加强现有 `XmlNameTable` 的使用范围，而不只用于 Reader 当前节点

### 本轮新增结论

在更贴近真实大文本内容的 segmented benchmark 下，subtree replay 的主热点已经从“首段文本取值”收敛为两块：

1. subtree 根元素的内容聚合 API，尤其是 `ReadElementContentAsString()`
2. 多段文本之间，特别是 comment 之后进入下一段 text 的节点推进

因此，下一步若继续做 Reader 提速，应该优先考虑：

1. 减少 replay 子树内部 repeated `Read()` 的推进成本
2. 继续压缩多段 text/comment 边界上的状态切换开销
3. 维持 detailed error line/column 与 stream buffering 行为不回归

不建议再优先投入“单段文本 `Value()`/`ReadString()` 微优化”，因为它已不再是 segmented 大子树场景中的主要瓶颈。

## 阶段 2：为超大文档支持做准备

目标：让库在“文档远大于可接受内存”的场景仍能工作，至少 Reader 能工作，DOM 路径可选择性工作。

### 1. 实现真正基于流的 XmlReader

优先级：P0

当前最大的结构阻塞不是 DOM，而是 Reader 也依赖整串输入。超大文档支持的第一步必须是：

- `XmlReader::Create(std::istream&)` 真正按块读取
- 词法扫描器支持 chunked buffer
- 跨块 token 需要拼接，但不能要求整个文档常驻内存

建议拆分：

1. 抽离字符源接口，统一支持 string source 与 stream source
2. 抽离 tokenizer，维护当前位置、行列、lookahead
3. Reader 事件生成只依赖 tokenizer，不直接依赖完整 `source_`

### 2. 把 ReadInnerXml/ReadOuterXml 从“默认能力”变成“按需成本”

超大文档下，这两个 API 天然可能昂贵，但不能让它们拖累普通 `Read()`。

建议：

- 对流式 Reader，只有在调用这些 API 时才消费子树并序列化
- 明确记录其额外成本和状态影响

### 3. DOM 加载路径改成“Reader 驱动构建”

优先级：P1

当前 DOM 解析和 Reader 解析虽然都有自己的逻辑，但超大文档支持要尽量避免再维护两套完全独立的扫描器。

建议：

- 让 `XmlDocument::Load(...)` 能走“Reader 事件 -> DOM 构建器”路径
- 这样一旦 Reader 支持真正流式，DOM 至少能在理论上支持边读边建
- 即便最终 DOM 仍因对象总量受内存限制，也能避免额外保存一整份源码文本

### 4. 重新定义“大文档模式”的推荐用法

超大文档支持不应承诺“超大 DOM 永远没问题”，而应明确区分：

- Reader 模式：支持超大文档顺序扫描、过滤、提取
- 选择性物化模式：只把命中的局部子树导入 DOM
- 全量 DOM 模式：仍受节点规模和内存限制，不作为超大文档主路径

### 5. 评估内存模型优化

当 Reader 真流式后，再决定 DOM 是否进一步做这些增强：

- arena/pool 分配器
- `shared_ptr` 替换为更轻量所有权模型
- 节点名、命名空间 URI、常见文本的更强字符串驻留
- 更紧凑的节点表示

这些改动收益可能很大，但侵入性也最高，不建议在没有基线和前两阶段结果前直接进入。

## 建议执行顺序

### 第一批

1. 建 benchmark 工程与样本数据
2. 去掉 Reader 默认元素预扫描
3. 改 bufferedNodes 队列结构
4. 测一轮前后收益

### 第二批

1. XPath 编译结果复用
2. 属性查找与兄弟导航热点优化
3. 解析路径内部 `string_view` 化

### 第三批

1. 抽象字符源与 tokenizer
2. 实现真正流式 `XmlReader::Create(std::istream&)`
3. 让 DOM 构建可复用 Reader 事件流

## 暂不建议现在就做的事

1. 不建议一开始就全面把 DOM 改成 arena + intrusive 结构。风险过高，且在没有 Reader 真流式前，超大文档主瓶颈并不在这里。
2. 不建议优先抠异常路径如 `ComputeLineColumn(...)` 的性能。这是冷路径，不该先动。
3. 不建议先优化 Schema 验证里的 `std::regex`。这块只有在 schema-heavy benchmark 证明确实占主导时才值得进入首批。

## 完成标准

性能优化阶段完成标准：

- Release 模式下，Reader 顺序读取和普通 DOM 解析在中大文档基线中有稳定可复现的下降幅度
- XPath 高频查询场景有明确收益
- 功能与异常语义回归全部通过

超大文档准备阶段完成标准：

- `XmlReader::Create(std::istream&)` 在常数级额外内存下可处理远大于内存舒适区的 XML 输入
- 普通 `Read()` 不依赖整文字符串常驻
- DOM 构建路径不再额外保留一整份源码副本

## 当前结论

如果现在就开始动手，第一刀应该落在 Reader：

1. 去掉 `ParseElement()` 中默认的 `CaptureElementXml(...)` 预扫描
2. 把 `bufferedNodes_` 改成真正队列
3. 同时建立 benchmark 基线

这三步成本最低，但能同时服务“现有性能优化”和“未来超大文档支持”两个目标。