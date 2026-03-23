# LibXML Benchmark Baseline

## Environment

- Date: 2026-03-20
- Build: Release x64
- Benchmark binary: [benchmarks/System.Xml.Benchmarks/build/System.Xml.Benchmarks/Release/System.Xml.Benchmarks.exe](benchmarks/System.Xml.Benchmarks/build/System.Xml.Benchmarks/Release/System.Xml.Benchmarks.exe)
- Build command:

```powershell
msbuild benchmarks\System.Xml.Benchmarks\System.Xml.Benchmarks.vcxproj /p:Configuration=Release /p:Platform=x64
```

- Run command:

```powershell
.\benchmarks\System.Xml.Benchmarks\build\System.Xml.Benchmarks\Release\System.Xml.Benchmarks.exe
```

## Scenarios

Current synthetic benchmark covers these public API paths:

1. `XmlDocument::Parse`
2. `XmlReader::Create(string) + Read()` full sequential scan
3. `XmlReader::Create(stream) + Read()` full sequential scan
4. Targeted XPath query on a parsed document
5. Broad XPath query on a parsed document

Datasets are generated catalog-style XML documents:

- `medium`: 250 books x 8 chapters, about 249 KB
- `large`: 2000 books x 8 chapters, about 1.998 MB

## Results

### medium

- Payload: `249141` bytes
- Iterations: `8`

| Scenario | Avg ms | Min ms | Max ms |
| --- | ---: | ---: | ---: |
| XmlDocument::Parse | 5.060 | 4.808 | 5.344 |
| XmlReader::Create(string)+Read | 9.190 | 8.782 | 9.858 |
| XmlReader::Create(stream)+Read | 9.719 | 9.485 | 10.132 |
| XPath targeted SelectNodes | 0.076 | 0.074 | 0.081 |
| XPath broad SelectNodes | 0.907 | 0.873 | 0.973 |

### large

- Payload: `1997999` bytes
- Iterations: `5`

| Scenario | Avg ms | Min ms | Max ms |
| --- | ---: | ---: | ---: |
| XmlDocument::Parse | 42.079 | 41.727 | 42.292 |
| XmlReader::Create(string)+Read | 77.190 | 76.012 | 78.011 |
| XmlReader::Create(stream)+Read | 83.261 | 81.694 | 86.383 |
| XPath targeted SelectNodes | 0.528 | 0.519 | 0.543 |
| XPath broad SelectNodes | 9.624 | 9.304 | 9.892 |

## First observations

1. Sequential `XmlReader` scan is significantly slower than DOM parse on these datasets, which is consistent with the current eager element preview and buffered-node behavior.
2. `Create(stream)` is only slightly slower than `Create(string)`, which matches the current implementation: both paths still materialize the full source text before tokenization.
3. Broad XPath queries scale much faster than targeted XPath queries, which is expected from the current parse-every-time and collect/filter/deduplicate execution model.

## After Reader lazy element preview

Change summary:

1. `XmlReader::ParseElement()` no longer eagerly calls `CaptureElementXml(...)` for non-empty elements.
2. `ReadInnerXml()` and `ReadOuterXml()` now capture and cache element markup on first demand.

Validation:

1. Release solution build passed.
2. Release test binary passed: `All System.Xml tests passed.`

Updated benchmark results:

### medium

| Scenario | Before avg ms | After avg ms | Delta |
| --- | ---: | ---: | ---: |
| XmlReader::Create(string)+Read | 9.190 | 4.794 | -47.8% |
| XmlReader::Create(stream)+Read | 9.719 | 5.554 | -42.9% |

### large

| Scenario | Before avg ms | After avg ms | Delta |
| --- | ---: | ---: | ---: |
| XmlReader::Create(string)+Read | 77.190 | 41.892 | -45.7% |
| XmlReader::Create(stream)+Read | 83.261 | 47.255 | -43.2% |

Notes:

1. `XmlDocument::Parse` and XPath numbers stayed in the same band, which is expected because this change targeted only the Reader hot path.
2. The first optimization pass confirms the earlier hypothesis: eager element preview was the dominant cost on sequential Reader scans.

## After buffered queue deque change

Change summary:

1. `XmlReader::bufferedNodes_` was changed from `std::vector` to `std::deque`.
2. `TryConsumeBufferedNode()` now uses `pop_front()` instead of `erase(begin())`.

Validation:

1. Release solution build passed.
2. Release test binary passed: `All System.Xml tests passed.`

Updated benchmark results versus the lazy-element-preview pass:

### medium

| Scenario | Pass 1 avg ms | Pass 2 avg ms | Delta |
| --- | ---: | ---: | ---: |
| XmlReader::Create(string)+Read | 4.794 | 4.560 | -4.9% |
| XmlReader::Create(stream)+Read | 5.554 | 5.701 | +2.6% |

### large

| Scenario | Pass 1 avg ms | Pass 2 avg ms | Delta |
| --- | ---: | ---: | ---: |
| XmlReader::Create(string)+Read | 41.892 | 36.399 | -13.1% |
| XmlReader::Create(stream)+Read | 47.255 | 42.624 | -9.8% |

Notes:

1. This optimization is materially smaller than the lazy element preview change, which is expected because it targets a narrower internal queue hot path.
2. The `stream` medium dataset showed minor noise, but the larger dataset still showed a clear improvement, so the deque change is still directionally correct.
3. Current synthetic benchmarks are not heavily entity-buffering-focused, so this likely understates the benefit on workloads with more queued reader events.

## Notes

1. This is a single-machine baseline intended for before/after comparison inside this repository, not a cross-machine public benchmark.
2. The current suite is intentionally small and synthetic; it is enough to validate the first optimization wave around Reader and XPath execution.
3. Future benchmark expansions should add larger payloads, file-backed reader scenarios, and memory measurements.

## After subtree replay segmented benchmark expansion

Change summary:

1. Added a multi-segment subtree replay dataset builder that splits large child text with comment boundaries, so replay is forced to cross repeated text/comment transitions instead of reading a single monolithic text node.
2. Added a dedicated segmented suite to isolate subtree replay steps that remain semantically valid on multi-segment content.
3. Added focused measurements for subtree creation, first text access, root `ReadString`, root `ReadElementContentAsString`, traversal, and comment-to-next-text transitions.

Validation:

1. Release solution build passed.
2. Release test binary passed: `All System.Xml tests passed.`

Key retained implementation state behind these numbers:

1. `ReadSubtree()` keeps the `<= 20 MiB` materialization fast path.
2. Shared-subrange replay remains enabled only for larger subtree paths.
3. Replay temp stream buffer remains `256 KiB`; replay search chunk size remains `64 KiB`.
4. `ReadElementContentAsString()` / `ReadElementString()` keep the simple-content direct-consume fast path.
5. `ParseText()` keeps the single-pass `"\n<&"` scan and replay keeps the matching hot search branch.

### single-doc reference points

These numbers remain useful as a control group because they keep the original single large text payload shape.

| Dataset | Scenario | Avg ms |
| --- | --- | ---: |
| single-doc xlarge-16mb | ReadElementContent | 18.5 |
| single-doc xlarge-16mb | ReadSubtree replay | 64.7 |
| single-doc xxlarge-64mb | ReadElementContent | 76.5 |
| single-doc xxlarge-64mb | ReadSubtree replay | 233.7 |

### segmented-doc retained baseline

The segmented datasets better represent large content that contains repeated boundaries inside one logical element payload.

| Dataset | Scenario | Avg ms |
| --- | --- | ---: |
| segmented-doc xlarge-16mb | Stream ReadSubtree replay | 206.009 |
| segmented-doc xlarge-16mb | Stream Subtree root element content | 96.074 |
| segmented-doc xlarge-16mb | Stream Subtree comment to next text | 0.776 |
| segmented-doc xxlarge-64mb | Stream ReadSubtree replay | 1066.000 |
| segmented-doc xxlarge-64mb | Stream Subtree root element content | 582.000 |
| segmented-doc xxlarge-64mb | Stream Subtree comment to next text | 0.920 |

### segmented benchmark conclusions

1. The dominant cost in large replayed multi-segment content is no longer first-text `Value()` access; it is subtree root consumption and repeated node advancement inside the subtree.
2. The direct-consume simple-content path materially reduces root element content cost without changing public API behavior.
3. The single-pass `ParseText()` scan removes a previously large boundary-transition hotspot. After the change, `comment -> next text` is no longer a top-level cost center.
4. Correctness fixes that restored detailed line/column reporting and prevented XML declaration reads from forcing full buffering did not erase the retained segmented benchmark gains.

### guardrails from this pass

1. The earlier fast path variant that aggregated simple content and then called `Skip()` is intentionally not retained, because it re-traversed the subtree and regressed the segmented replay path.
2. Line-info helpers must stay bounded to the requested source range. Any unbounded newline search on a stream-backed source risks forcing full buffering and invalidating the streaming benchmark model.