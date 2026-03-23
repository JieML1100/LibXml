#include "XmlInternal.h"

namespace System::Xml {

class StreamXmlReaderInputSource final : public XmlReaderInputSource {
public:
    ~StreamXmlReaderInputSource() override {
        CloseReplayFile();
        RemoveReplayFile();
    }

    explicit StreamXmlReaderInputSource(std::istream& stream)
        : stream_(&stream) {
    }

    StreamXmlReaderInputSource(std::istream& stream, std::string initialBuffer)
        : stream_(&stream),
          buffer_(std::move(initialBuffer)),
          eof_(stream_ == nullptr || !stream_->good()) {
    }

    explicit StreamXmlReaderInputSource(std::shared_ptr<std::istream> stream)
        : ownedStream_(std::move(stream)),
          stream_(ownedStream_.get()) {
    }

    StreamXmlReaderInputSource(std::shared_ptr<std::istream> stream, std::string initialBuffer)
        : ownedStream_(std::move(stream)),
          stream_(ownedStream_.get()),
          buffer_(std::move(initialBuffer)),
          eof_(stream_ == nullptr || !stream_->good()) {
    }

    char CharAt(std::size_t position) const noexcept override {
        EnsureBufferedThrough(position);
        if (position >= bufferStartOffset_) {
            const auto bufferIndex = bufferLogicalStart_ + (position - bufferStartOffset_);
            return bufferIndex < buffer_.size() ? buffer_[bufferIndex] : '\0';
        }
        return ReadReplayCharAt(position);
    }

    const char* PtrAt(std::size_t position, std::size_t& available) const noexcept override {
        available = 0;
        EnsureBufferedThrough(position);
        if (position >= bufferStartOffset_) {
            const auto bufferIndex = bufferLogicalStart_ + (position - bufferStartOffset_);
            if (bufferIndex < buffer_.size()) {
                available = buffer_.size() - bufferIndex;
                return buffer_.data() + static_cast<std::ptrdiff_t>(bufferIndex);
            }
            return nullptr;
        }
        return ReadReplayPtrAt(position, available);
    }

    std::size_t Find(const std::string& token, std::size_t position) const noexcept override {
        if (token.empty()) {
            return position;
        }

        const bool singleToken = token.size() == 1;
        const char singleTokenChar = singleToken ? token.front() : '\0';

        if (position < bufferStartOffset_) {
            const std::size_t replaySearchEnd = (std::min)(bufferStartOffset_, replayLength_);
            const std::size_t replayFound = singleToken
                ? FindFirstOfInReplay(std::string_view(&singleTokenChar, 1), position, replaySearchEnd)
                : FindInReplay(token, position, replaySearchEnd);
            if (replayFound != std::string::npos) {
                return replayFound;
            }

            if (token.size() > 1 && bufferStartOffset_ != 0) {
                const std::size_t boundaryStart = bufferStartOffset_ >= token.size() - 1
                    ? (std::max)(position, bufferStartOffset_ - (token.size() - 1))
                    : position;
                const std::size_t boundaryEnd = BufferedEndOffset();
                for (std::size_t cursor = boundaryStart; cursor < bufferStartOffset_; ++cursor) {
                    if (cursor > boundaryEnd || token.size() > boundaryEnd - cursor) {
                        break;
                    }

                    bool matched = true;
                    for (std::size_t index = 0; index < token.size(); ++index) {
                        if (CharAt(cursor + index) != token[index]) {
                            matched = false;
                            break;
                        }
                    }
                    if (matched) {
                        return cursor;
                    }
                }
            }

            position = bufferStartOffset_;
        }

        EnsureBufferedThrough(position);
        std::size_t searchPosition = position >= bufferStartOffset_
            ? bufferLogicalStart_ + (position - bufferStartOffset_)
            : bufferLogicalStart_;
        while (true) {
            const auto found = singleToken
                ? buffer_.find(singleTokenChar, searchPosition)
                : buffer_.find(token, searchPosition);
            if (found != std::string::npos) {
                return bufferStartOffset_ + (found - bufferLogicalStart_);
            }
            if (eof_) {
                return std::string::npos;
            }

            const auto sizeBefore = buffer_.size();
            ReadMore();
            if (buffer_.size() == sizeBefore) {
                return std::string::npos;
            }

            if (token.size() > 1 && sizeBefore >= token.size() - 1) {
                searchPosition = (std::max)(searchPosition, sizeBefore - (token.size() - 1));
            } else {
                searchPosition = (std::max)(searchPosition, sizeBefore);
            }
        }
    }

    std::size_t FindFirstOf(std::string_view tokens, std::size_t position) const noexcept {
        if (tokens.empty()) {
            return position;
        }

        if (position < bufferStartOffset_) {
            const std::size_t replaySearchEnd = (std::min)(bufferStartOffset_, replayLength_);
            const std::size_t replayFound = FindFirstOfInReplay(tokens, position, replaySearchEnd);
            if (replayFound != std::string::npos) {
                return replayFound;
            }
            position = bufferStartOffset_;
        }

        EnsureBufferedThrough(position);
        std::size_t searchPosition = position >= bufferStartOffset_
            ? bufferLogicalStart_ + (position - bufferStartOffset_)
            : bufferLogicalStart_;
        while (true) {
            const auto found = buffer_.find_first_of(tokens, searchPosition);
            if (found != std::string::npos) {
                return bufferStartOffset_ + (found - bufferLogicalStart_);
            }
            if (eof_) {
                return std::string::npos;
            }

            const auto sizeBefore = buffer_.size();
            ReadMore();
            if (buffer_.size() == sizeBefore) {
                return std::string::npos;
            }

            searchPosition = (std::max)(searchPosition, sizeBefore);
        }
    }

    std::size_t FindNextTextSpecial(std::size_t position) const noexcept {
        if (position < bufferStartOffset_) {
            const std::size_t replaySearchEnd = (std::min)(bufferStartOffset_, replayLength_);
            const std::size_t replayFound = FindFirstOfInReplay("<&", position, replaySearchEnd);
            if (replayFound != std::string::npos) {
                return replayFound;
            }
            position = bufferStartOffset_;
        }

        EnsureBufferedThrough(position);
        std::size_t searchPosition = position >= bufferStartOffset_
            ? bufferLogicalStart_ + (position - bufferStartOffset_)
            : bufferLogicalStart_;
        while (true) {
            const auto found = buffer_.find_first_of("<&", searchPosition);
            if (found != std::string::npos) {
                return bufferStartOffset_ + (found - bufferLogicalStart_);
            }
            if (eof_) {
                return std::string::npos;
            }

            const auto sizeBefore = buffer_.size();
            ReadMore();
            if (buffer_.size() == sizeBefore) {
                return std::string::npos;
            }

            searchPosition = (std::max)(searchPosition, sizeBefore);
        }
    }

    std::size_t ScanQuotedValueEnd(std::size_t quoteStart) const noexcept {
        const char quote = CharAt(quoteStart);
        if (quote != '"' && quote != '\'') {
            return std::string::npos;
        }

        std::size_t scanPosition = quoteStart + 1;
        while (true) {
            EnsureBufferedThrough(scanPosition);
            if (scanPosition < bufferStartOffset_) {
                break;
            }

            const std::size_t bufferIndex = bufferLogicalStart_ + (scanPosition - bufferStartOffset_);
            if (bufferIndex < buffer_.size()) {
                const std::size_t offset = FindQuoteOrNulInBuffer(
                    buffer_.data() + bufferIndex,
                    buffer_.size() - bufferIndex,
                    quote);
                if (offset != std::string::npos) {
                    const std::size_t match = scanPosition + offset;
                    return CharAt(match) == quote ? match : std::string::npos;
                }
            }

            if (eof_) {
                return std::string::npos;
            }

            const std::size_t previousEnd = BufferedEndOffset();
            ReadMore();
            if (BufferedEndOffset() == previousEnd) {
                return std::string::npos;
            }
            scanPosition = previousEnd;
        }

        return ScanQuotedValueEndAt(quoteStart, [this](std::size_t probe) noexcept {
            return CharAt(probe);
        });
    }


    std::string Slice(std::size_t start, std::size_t count) const override {
        std::string result;
        AppendSliceTo(result, start, count);
        return result;
    }

    void AppendSliceTo(std::string& target, std::size_t start, std::size_t count) const override {
        if (count == std::string::npos) {
            EnsureAllBuffered();
        } else if (count != 0 && start <= (std::numeric_limits<std::size_t>::max)() - (count - 1)) {
            EnsureBufferedThrough(start + count - 1);
        } else {
            EnsureBufferedThrough(start);
        }

        const std::size_t bufferedEnd = BufferedEndOffset();
        if (start >= bufferedEnd && !(HasReplayFile() && start < replayLength_)) {
            return;
        }

        const std::size_t requestedEnd = count == std::string::npos
            ? bufferedEnd
            : start + (std::min)(count, bufferedEnd > start ? bufferedEnd - start : 0);
        const std::size_t actualEnd = count == std::string::npos
            ? bufferedEnd
            : (std::min)(requestedEnd, bufferedEnd);

        if (start < bufferStartOffset_) {
            const std::size_t replayEnd = (std::min)(actualEnd, bufferStartOffset_);
            if (replayEnd > start) {
                AppendReplaySliceTo(target, start, replayEnd - start);
            }
        }

        if (actualEnd > bufferStartOffset_) {
            const std::size_t memoryStart = start < bufferStartOffset_ ? bufferStartOffset_ : start;
            const std::size_t memoryCount = actualEnd - memoryStart;
            if (memoryCount != 0) {
                target.append(buffer_, bufferLogicalStart_ + (memoryStart - bufferStartOffset_), memoryCount);
            }
        }
    }

private:
    static constexpr std::size_t ChunkSize = 64 * 1024;
    static constexpr std::size_t BufferCompactionThreshold = 256 * 1024;
    static constexpr std::size_t ReplayIoBufferSize = 256 * 1024;

    std::size_t ActiveBufferSize() const noexcept {
        return buffer_.size() >= bufferLogicalStart_ ? buffer_.size() - bufferLogicalStart_ : 0;
    }

    std::size_t BufferedEndOffset() const noexcept {
        return bufferStartOffset_ + ActiveBufferSize();
    }

    void MaybeCompactBuffer() const {
        if (bufferLogicalStart_ == 0) {
            return;
        }

        const std::size_t activeSize = ActiveBufferSize();
        if (bufferLogicalStart_ < BufferCompactionThreshold && bufferLogicalStart_ < activeSize) {
            return;
        }

        buffer_.erase(0, bufferLogicalStart_);
        bufferLogicalStart_ = 0;
    }

    void CloseOwnedStreamIfPossible() const noexcept {
        if (ownedStream_ == nullptr) {
            return;
        }

        if (auto* fileStream = dynamic_cast<std::ifstream*>(ownedStream_.get()); fileStream != nullptr) {
            fileStream->close();
        }
        stream_ = nullptr;
    }

    void CloseReplayFile() const noexcept {
        if (replayFile_ != nullptr && replayFile_->is_open()) {
            replayFile_->close();
        }
    }

    void RemoveReplayFile() const noexcept {
        if (replayPath_.empty()) {
            return;
        }
        std::error_code error;
        std::filesystem::remove(replayPath_, error);
        replayPath_.clear();
    }

    bool HasReplayFile() const noexcept {
        return replayFile_ != nullptr && replayFile_->is_open();
    }

    void EnableReplay() const override {
        replayEnabled_ = true;
        if (!HasReplayFile()) {
            EnsureReplayFile();
        }
    }

    void DiscardBefore(std::size_t position) const override {
        if (position <= bufferStartOffset_) {
            return;
        }

        const std::size_t bufferEnd = BufferedEndOffset();
        const std::size_t discardEnd = (std::min)(position, bufferEnd);
        if (discardEnd <= bufferStartOffset_) {
            return;
        }

        if (replayEnabled_ && !HasReplayFile()) {
            EnsureReplayFile();
        }

        const std::size_t discardCount = discardEnd - bufferStartOffset_;
        bufferStartOffset_ = discardEnd;
        bufferLogicalStart_ += discardCount;
        MaybeCompactBuffer();
    }

    void EnsureReplayFile() const {
        if (HasReplayFile()) {
            return;
        }

        replayPath_ = CreateTemporaryXmlReplayPath();
        replayFile_ = std::make_unique<std::fstream>(
            replayPath_,
            std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (replayFile_ == nullptr || !replayFile_->is_open()) {
            RemoveReplayFile();
            throw XmlException("Failed to create stream replay file");
        }

        replayIoBuffer_.assign(ReplayIoBufferSize, '\0');
        replayFile_->rdbuf()->pubsetbuf(replayIoBuffer_.data(), static_cast<std::streamsize>(replayIoBuffer_.size()));

        const std::size_t activeSize = ActiveBufferSize();
        if (activeSize != 0) {
            replayFile_->seekp(static_cast<std::streamoff>(bufferStartOffset_), std::ios::beg);
            replayFile_->write(
                buffer_.data() + static_cast<std::ptrdiff_t>(bufferLogicalStart_),
                static_cast<std::streamsize>(activeSize));
            if (!*replayFile_) {
                CloseReplayFile();
                RemoveReplayFile();
                replayFile_.reset();
                throw XmlException("Failed to seed stream replay file");
            }
        }
        replayLength_ = bufferStartOffset_ + activeSize;
        replayFile_->flush();
    }

    char ReadReplayCharAt(std::size_t position) const noexcept {
        std::size_t available = 0;
        if (const char* ptr = ReadReplayPtrAt(position, available); ptr != nullptr) {
            return *ptr;
        }
        return '\0';
    }

    const char* ReadReplayPtrAt(std::size_t position, std::size_t& available) const noexcept {
        available = 0;
        if (!HasReplayFile() || position >= replayLength_) {
            return nullptr;
        }

        if (position < replayWindowStart_ || position >= replayWindowStart_ + replayWindow_.size()) {
            replayWindowStart_ = position;
            const std::size_t remaining = replayLength_ - position;
            const std::size_t windowSize = (std::min)(ChunkSize, remaining);
            replayWindow_.assign(windowSize, '\0');

            replayFile_->clear();
            replayFile_->seekg(static_cast<std::streamoff>(position), std::ios::beg);
            replayFile_->read(replayWindow_.data(), static_cast<std::streamsize>(windowSize));
            const auto bytesRead = replayFile_->gcount();
            if (bytesRead <= 0) {
                replayFile_->clear();
                replayWindow_.clear();
                return nullptr;
            }

            replayWindow_.resize(static_cast<std::size_t>(bytesRead));
            replayFile_->clear();
        }

        const std::size_t windowIndex = position - replayWindowStart_;
        if (windowIndex >= replayWindow_.size()) {
            return nullptr;
        }

        available = replayWindow_.size() - windowIndex;
        return replayWindow_.data() + static_cast<std::ptrdiff_t>(windowIndex);
    }

    void AppendReplaySliceTo(std::string& target, std::size_t start, std::size_t count) const {
        if (!HasReplayFile() || count == 0 || start >= replayLength_) {
            return;
        }

        const std::size_t actualCount = (std::min)(count, replayLength_ - start);
        const std::size_t originalSize = target.size();
        target.resize(originalSize + actualCount);
        replayFile_->clear();
        std::streambuf* replayBuffer = replayFile_->rdbuf();
        replayBuffer->pubseekpos(static_cast<std::streampos>(start), std::ios::in);
        const auto bytesRead = replayBuffer->sgetn(
            target.data() + static_cast<std::ptrdiff_t>(originalSize),
            static_cast<std::streamsize>(actualCount));
        replayFile_->clear();
        if (bytesRead > 0) {
            target.resize(originalSize + static_cast<std::size_t>(bytesRead));
        } else {
            target.resize(originalSize);
        }
    }

    std::size_t FindInReplay(const std::string& token, std::size_t start, std::size_t end) const noexcept {
        if (!HasReplayFile() || start >= end) {
            return std::string::npos;
        }

        const std::size_t overlap = token.size() > 1 ? token.size() - 1 : 0;
        std::size_t cursor = start;
        std::string carry;

        while (cursor < end) {
            const std::size_t remaining = end - cursor;
            const std::size_t chunkSize = (std::min)(ChunkSize, remaining);
            std::string block = carry;
            const std::size_t blockPrefixSize = carry.size();

            replayFile_->clear();
            replayFile_->seekg(static_cast<std::streamoff>(cursor), std::ios::beg);
            const std::size_t blockOriginalSize = block.size();
            block.resize(blockOriginalSize + chunkSize);
            replayFile_->read(block.data() + static_cast<std::ptrdiff_t>(blockOriginalSize), static_cast<std::streamsize>(chunkSize));
            const auto bytesRead = replayFile_->gcount();
            replayFile_->clear();
            if (bytesRead <= 0) {
                return std::string::npos;
            }
            block.resize(blockOriginalSize + static_cast<std::size_t>(bytesRead));

            const auto found = block.find(token);
            if (found != std::string::npos) {
                const std::size_t matchStart = cursor - blockPrefixSize + found;
                if (matchStart + token.size() <= end) {
                    return matchStart;
                }
            }

            if (overlap != 0) {
                const std::size_t carrySize = (std::min)(overlap, block.size());
                carry.assign(block, block.size() - carrySize, carrySize);
            }
            cursor += static_cast<std::size_t>(bytesRead);
        }

        return std::string::npos;
    }

    std::size_t FindFirstOfInReplay(std::string_view tokens, std::size_t start, std::size_t end) const noexcept {
        if (!HasReplayFile() || start >= end || tokens.empty()) {
            return std::string::npos;
        }

        std::size_t cursor = start;
        const bool textSpecialTokens = tokens == "<&";
        const bool textLineSpecialTokens = tokens == "\n<&";
        const bool singleToken = tokens.size() == 1;
        const char singleTokenChar = singleToken ? tokens.front() : '\0';
        while (cursor < end) {
            const std::size_t remaining = end - cursor;
            const std::size_t chunkSize = (std::min)(ChunkSize, remaining);
            std::string block(chunkSize, '\0');

            replayFile_->clear();
            replayFile_->seekg(static_cast<std::streamoff>(cursor), std::ios::beg);
            replayFile_->read(block.data(), static_cast<std::streamsize>(chunkSize));
            const auto bytesRead = replayFile_->gcount();
            replayFile_->clear();
            if (bytesRead <= 0) {
                return std::string::npos;
            }

            if (textSpecialTokens) {
                for (std::size_t index = 0; index < static_cast<std::size_t>(bytesRead); ++index) {
                    const char ch = block[index];
                    if (ch == '<' || ch == '&') {
                        return cursor + index;
                    }
                }
            } else if (textLineSpecialTokens) {
                for (std::size_t index = 0; index < static_cast<std::size_t>(bytesRead); ++index) {
                    const char ch = block[index];
                    if (ch == '\n' || ch == '<' || ch == '&') {
                        return cursor + index;
                    }
                }
            } else if (singleToken) {
                for (std::size_t index = 0; index < static_cast<std::size_t>(bytesRead); ++index) {
                    if (block[index] == singleTokenChar) {
                        return cursor + index;
                    }
                }
            } else {
                for (std::size_t index = 0; index < static_cast<std::size_t>(bytesRead); ++index) {
                    if (tokens.find(block[index]) != std::string_view::npos) {
                        return cursor + index;
                    }
                }
            }

            cursor += static_cast<std::size_t>(bytesRead);
        }

        return std::string::npos;
    }


    void EnsureBufferedThrough(std::size_t position) const noexcept {
        while (!eof_ && BufferedEndOffset() <= position) {
            ReadMore();
        }
    }

    void EnsureAllBuffered() const noexcept {
        while (!eof_) {
            ReadMore();
        }
    }

    void ReadMore() const noexcept {
        if (stream_ == nullptr || eof_) {
            eof_ = true;
            return;
        }

        char chunk[ChunkSize];
        stream_->read(chunk, static_cast<std::streamsize>(ChunkSize));
        const auto bytesRead = stream_->gcount();
        if (bytesRead > 0) {
            const std::size_t byteCount = static_cast<std::size_t>(bytesRead);
            buffer_.append(chunk, byteCount);
            if (replayEnabled_ && !HasReplayFile()) {
                try {
                    EnsureReplayFile();
                } catch (...) {
                    eof_ = true;
                    CloseOwnedStreamIfPossible();
                    return;
                }
            }
            if (replayEnabled_ && HasReplayFile()) {
                replayFile_->clear();
                replayFile_->seekp(static_cast<std::streamoff>(replayLength_), std::ios::beg);
                replayFile_->write(chunk, bytesRead);
                replayFile_->flush();
                if (!*replayFile_) {
                    eof_ = true;
                    CloseOwnedStreamIfPossible();
                    return;
                }
                replayLength_ += byteCount;
            }
        }
        if (bytesRead < static_cast<std::streamsize>(ChunkSize) || !stream_->good()) {
            eof_ = true;
            CloseOwnedStreamIfPossible();
        }
    }

    mutable std::shared_ptr<std::istream> ownedStream_;
    mutable std::istream* stream_ = nullptr;
    mutable std::string buffer_;
    mutable std::size_t bufferStartOffset_ = 0;
    mutable std::size_t bufferLogicalStart_ = 0;
    mutable std::unique_ptr<std::fstream> replayFile_;
    mutable std::filesystem::path replayPath_;
    mutable std::vector<char> replayIoBuffer_;
    mutable std::size_t replayLength_ = 0;
    mutable std::size_t replayWindowStart_ = 0;
    mutable std::string replayWindow_;
    mutable bool replayEnabled_ = false;
    mutable bool eof_ = false;
};

class SubrangeXmlReaderInputSource final : public XmlReaderInputSource {
public:
    SubrangeXmlReaderInputSource(std::shared_ptr<const XmlReaderInputSource> inputSource, std::size_t start, std::size_t length)
        : inputSource_(std::move(inputSource)),
          start_(start),
          length_(length) {
    }

    char CharAt(std::size_t position) const noexcept override {
        if (inputSource_ == nullptr || position >= length_) {
            return '\0';
        }
        return inputSource_->CharAt(start_ + position);
    }

    const char* PtrAt(std::size_t position, std::size_t& available) const noexcept override {
        available = 0;
        if (inputSource_ == nullptr || position >= length_) {
            return nullptr;
        }

        std::size_t sourceAvailable = 0;
        const char* ptr = inputSource_->PtrAt(start_ + position, sourceAvailable);
        if (ptr == nullptr) {
            return nullptr;
        }

        available = (std::min)(sourceAvailable, length_ - position);
        return available == 0 ? nullptr : ptr;
    }

    std::size_t Find(const std::string& token, std::size_t position) const noexcept override {
        if (inputSource_ == nullptr) {
            return std::string::npos;
        }
        if (token.empty()) {
            return position <= length_ ? position : std::string::npos;
        }
        if (position >= length_) {
            return std::string::npos;
        }

        const std::size_t found = inputSource_->Find(token, start_ + position);
        if (found == std::string::npos || found < start_) {
            return std::string::npos;
        }

        const std::size_t end = start_ + length_;
        if (found >= end || token.size() > end - found) {
            return std::string::npos;
        }

        return found - start_;
    }

    std::size_t FindFirstOf(std::string_view tokens, std::size_t position) const noexcept {
        if (tokens.empty()) {
            return position <= length_ ? position : std::string::npos;
        }
        if (inputSource_ == nullptr || position >= length_) {
            return std::string::npos;
        }

        const std::size_t end = start_ + length_;
        if (const auto* streamSource = dynamic_cast<const StreamXmlReaderInputSource*>(inputSource_.get()); streamSource != nullptr) {
            const std::size_t found = tokens == "<&"
                ? streamSource->FindNextTextSpecial(start_ + position)
                : streamSource->FindFirstOf(tokens, start_ + position);
            if (found == std::string::npos || found < start_ || found >= end) {
                return std::string::npos;
            }
            return found - start_;
        }
        if (const auto* stringSource = dynamic_cast<const StringXmlReaderInputSource*>(inputSource_.get()); stringSource != nullptr) {
            const std::size_t found = stringSource->FindFirstOf(tokens, start_ + position);
            if (found == std::string::npos || found < start_ || found >= end) {
                return std::string::npos;
            }
            return found - start_;
        }

        for (std::size_t probe = position; probe < length_; ++probe) {
            const char ch = inputSource_->CharAt(start_ + probe);
            if (ch == '\0') {
                return std::string::npos;
            }
            if (tokens.find(ch) != std::string_view::npos) {
                return probe;
            }
        }
        return std::string::npos;
    }

    std::size_t ScanQuotedValueEnd(std::size_t quoteStart) const noexcept {
        if (inputSource_ == nullptr || quoteStart >= length_) {
            return std::string::npos;
        }

        std::size_t found = std::string::npos;
        if (const auto* stringSource = dynamic_cast<const StringXmlReaderInputSource*>(inputSource_.get()); stringSource != nullptr) {
            found = stringSource->ScanQuotedValueEnd(start_ + quoteStart);
        } else if (const auto* streamSource = dynamic_cast<const StreamXmlReaderInputSource*>(inputSource_.get()); streamSource != nullptr) {
            found = streamSource->ScanQuotedValueEnd(start_ + quoteStart);
        } else {
            found = ScanQuotedValueEndAt(start_ + quoteStart, [this](std::size_t probe) noexcept {
                return inputSource_->CharAt(probe);
            });
        }

        if (found == std::string::npos || found < start_ || found >= start_ + length_) {
            return std::string::npos;
        }
        return found - start_;
    }

    std::string Slice(std::size_t start, std::size_t count) const override {
        if (inputSource_ == nullptr || start >= length_) {
            return {};
        }

        const std::size_t available = length_ - start;
        const std::size_t clampedCount = count == std::string::npos ? available : (std::min)(count, available);
        return inputSource_->Slice(start_ + start, clampedCount);
    }

    void AppendSliceTo(std::string& target, std::size_t start, std::size_t count) const override {
        if (inputSource_ == nullptr || start >= length_) {
            return;
        }

        const std::size_t available = length_ - start;
        const std::size_t clampedCount = count == std::string::npos ? available : (std::min)(count, available);
        inputSource_->AppendSliceTo(target, start_ + start, clampedCount);
    }

    void EnableReplay() const override {
        if (inputSource_ != nullptr) {
            inputSource_->EnableReplay();
        }
    }

private:
    std::shared_ptr<const XmlReaderInputSource> inputSource_;
    std::size_t start_ = 0;
    std::size_t length_ = 0;
};

std::size_t FindFirstOfFromInputSource(const XmlReaderInputSource* inputSource, std::string_view tokens, std::size_t position) noexcept {
    if (inputSource == nullptr) {
        return std::string::npos;
    }
    if (const auto* streamSource = dynamic_cast<const StreamXmlReaderInputSource*>(inputSource); streamSource != nullptr) {
        if (tokens == "<&") {
            return streamSource->FindNextTextSpecial(position);
        }
        return streamSource->FindFirstOf(tokens, position);
    }
    if (const auto* subrangeSource = dynamic_cast<const SubrangeXmlReaderInputSource*>(inputSource); subrangeSource != nullptr) {
        return subrangeSource->FindFirstOf(tokens, position);
    }

    if (tokens == "<&") {
        const std::size_t nextEntity = inputSource->Find("&", position);
        const std::size_t nextMarkup = inputSource->Find("<", position);
        if (nextEntity == std::string::npos) {
            return nextMarkup;
        }
        if (nextMarkup == std::string::npos) {
            return nextEntity;
        }
        return (std::min)(nextEntity, nextMarkup);
    }

    if (tokens.empty()) {
        return position;
    }
    for (std::size_t probe = position; ; ++probe) {
        const char ch = inputSource->CharAt(probe);
        if (ch == '\0') {
            return std::string::npos;
        }
        if (tokens.find(ch) != std::string_view::npos) {
            return probe;
        }
    }
}

void AdvanceLineInfoFromInputSource(
    const XmlReaderInputSource* inputSource,
    std::size_t start,
    std::size_t end,
    std::size_t& line,
    std::size_t& column) noexcept {
    if (end <= start) {
        return;
    }
    if (inputSource == nullptr) {
        column += end - start;
        return;
    }

    for (std::size_t index = start; index < end; ++index) {
        if (inputSource->CharAt(index) == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
}

void SkipXmlWhitespaceAt(const XmlReaderInputSource* inputSource, std::size_t& position) noexcept {
    while (inputSource != nullptr) {
        const char ch = inputSource->CharAt(position);
        if (ch == '\0' || !IsWhitespace(ch)) {
            break;
        }
        ++position;
    }
}

void SkipXmlWhitespaceAt(std::string_view text, std::size_t& position) noexcept {
    const std::size_t size = text.size();
    if (position >= size) return;

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__)
    const __m128i v_space = _mm_set1_epi8(' ');
    const __m128i v_tab = _mm_set1_epi8('\t');
    const __m128i v_lf = _mm_set1_epi8('\n');
    const __m128i v_cr = _mm_set1_epi8('\r');

    while (position + 16 <= size) {
        __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(text.data() + position));

        __m128i m_space = _mm_cmpeq_epi8(chunk, v_space);
        __m128i m_tab = _mm_cmpeq_epi8(chunk, v_tab);
        __m128i m_lf = _mm_cmpeq_epi8(chunk, v_lf);
        __m128i m_cr = _mm_cmpeq_epi8(chunk, v_cr);

        __m128i is_ws = _mm_or_si128(_mm_or_si128(m_space, m_tab),
            _mm_or_si128(m_lf, m_cr));

        unsigned int mask = static_cast<unsigned int>(_mm_movemask_epi8(is_ws));

        if (mask != 0xFFFF) {
            unsigned long first_non_ws_index = 0;
#ifdef _MSC_VER
            _BitScanForward(&first_non_ws_index, ~mask & 0xFFFF);
#else
            first_non_ws_index = __builtin_ctz(~mask | 0x10000);
#endif
            position += first_non_ws_index;
            return;
        }
        position += 16;
    }
#endif
    while (position < size && IsWhitespace(text[position])) {
        ++position;
    }
}

bool TryConsumeNameFromInputSourceAt(const XmlReaderInputSource* inputSource, std::size_t& position) noexcept {
    if (inputSource == nullptr) {
        return false;
    }

    const char first = inputSource->CharAt(position);
    if (first == '\0' || !IsNameStartChar(first)) {
        return false;
    }

    ++position;
    while (true) {
        const char ch = inputSource->CharAt(position);
        if (ch == '\0' || !IsNameChar(ch)) {
            break;
        }
        ++position;
    }

    return true;
}

std::string ParseNameFromInputSourceAt(const XmlReaderInputSource* inputSource, std::size_t& position) {
    const std::size_t start = position;
    if (!TryConsumeNameFromInputSourceAt(inputSource, position)) {
        return {};
    }

    return inputSource->Slice(start, position - start);
}

bool SkipTagFromInputSourceAt(const XmlReaderInputSource* inputSource, std::size_t& position, bool& isEmptyElement) noexcept {
    if (inputSource == nullptr || inputSource->CharAt(position) != '<') {
        return false;
    }

    ++position;
    if (!TryConsumeNameFromInputSourceAt(inputSource, position)) {
        return false;
    }

    while (true) {
        const char ch = inputSource->CharAt(position);
        if (ch == '\0') {
            return false;
        }

        if (ch == '"' || ch == '\'') {
            const auto quoteEnd = ScanQuotedValueEndAt(position, [inputSource](std::size_t probe) noexcept {
                return inputSource->CharAt(probe);
            });
            if (quoteEnd == std::string::npos) {
                return false;
            }
            position = quoteEnd + 1;
            continue;
        }

        if (inputSource->CharAt(position) == '/' && inputSource->CharAt(position + 1) == '>') {
            position += 2;
            isEmptyElement = true;
            return true;
        }

        if (ch == '>') {
            ++position;
            isEmptyElement = false;
            return true;
        }

        ++position;
    }
}

bool StartsWithAt(const XmlReaderInputSource* inputSource, std::size_t position, std::string_view token) noexcept {
    if (inputSource == nullptr) {
        return false;
    }

    for (std::size_t index = 0; index < token.size(); ++index) {
        if (inputSource->CharAt(position + index) != token[index]) {
            return false;
        }
    }

    return true;
}

std::size_t ScanDocumentTypeInternalSubsetEndAt(const XmlReaderInputSource* inputSource, std::size_t contentStart) noexcept {
    if (inputSource == nullptr) {
        return std::string::npos;
    }

    std::size_t position = contentStart;
    int bracketDepth = 1;
    bool inQuote = false;
    char quote = '\0';

    while (bracketDepth > 0) {
        const char ch = inputSource->CharAt(position);
        if (ch == '\0') {
            break;
        }

        ++position;
        if (inQuote) {
            if (ch == quote) {
                inQuote = false;
            }
            continue;
        }

        if (ch == '"' || ch == '\'') {
            inQuote = true;
            quote = ch;
        } else if (ch == '[') {
            ++bracketDepth;
        } else if (ch == ']') {
            --bracketDepth;
        }
    }

    return bracketDepth == 0 ? position - 1 : std::string::npos;
}

std::size_t ScanQuotedValueEndAt(const XmlReaderInputSource* inputSource, std::size_t quoteStart) noexcept {
    if (inputSource == nullptr) {
        return std::string::npos;
    }

    if (const auto* stringSource = dynamic_cast<const StringXmlReaderInputSource*>(inputSource); stringSource != nullptr) {
        return stringSource->ScanQuotedValueEnd(quoteStart);
    }
    if (const auto* streamSource = dynamic_cast<const StreamXmlReaderInputSource*>(inputSource); streamSource != nullptr) {
        return streamSource->ScanQuotedValueEnd(quoteStart);
    }
    if (const auto* subrangeSource = dynamic_cast<const SubrangeXmlReaderInputSource*>(inputSource); subrangeSource != nullptr) {
        return subrangeSource->ScanQuotedValueEnd(quoteStart);
    }

    return ScanQuotedValueEndAt(quoteStart, [inputSource](std::size_t probe) noexcept {
        return inputSource->CharAt(probe);
    });
}

std::size_t ScanDelimitedSectionEndAt(
    const XmlReaderInputSource* inputSource,
    std::size_t contentStart,
    std::string_view terminator) noexcept {
    if (inputSource == nullptr) {
        return std::string::npos;
    }

    return ScanDelimitedSectionEndAt(contentStart, terminator, [inputSource](std::string_view token, std::size_t position) noexcept {
        return inputSource->Find(std::string(token), position);
    });
}

struct XmlQuotedValueToken {
    std::string rawValue;
    bool valid = false;
};

XmlQuotedValueToken ParseQuotedLiteralFromInputSourceAt(const XmlReaderInputSource* inputSource, std::size_t& position) {
    XmlQuotedValueToken token;
    if (inputSource == nullptr) {
        return token;
    }

    const char quote = inputSource->CharAt(position);
    const auto quoteEnd = ScanQuotedValueEndAt(position, [inputSource](std::size_t probe) noexcept {
        return inputSource->CharAt(probe);
    });
    if ((quote != '"' && quote != '\'') || quoteEnd == std::string::npos) {
        return token;
    }

    token.rawValue = inputSource->Slice(position + 1, quoteEnd - position - 1);
    position = quoteEnd + 1;
    token.valid = true;
    return token;
}

struct XmlEntityReferenceToken {
    std::string raw;
    std::string name;
    std::size_t end = std::string::npos;
};

XmlEntityReferenceToken ScanEntityReferenceFromInputSourceAt(const XmlReaderInputSource* inputSource, std::size_t ampersandPosition) {
    if (inputSource == nullptr) {
        return {};
    }

    const auto semicolon = inputSource->Find(";", ampersandPosition);
    if (semicolon == std::string::npos) {
        return {};
    }

    return {
        inputSource->Slice(ampersandPosition, semicolon - ampersandPosition + 1),
        inputSource->Slice(ampersandPosition + 1, semicolon - ampersandPosition - 1),
        semicolon + 1};
}

class XmlReaderTokenizer final {
public:
    using AttributeAssignmentToken = XmlAttributeAssignmentToken;
    using QuotedValueToken = XmlQuotedValueToken;
    using EntityReferenceToken = XmlEntityReferenceToken;

    explicit XmlReaderTokenizer(std::shared_ptr<const XmlReaderInputSource> inputSource)
                : inputSource_(std::move(inputSource)),
                    stringInputSource_(dynamic_cast<const StringXmlReaderInputSource*>(inputSource_.get())),
                    streamInputSource_(dynamic_cast<const StreamXmlReaderInputSource*>(inputSource_.get())),
                    subrangeInputSource_(dynamic_cast<const SubrangeXmlReaderInputSource*>(inputSource_.get())) {
    }

    bool HasChar(std::size_t position) const noexcept {
        return CharAt(position) != '\0';
    }

    char CharAt(std::size_t position) const noexcept {
        return inputSource_ == nullptr ? '\0' : inputSource_->CharAt(position);
    }

    const char* PtrAt(std::size_t position, std::size_t& available) const noexcept {
        available = 0;
        return inputSource_ == nullptr ? nullptr : inputSource_->PtrAt(position, available);
    }

    bool StartsWith(std::size_t position, std::string_view token) const noexcept {
        return StartsWithAt(inputSource_.get(), position, token);
    }

    std::size_t Find(const std::string& token, std::size_t position) const noexcept {
        return inputSource_ == nullptr ? std::string::npos : inputSource_->Find(token, position);
    }

    std::size_t ScanQuotedValueEnd(std::size_t quoteStart) const noexcept {
        return ScanQuotedValueEndAt(inputSource_.get(), quoteStart);
    }

    std::size_t ScanDelimitedSectionEnd(std::size_t contentStart, std::string_view terminator) const noexcept {
        return ScanDelimitedSectionEndAt(inputSource_.get(), contentStart, terminator);
    }

    std::size_t ScanDocumentTypeInternalSubsetEnd(std::size_t contentStart) const noexcept {
        return ScanDocumentTypeInternalSubsetEndAt(inputSource_.get(), contentStart);
    }

    XmlMarkupKind ClassifyMarkup(std::size_t position) const noexcept {
        return ClassifyXmlMarkupWithCharAt(position, [this](std::size_t probe) noexcept {
            return CharAt(probe);
        });
    }

    std::string ParseNameAt(std::size_t& position) const {
        return ParseNameFromInputSourceAt(inputSource_.get(), position);
    }

    bool SkipTag(std::size_t& position, bool& isEmptyElement) const {
        return SkipTagFromInputSourceAt(inputSource_.get(), position, isEmptyElement);
    }

    void SkipWhitespace(std::size_t& position) const noexcept {
        SkipXmlWhitespaceAt(inputSource_.get(), position);
    }

    bool ConsumeStartTagClose(std::size_t& position, bool& isEmptyElement) const noexcept {
        return ConsumeXmlStartTagCloseAt(
            position,
            isEmptyElement,
            [this](std::size_t probe) noexcept {
                return CharAt(probe);
            },
            [this](std::size_t probe, std::string_view token) noexcept {
                return StartsWith(probe, token);
            });
    }

    bool ConsumeEndTagClose(std::size_t& position) const noexcept {
        return ConsumeXmlEndTagCloseAt(
            position,
            [this](std::size_t& probe) noexcept {
                SkipWhitespace(probe);
            },
            [this](std::size_t probe) noexcept {
                return CharAt(probe);
            });
    }

    AttributeAssignmentToken ParseAttributeAssignment(std::size_t& position) const {
        return ParseXmlAttributeAssignmentAt(
            position,
            [this](std::size_t& probe) {
                return ParseNameAt(probe);
            },
            [this](std::size_t& probe) noexcept {
                SkipWhitespace(probe);
            },
            [this](std::size_t probe) noexcept {
                return CharAt(probe);
            },
            [this](std::size_t probe) noexcept {
                return ScanQuotedValueEnd(probe);
            });
    }

    QuotedValueToken ParseQuotedLiteral(std::size_t& position) const {
        return ParseQuotedLiteralFromInputSourceAt(inputSource_.get(), position);
    }

    EntityReferenceToken ScanEntityReference(std::size_t ampersandPosition) const {
        return ScanEntityReferenceFromInputSourceAt(inputSource_.get(), ampersandPosition);
    }

    std::string Slice(std::size_t start, std::size_t count = std::string::npos) const {
        return inputSource_ == nullptr ? std::string{} : inputSource_->Slice(start, count);
    }

private:
    std::shared_ptr<const XmlReaderInputSource> inputSource_;
    const StringXmlReaderInputSource* stringInputSource_ = nullptr;
    const StreamXmlReaderInputSource* streamInputSource_ = nullptr;
    const SubrangeXmlReaderInputSource* subrangeInputSource_ = nullptr;
};

XmlReader::XmlReader(XmlReaderSettings settings) : settings_(std::move(settings)) {
}

char XmlReader::SourceCharAt(std::size_t position) const noexcept {
    return inputSource_ == nullptr ? '\0' : inputSource_->CharAt(position);
}

const char* XmlReader::SourcePtrAt(std::size_t position, std::size_t& available) const noexcept {
    available = 0;
    return inputSource_ == nullptr ? nullptr : inputSource_->PtrAt(position, available);
}

bool XmlReader::HasSourceChar(std::size_t position) const noexcept {
    return SourceCharAt(position) != '\0';
}

std::size_t XmlReader::FindInSource(const std::string& token, std::size_t position) const noexcept {
    return inputSource_ == nullptr ? std::string::npos : inputSource_->Find(token, position);
}

std::string XmlReader::SourceSubstr(std::size_t start, std::size_t count) const {
    return inputSource_ == nullptr ? std::string{} : inputSource_->Slice(start, count);
}

bool XmlReader::SourceRangeContains(std::size_t start, std::size_t end, char value) const noexcept {
    if (inputSource_ == nullptr || end <= start) {
        return false;
    }

    for (std::size_t index = start; index < end && HasSourceChar(index); ++index) {
        if (SourceCharAt(index) == value) {
            return true;
        }
    }

    return false;
}

void XmlReader::AppendSourceSubstrTo(std::string& target, std::size_t start, std::size_t count) const {
    if (inputSource_ == nullptr) {
        return;
    }
    inputSource_->AppendSliceTo(target, start, count);
}

std::size_t XmlReader::AppendDecodedSourceRangeTo(std::string& target, std::size_t start, std::size_t end) const {
    if (inputSource_ == nullptr || end <= start) {
        return 0;
    }

    const std::size_t originalSize = target.size();
    target.reserve(originalSize + (end - start));
    XmlReaderTokenizer tokenizer(inputSource_);

    if (const auto* stringSource = dynamic_cast<const StringXmlReaderInputSource*>(inputSource_.get()); stringSource != nullptr) {
        const std::string& source = *stringSource->Text();
        
        if (entityDeclarations_.empty()) {
            target.resize(originalSize + (end - start));
            char* out = target.data() + originalSize;
            const char* in = source.data() + start;
            const char* const inEnd = source.data() + end;

            while (in < inEnd) {
                const char* ampersand = static_cast<const char*>(std::memchr(in, '&', inEnd - in));
                if (!ampersand) {
                    const std::size_t len = inEnd - in;
                    std::memcpy(out, in, len);
                    out += len;
                    break;
                }

                const std::size_t len = ampersand - in;
                if (len > 0) {
                    std::memcpy(out, in, len);
                    out += len;
                }
                in = ampersand;

                const char* semicolon = static_cast<const char*>(std::memchr(in + 1, ';', inEnd - (in + 1)));
                if (!semicolon) {
                    Throw("Unterminated entity reference");
                }

                const std::string_view entity(in + 1, semicolon - (in + 1));
                const auto predefined = ResolvePredefinedEntityReferenceValueView(entity);
                if (!predefined.empty()) {
                    std::memcpy(out, predefined.data(), predefined.size());
                    out += predefined.size();
                } else if (!entity.empty() && entity.front() == '#') {
                    unsigned int codePoint = 0;
                    if (!TryParseNumericEntityReferenceCodePoint(entity, codePoint)) {
                        Throw("Invalid numeric entity reference: &" + std::string(entity) + ';');
                    }
                    if (codePoint <= 0x7F) {
                        *out++ = static_cast<char>(codePoint);
                    } else if (codePoint <= 0x7FF) {
                        *out++ = static_cast<char>(0xC0 | (codePoint >> 6));
                        *out++ = static_cast<char>(0x80 | (codePoint & 0x3F));
                    } else if (codePoint <= 0xFFFF) {
                        *out++ = static_cast<char>(0xE0 | (codePoint >> 12));
                        *out++ = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                        *out++ = static_cast<char>(0x80 | (codePoint & 0x3F));
                    } else {
                        *out++ = static_cast<char>(0xF0 | (codePoint >> 18));
                        *out++ = static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
                        *out++ = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                        *out++ = static_cast<char>(0x80 | (codePoint & 0x3F));
                    }
                } else {
                    Throw("Unknown entity reference: &" + std::string(entity) + ';');
                }
                in = semicolon + 1;
            }
            
            const std::size_t finalLen = out - target.data();
            target.resize(finalLen);
            const std::size_t appendedBytes = finalLen - originalSize;
            if (settings_.MaxCharactersFromEntities != 0) {
                const_cast<XmlReader*>(this)->entityCharactersRead_ += appendedBytes;
                if (const_cast<XmlReader*>(this)->entityCharactersRead_ > settings_.MaxCharactersFromEntities) {
                    Throw("The XML document exceeds the configured MaxCharactersFromEntities limit");
                }
            }
            return appendedBytes;
        }

        std::size_t cursor = start;
        while (cursor < end) {
            const std::size_t ampersand = tokenizer.Find("&", cursor);
            if (ampersand == std::string::npos || ampersand >= end) {
                target.append(source, cursor, end - cursor);
                break;
            }

            if (ampersand > cursor) {
                target.append(source, cursor, ampersand - cursor);
            }

            const auto entityToken = tokenizer.ScanEntityReference(ampersand);
            if (entityToken.end == std::string::npos || entityToken.end > end) {
                Throw("Unterminated entity reference");
            }

            const std::string_view entity(source.data() + ampersand + 1, entityToken.end - ampersand - 2);
            const auto predefined = ResolvePredefinedEntityReferenceValueView(entity);
            if (!predefined.empty()) {
                target.append(predefined.data(), predefined.size());
            } else if (!entity.empty() && entity.front() == '#') {
                unsigned int codePoint = 0;
                if (!TryParseNumericEntityReferenceCodePoint(entity, codePoint)) {
                    Throw("Invalid numeric entity reference: &" + std::string(entity) + ';');
                }
                AppendCodePointUtf8(target, codePoint);
            } else {
                const std::string entityName(entity);
                if (const auto found = entityDeclarations_.find(entityName); found != entityDeclarations_.end()) {
                    DecodeEntityTextTo(
                        target,
                        found->second,
                        [this](const std::string& nestedEntity) -> std::optional<std::string> {
                            const auto resolved = entityDeclarations_.find(nestedEntity);
                            return resolved == entityDeclarations_.end() ? std::nullopt : std::optional<std::string>(resolved->second);
                        },
                        [this](const std::string& message) {
                            Throw(message);
                        });
                } else {
                    Throw("Unknown entity reference: &" + entityName + ';');
                }
            }

            cursor = entityToken.end;
        }

        const std::size_t appendedBytes = target.size() - originalSize;
        if (settings_.MaxCharactersFromEntities != 0) {
            const_cast<XmlReader*>(this)->entityCharactersRead_ += appendedBytes;
            if (const_cast<XmlReader*>(this)->entityCharactersRead_ > settings_.MaxCharactersFromEntities) {
                Throw("The XML document exceeds the configured MaxCharactersFromEntities limit");
            }
        }

        return appendedBytes;
    }

    std::size_t cursor = start;
    while (cursor < end) {
        std::size_t ampersand = cursor;
        while (ampersand < end && SourceCharAt(ampersand) != '&') {
            ++ampersand;
        }
        if (ampersand > cursor) {
            AppendSourceSubstrTo(target, cursor, ampersand - cursor);
            cursor = ampersand;
            continue;
        }

        std::size_t semicolon = ampersand + 1;
        while (semicolon < end && SourceCharAt(semicolon) != ';') {
            ++semicolon;
        }
        if (semicolon >= end || SourceCharAt(semicolon) != ';') {
            Throw("Unterminated entity reference");
        }

        std::string entity;
        entity.reserve(semicolon - ampersand - 1);
        for (std::size_t probe = ampersand + 1; probe < semicolon; ++probe) {
            entity.push_back(SourceCharAt(probe));
        }

        const auto predefined = ResolvePredefinedEntityReferenceValueView(entity);
        if (!predefined.empty()) {
            target.append(predefined.data(), predefined.size());
        } else if (!entity.empty() && entity.front() == '#') {
            unsigned int codePoint = 0;
            if (!TryParseNumericEntityReferenceCodePoint(entity, codePoint)) {
                Throw("Invalid numeric entity reference: &" + entity + ';');
            }
            AppendCodePointUtf8(target, codePoint);
        } else if (const auto found = entityDeclarations_.find(entity); found != entityDeclarations_.end()) {
            DecodeEntityTextTo(
                target,
                found->second,
                [this](const std::string& nestedEntity) -> std::optional<std::string> {
                    const auto resolved = entityDeclarations_.find(nestedEntity);
                    return resolved == entityDeclarations_.end() ? std::nullopt : std::optional<std::string>(resolved->second);
                },
                [this](const std::string& message) {
                    Throw(message);
                });
        } else {
            Throw("Unknown entity reference: &" + entity + ';');
        }

        cursor = semicolon + 1;
    }

    const std::size_t appendedBytes = target.size() - originalSize;
    if (settings_.MaxCharactersFromEntities != 0) {
        const_cast<XmlReader*>(this)->entityCharactersRead_ += appendedBytes;
        if (const_cast<XmlReader*>(this)->entityCharactersRead_ > settings_.MaxCharactersFromEntities) {
            Throw("The XML document exceeds the configured MaxCharactersFromEntities limit");
        }
    }

    return appendedBytes;
}

void XmlReader::AppendCurrentValueTo(std::string& target) const {
    if (!currentValue_.empty()) {
        target += currentValue_;
        return;
    }

    if (currentValueStart_ != std::string::npos
        && currentValueEnd_ != std::string::npos
        && currentValueEnd_ >= currentValueStart_) {
        AppendSourceSubstrTo(target, currentValueStart_, currentValueEnd_ - currentValueStart_);
        return;
    }

    target += Value();
}

void XmlReader::DecodeAndAppendCurrentBase64(std::vector<unsigned char>& buffer, unsigned int& accumulator, int& bits) const {
    if (!currentValue_.empty()) {
        AppendDecodedBase64Chunk(currentValue_, buffer, accumulator, bits);
        return;
    }

    if (currentValueStart_ != std::string::npos
        && currentValueEnd_ != std::string::npos
        && currentValueEnd_ >= currentValueStart_) {
        for (std::size_t index = currentValueStart_; index < currentValueEnd_; ++index) {
            const char ch = SourceCharAt(index);
            if (IsWhitespace(ch) || ch == '=') {
                continue;
            }

            const int value = DecodeBase64Char(ch);
            if (value < 0) {
                continue;
            }

            accumulator = (accumulator << 6) | static_cast<unsigned int>(value);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                buffer.push_back(static_cast<unsigned char>((accumulator >> bits) & 0xFF));
            }
        }
        return;
    }

    AppendDecodedBase64Chunk(Value(), buffer, accumulator, bits);
}

std::size_t XmlReader::EarliestRetainedSourceOffset() const noexcept {
    std::size_t earliest = position_;

    auto includeOffset = [&earliest](std::size_t offset) {
        if (offset != std::string::npos && offset < earliest) {
            earliest = offset;
        }
    };

    auto includeValueRange = [&includeOffset](const std::string& value, std::size_t valueStart, std::size_t valueEnd) {
        if (valueStart != std::string::npos && valueEnd != std::string::npos && valueEnd >= valueStart && value.empty()) {
            includeOffset(valueStart);
        }
    };

    includeValueRange(currentValue_, currentValueStart_, currentValueEnd_);

    if (currentNodeType_ == XmlNodeType::Element) {
        if ((currentOuterXml_.empty() || (!currentIsEmptyElement_ && currentInnerXml_.empty()))
            && currentElementStart_ != std::string::npos) {
            includeOffset(currentElementStart_);
        }
        includeOffset(currentEarliestRetainedAttributeValueStart_);
    } else if (currentOuterXml_.empty() && currentNodeStart_ != std::string::npos && currentNodeEnd_ != std::string::npos) {
        includeOffset(currentNodeStart_);
    }

    for (const auto& node : bufferedNodes_) {
        includeValueRange(node.value, node.valueStart, node.valueEnd);
        if (node.nodeType == XmlNodeType::Element) {
            if ((node.outerXml.empty() || (!node.isEmptyElement && node.innerXml.empty()))
                && node.elementStart != std::string::npos) {
                includeOffset(node.elementStart);
            }
        } else if (node.outerXml.empty() && node.nodeStart != std::string::npos && node.nodeEnd != std::string::npos) {
            includeOffset(node.nodeStart);
        }
    }

    return earliest;
}

void XmlReader::MaybeDiscardSourcePrefix() const {
    if (inputSource_ == nullptr) {
        return;
    }

    const std::size_t discardBefore = EarliestRetainedSourceOffset();
    if (discardBefore <= discardedSourceOffset_) {
        return;
    }

    std::size_t line = 0;
    std::size_t column = 0;
    if (discardBefore == position_) {
        line = lineNumber_;
        column = linePosition_;
    } else {
        const auto computed = ComputeLineColumn(discardBefore);
        line = computed.first;
        column = computed.second;
    }
    inputSource_->DiscardBefore(discardBefore);
    discardedSourceOffset_ = discardBefore;
    discardedLineNumber_ = line;
    discardedLinePosition_ = column;
}

void XmlReader::FinalizeSuccessfulRead() {
    eof_ = false;
    MaybeDiscardSourcePrefix();
}

char XmlReader::Peek() const noexcept {
    return SourceCharAt(position_);
}

char XmlReader::ReadChar() {
    if (!HasSourceChar(position_)) {
        Throw("Unexpected end of XML document");
    }

    const char ch = SourceCharAt(position_++);
    ++totalCharactersRead_;
    if (settings_.MaxCharactersInDocument != 0 && totalCharactersRead_ > settings_.MaxCharactersInDocument) {
        Throw("The XML document exceeds the configured MaxCharactersInDocument limit");
    }
    if (ch == '\n') {
        ++lineNumber_;
        linePosition_ = 1;
    } else {
        ++linePosition_;
    }

    return ch;
}

bool XmlReader::StartsWith(const std::string& token) const noexcept {
    for (std::size_t index = 0; index < token.size(); ++index) {
        if (SourceCharAt(position_ + index) != token[index]) {
            return false;
        }
    }
    return true;
}

void XmlReader::SkipWhitespace() {
    while (HasSourceChar(position_) && IsWhitespace(SourceCharAt(position_))) {
        if (SourceCharAt(position_) == '\n') {
            ++lineNumber_;
            linePosition_ = 1;
        } else {
            ++linePosition_;
        }
        ++position_;
    }
}

std::string XmlReader::ParseName() {
    if (!IsNameStartChar(Peek())) {
        Throw("Invalid XML name");
    }

    const auto start = position_++;
    while (HasSourceChar(position_) && IsNameChar(SourceCharAt(position_))) {
        ++position_;
    }

    linePosition_ += position_ - start;

    return SourceSubstr(start, position_ - start);
}

std::string XmlReader::DecodeEntities(const std::string& value) const {
    const std::string decoded = DecodeEntityText(
        value,
        [this](const std::string& entity) -> std::optional<std::string> {
            const auto found = entityDeclarations_.find(entity);
            return found == entityDeclarations_.end() ? std::nullopt : std::optional<std::string>(found->second);
        },
        [this](const std::string& message) {
            Throw(message);
        });
    if (settings_.MaxCharactersFromEntities != 0) {
        const_cast<XmlReader*>(this)->entityCharactersRead_ += decoded.size();
        if (const_cast<XmlReader*>(this)->entityCharactersRead_ > settings_.MaxCharactersFromEntities) {
            Throw("The XML document exceeds the configured MaxCharactersFromEntities limit");
        }
    }
    return decoded;
}

void XmlReader::QueueNode(
    XmlNodeType nodeType,
    std::string name,
    std::string namespaceUri,
    std::string value,
    int depth,
    bool isEmptyElement,
    std::string innerXml,
    std::string outerXml,
    std::size_t valueStart,
    std::size_t valueEnd,
    std::size_t nodeStart,
    std::size_t nodeEnd,
    std::vector<std::pair<std::string, std::string>> attributes,
    std::vector<std::string> attributeNamespaceUris,
    std::size_t elementStart,
    std::size_t contentStart,
    std::size_t closeStart,
    std::size_t closeEnd) {
    bufferedNodes_.push_back(BufferedNode{
        nodeType,
        std::move(name),
        std::move(namespaceUri),
        std::move(value),
        valueStart,
        valueEnd,
        depth,
        isEmptyElement,
        std::move(innerXml),
        std::move(outerXml),
        nodeStart,
        nodeEnd,
        std::move(attributes),
        std::move(attributeNamespaceUris),
        elementStart,
        contentStart,
        closeStart,
        closeEnd});
}

bool XmlReader::TryConsumeBufferedNode() {
    if (bufferedNodes_.empty()) {
        return false;
    }

    auto node = std::move(bufferedNodes_.front());
    bufferedNodes_.pop_front();
    SetCurrentNode(
        node.nodeType,
        std::move(node.name),
        std::move(node.namespaceUri),
        std::move(node.value),
        node.depth,
        node.isEmptyElement,
        std::move(node.innerXml),
        std::move(node.outerXml),
        node.valueStart,
        node.valueEnd,
        node.nodeStart,
        node.nodeEnd,
        std::move(node.attributes),
        std::move(node.attributeNamespaceUris),
        node.elementStart,
        node.contentStart,
        node.closeStart,
        node.closeEnd);
    return true;
}

std::string XmlReader::ParseQuotedValue(bool decodeEntities) {
    XmlReaderTokenizer tokenizer(inputSource_);
    const char quote = ReadChar();
    if (quote != '\"' && quote != '\'') {
        Throw("Expected quoted value");
    }

    const auto start = position_;
    const auto quoteEnd = tokenizer.ScanQuotedValueEnd(start - 1);
    if (quoteEnd == std::string::npos) {
        Throw("Unterminated quoted value");
    }

    const auto raw = tokenizer.Slice(start, quoteEnd - start);
    position_ = quoteEnd + 1;
    return decodeEntities ? DecodeEntities(raw) : raw;
}

void XmlReader::ResetCurrentNode() {
    currentNodeType_ = XmlNodeType::None;
    currentName_.clear();
    currentNamespaceUri_.clear();
    currentValue_.clear();
    currentInnerXml_.clear();
    currentOuterXml_.clear();
    currentAttributes_.clear();
    currentAttributeNamespaceUrisResolved_.clear();
    currentAttributeValueMetadata_.clear();
    currentAttributeNamespaceUris_.clear();
    currentLocalNamespaceDeclarations_.clear();
    currentEarliestRetainedAttributeValueStart_ = std::string::npos;
    currentDeclarationVersion_.clear();
    currentDeclarationEncoding_.clear();
    currentDeclarationStandalone_.clear();
    currentValueStart_ = std::string::npos;
    currentValueEnd_ = std::string::npos;
    currentDepth_ = 0;
    currentIsEmptyElement_ = false;
    currentNodeStart_ = std::string::npos;
    currentNodeEnd_ = std::string::npos;
    currentElementStart_ = std::string::npos;
    currentContentStart_ = std::string::npos;
    currentCloseStart_ = std::string::npos;
    currentCloseEnd_ = std::string::npos;
}

void XmlReader::SetCurrentNode(
    XmlNodeType nodeType,
    std::string name,
    std::string namespaceUri,
    std::string value,
    int depth,
    bool isEmptyElement,
    std::string innerXml,
    std::string outerXml,
    std::size_t valueStart,
    std::size_t valueEnd,
    std::size_t nodeStart,
    std::size_t nodeEnd,
    std::vector<std::pair<std::string, std::string>> attributes,
    std::vector<std::string> attributeNamespaceUris,
    std::size_t elementStart,
    std::size_t contentStart,
    std::size_t closeStart,
    std::size_t closeEnd) {
    if (!name.empty()) {
        nameTable_.Add(name);
    }
    if (!namespaceUri.empty()) {
        nameTable_.Add(namespaceUri);
    }
    for (const auto& attribute : attributes) {
        if (!attribute.first.empty()) {
            nameTable_.Add(attribute.first);
        }
    }
    for (const auto& attributeNamespaceUri : attributeNamespaceUris) {
        if (!attributeNamespaceUri.empty()) {
            nameTable_.Add(attributeNamespaceUri);
        }
    }
    currentNodeType_ = nodeType;
    currentName_ = std::move(name);
    currentNamespaceUri_ = std::move(namespaceUri);
    currentValue_ = std::move(value);
    currentDepth_ = depth;
    currentIsEmptyElement_ = isEmptyElement;
    currentInnerXml_ = std::move(innerXml);
    currentOuterXml_ = std::move(outerXml);
    currentAttributes_ = std::move(attributes);
    currentAttributeNamespaceUrisResolved_.assign(attributeNamespaceUris.size(), static_cast<unsigned char>(1));
    currentAttributeValueMetadata_.clear();
    currentAttributeNamespaceUris_ = std::move(attributeNamespaceUris);
    currentLocalNamespaceDeclarations_.clear();
    currentDeclarationVersion_.clear();
    currentDeclarationEncoding_.clear();
    currentDeclarationStandalone_.clear();
    currentValueStart_ = valueStart;
    currentValueEnd_ = valueEnd;
    currentNodeStart_ = nodeStart;
    currentNodeEnd_ = nodeEnd;
    currentElementStart_ = elementStart;
    currentContentStart_ = contentStart;
    currentCloseStart_ = closeStart;
    currentCloseEnd_ = closeEnd;
}

const XmlNameTable& XmlReader::NameTable() const noexcept {
    return nameTable_;
}

std::pair<std::size_t, std::size_t> XmlReader::ComputeLineColumn(std::size_t position) const noexcept {
    std::size_t scanStart = 0;
    std::size_t line = 1;
    std::size_t column = 1;
    if (position >= discardedSourceOffset_) {
        scanStart = discardedSourceOffset_;
        line = discardedLineNumber_;
        column = discardedLinePosition_;
    }

    if (position > scanStart && inputSource_ != nullptr) {
        const std::size_t firstNewline = FindFirstOfFromInputSource(inputSource_.get(), "\n", scanStart);
        if (firstNewline == std::string::npos || firstNewline >= position) {
            return {line, column + (position - scanStart)};
        }
    }

    for (std::size_t index = scanStart; index < position && HasSourceChar(index); ++index) {
        if (SourceCharAt(index) == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    return {line, column};
}

[[noreturn]] void XmlReader::Throw(const std::string& message) const {
    const auto [line, column] = ComputeLineColumn(position_);
    throw XmlException(message, line, column);
}

void XmlReader::ParseDeclaration() {
    XmlReaderTokenizer tokenizer(inputSource_);
    const auto start = position_;
    if (!xmlDeclarationAllowed_) {
        position_ += 5;
        linePosition_ += 5;
        Throw("XML declaration is only allowed at the beginning of the document");
    }
    position_ += 5;
    linePosition_ += 5;

    std::string version = "1.0";
    std::string encoding;
    std::string standalone;

    while (true) {
        SkipWhitespace();
        if (tokenizer.StartsWith(position_, "?>")) {
            position_ += 2;
            linePosition_ += 2;
            break;
        }

        const auto attributeStart = position_;
        std::size_t cursor = position_;
        const auto attributeToken = tokenizer.ParseAttributeAssignment(cursor);
        position_ = cursor;
        AdvanceLineInfoFromInputSource(inputSource_.get(), attributeStart, position_, lineNumber_, linePosition_);
        if (attributeToken.name.empty()) {
            Throw("Invalid XML name");
        }

        if (!attributeToken.valid) {
            if (!attributeToken.sawEquals) {
                if (tokenizer.HasChar(position_)) {
                    ++linePosition_;
                    ++position_;
                }
                Throw("Expected '=' in XML declaration");
            }
            if (tokenizer.HasChar(position_)) {
                ++linePosition_;
                ++position_;
            }
            Throw("Expected quoted value");
        }

        const std::string name = attributeToken.name;
        const std::string value = DecodeEntities(SourceSubstr(attributeToken.rawValueStart, attributeToken.rawValueEnd - attributeToken.rawValueStart));

        if (name == "version") {
            version = value;
        } else if (name == "encoding") {
            encoding = value;
        } else if (name == "standalone") {
            standalone = value;
        } else {
            Throw("Unsupported XML declaration attribute: " + name);
        }
    }

    XmlDeclaration declaration(version, encoding, standalone);
    SetCurrentNode(
        XmlNodeType::XmlDeclaration,
        "xml",
        {},
        {},
        0,
        false,
        {},
        {},
        std::string::npos,
        std::string::npos,
        start,
        position_);
    currentDeclarationVersion_ = declaration.Version();
    currentDeclarationEncoding_ = declaration.Encoding();
    currentDeclarationStandalone_ = declaration.Standalone();
    xmlDeclarationAllowed_ = false;
}

void XmlReader::ParseDocumentType() {
    XmlReaderTokenizer tokenizer(inputSource_);
    if (settings_.DtdProcessing == DtdProcessing::Prohibit) {
        Throw("DTD is prohibited in this XML document");
    }

    const auto start = position_;
    position_ += 9;
    linePosition_ += 9;
    SkipWhitespace();

    const std::string name = ParseName();
    SkipWhitespace();

    std::string publicId;
    std::string systemId;
    std::string internalSubset;

    entityDeclarations_.clear();
    declaredEntityNames_.clear();
    notationDeclarationNames_.clear();
    unparsedEntityDeclarationNames_.clear();
    externalEntitySystemIds_.clear();

    if (tokenizer.StartsWith(position_, "PUBLIC")) {
        position_ += 6;
        linePosition_ += 6;
        SkipWhitespace();
        const auto publicLiteralStart = position_;
        std::size_t cursor = position_;
        auto publicIdToken = tokenizer.ParseQuotedLiteral(cursor);
        position_ = cursor;
        AdvanceLineInfoFromInputSource(inputSource_.get(), publicLiteralStart, position_, lineNumber_, linePosition_);
        if (!publicIdToken.valid) {
            if (tokenizer.HasChar(position_)) {
                ++linePosition_;
                ++position_;
            }
            Throw("Expected quoted value");
        }
        publicId = publicIdToken.rawValue;

        const auto betweenIdsStart = position_;
        tokenizer.SkipWhitespace(position_);
        AdvanceLineInfoFromInputSource(inputSource_.get(), betweenIdsStart, position_, lineNumber_, linePosition_);
        const auto systemLiteralStart = position_;
        cursor = position_;
        auto systemIdToken = tokenizer.ParseQuotedLiteral(cursor);
        position_ = cursor;
        AdvanceLineInfoFromInputSource(inputSource_.get(), systemLiteralStart, position_, lineNumber_, linePosition_);
        if (!systemIdToken.valid) {
            if (tokenizer.HasChar(position_)) {
                ++linePosition_;
                ++position_;
            }
            Throw("Expected quoted value");
        }
        systemId = systemIdToken.rawValue;
        SkipWhitespace();
    } else if (tokenizer.StartsWith(position_, "SYSTEM")) {
        position_ += 6;
        linePosition_ += 6;
        SkipWhitespace();
        const auto systemLiteralStart = position_;
        std::size_t cursor = position_;
        auto systemIdToken = tokenizer.ParseQuotedLiteral(cursor);
        position_ = cursor;
        AdvanceLineInfoFromInputSource(inputSource_.get(), systemLiteralStart, position_, lineNumber_, linePosition_);
        if (!systemIdToken.valid) {
            if (tokenizer.HasChar(position_)) {
                ++linePosition_;
                ++position_;
            }
            Throw("Expected quoted value");
        }
        systemId = systemIdToken.rawValue;
        SkipWhitespace();
    }

    if (Peek() == '[') {
        ++position_;
        ++linePosition_;
        const auto subsetStart = position_;
        const auto subsetEnd = tokenizer.ScanDocumentTypeInternalSubsetEnd(subsetStart);
        if (subsetEnd == std::string::npos) {
            Throw("Unterminated DOCTYPE internal subset");
        }
        position_ = subsetEnd + 1;
        AdvanceLineInfoFromInputSource(inputSource_.get(), subsetStart, position_, lineNumber_, linePosition_);
        internalSubset = Trim(SourceSubstr(subsetStart, subsetEnd - subsetStart));
        if (settings_.DtdProcessing == DtdProcessing::Parse) {
            std::vector<std::shared_ptr<XmlNode>> entities;
            std::vector<std::shared_ptr<XmlNode>> notations;
            ParseDocumentTypeInternalSubset(internalSubset, entities, notations);
            PopulateInternalEntityDeclarations(entities, entityDeclarations_);
            for (const auto& entity : entities) {
                if (entity != nullptr) {
                    declaredEntityNames_.insert(entity->Name());
                    const auto typedEntity = std::dynamic_pointer_cast<XmlEntity>(entity);
                    if (typedEntity != nullptr) {
                        if (!typedEntity->SystemId().empty()) {
                            externalEntitySystemIds_[typedEntity->Name()] = typedEntity->SystemId();
                        }
                        if (!typedEntity->NotationName().empty()) {
                            unparsedEntityDeclarationNames_.insert(typedEntity->Name());
                        }
                    }
                }
            }
            for (const auto& notation : notations) {
                if (notation != nullptr) {
                    notationDeclarationNames_.insert(notation->Name());
                }
            }
        }
        SkipWhitespace();
    }

    if (ReadChar() != '>') {
        Throw("Expected '>' after DOCTYPE");
    }

    SetCurrentNode(
        XmlNodeType::DocumentType,
        name,
        {},
        {},
        static_cast<int>(elementStack_.size()),
        false,
        {},
        {},
        std::string::npos,
        std::string::npos,
        start,
        position_);
    sawDocumentType_ = true;
}

void XmlReader::ParseProcessingInstruction() {
    XmlReaderTokenizer tokenizer(inputSource_);
    const auto start = position_;
    position_ += 2;
    linePosition_ += 2;
    const std::string target = ParseName();
    std::size_t dataStart = std::string::npos;
    std::size_t dataEnd = std::string::npos;
    if (!StartsWith("?>")) {
        const auto end = tokenizer.ScanDelimitedSectionEnd(position_, "?>");
        if (end == std::string::npos) {
            Throw("Unterminated processing instruction");
        }
        dataStart = position_;
        dataEnd = end;
        while (dataStart < dataEnd && IsWhitespace(SourceCharAt(dataStart))) {
            ++dataStart;
        }
        while (dataEnd > dataStart && IsWhitespace(SourceCharAt(dataEnd - 1))) {
            --dataEnd;
        }
        const auto lineInfoStart = position_;
        position_ = end;
        AdvanceLineInfoFromInputSource(inputSource_.get(), lineInfoStart, position_, lineNumber_, linePosition_);
    }
    position_ += 2;
    linePosition_ += 2;

    SetCurrentNode(
        XmlNodeType::ProcessingInstruction,
        target,
        {},
        {},
        static_cast<int>(elementStack_.size()),
        false,
        {},
        {},
        dataStart,
        dataEnd,
        start,
        position_);
}

void XmlReader::ParseComment() {
    XmlReaderTokenizer tokenizer(inputSource_);
    const auto start = position_;
    const auto end = tokenizer.ScanDelimitedSectionEnd(position_ + 4, "-->");
    if (end == std::string::npos) {
        const auto [line, column] = ComputeLineColumn(position_ + 4);
        throw XmlException("Unterminated comment", line, column);
    }
    const auto valueStart = position_ + 4;
    const auto valueEnd = end;
    position_ = end + 3;
    AdvanceLineInfoFromInputSource(inputSource_.get(), start, position_, lineNumber_, linePosition_);

    SetCurrentNode(XmlNodeType::Comment, {}, {}, {}, static_cast<int>(elementStack_.size()), false, {}, {}, valueStart, valueEnd, start, position_);
}

void XmlReader::ParseCData() {
    XmlReaderTokenizer tokenizer(inputSource_);
    const auto start = position_;
    const auto end = tokenizer.ScanDelimitedSectionEnd(position_ + 9, "]]>");
    if (end == std::string::npos) {
        const auto [line, column] = ComputeLineColumn(position_ + 9);
        throw XmlException("Unterminated CDATA section", line, column);
    }
    const auto valueStart = position_ + 9;
    const auto valueEnd = end;
    position_ = end + 3;
    AdvanceLineInfoFromInputSource(inputSource_.get(), start, position_, lineNumber_, linePosition_);

    SetCurrentNode(XmlNodeType::CDATA, {}, {}, {}, static_cast<int>(elementStack_.size()), false, {}, {}, valueStart, valueEnd, start, position_);
}

void XmlReader::ParseText() {
    XmlReaderTokenizer tokenizer(inputSource_);
    const int textDepth = static_cast<int>(elementStack_.size());
    const bool preserveSpace = !xmlSpacePreserveStack_.empty() && xmlSpacePreserveStack_.back();
    std::string textBuffer;
    std::size_t rawSegmentStart = std::string::npos;
    bool textBufferMaterialized = false;
    bool rawSegmentKnownNonWhitespace = false;

    auto isWhitespaceOnlySourceRange = [&](std::size_t start, std::size_t end) {
        for (std::size_t index = start; index < end; ++index) {
            if (!IsWhitespace(SourceCharAt(index))) {
                return false;
            }
        }
        return true;
    };

    auto flushText = [&](std::size_t segmentEnd) {
        if (rawSegmentStart == std::string::npos) {
            return;
        }

        bool isWhitespaceOnly = false;
        if (textBufferMaterialized) {
            isWhitespaceOnly = IsWhitespaceOnly(textBuffer);
        } else if (rawSegmentKnownNonWhitespace) {
            isWhitespaceOnly = false;
        } else {
            isWhitespaceOnly = isWhitespaceOnlySourceRange(rawSegmentStart, segmentEnd);
        }

        XmlNodeType nodeType = XmlNodeType::Text;
        if (isWhitespaceOnly) {
            nodeType = preserveSpace ? XmlNodeType::SignificantWhitespace : XmlNodeType::Whitespace;
        }

        if (textBufferMaterialized) {
            QueueNode(nodeType, {}, {}, textBuffer, textDepth, false, {}, {}, std::string::npos, std::string::npos, rawSegmentStart, segmentEnd);
        } else {
            QueueNode(nodeType, {}, {}, {}, textDepth, false, {}, {}, rawSegmentStart, segmentEnd, rawSegmentStart, segmentEnd);
        }
        textBuffer.clear();
        rawSegmentStart = std::string::npos;
        textBufferMaterialized = false;
        rawSegmentKnownNonWhitespace = false;
    };

    while (HasSourceChar(position_) && SourceCharAt(position_) != '<') {
        if (SourceCharAt(position_) != '&') {
            const auto segmentEnd = FindFirstOfFromInputSource(inputSource_.get(), "\n<&", position_);

            if (segmentEnd != std::string::npos && segmentEnd > position_) {
                if (rawSegmentStart == std::string::npos) {
                    rawSegmentStart = position_;
                }
                if (!rawSegmentKnownNonWhitespace && !IsWhitespace(SourceCharAt(position_))) {
                    rawSegmentKnownNonWhitespace = true;
                }
                if (textBufferMaterialized) {
                    AppendSourceSubstrTo(textBuffer, position_, segmentEnd - position_);
                }
                linePosition_ += segmentEnd - position_;
                position_ = segmentEnd;
                continue;
            }

            if (rawSegmentStart == std::string::npos) {
                rawSegmentStart = position_;
            }
            if (!rawSegmentKnownNonWhitespace && !IsWhitespace(SourceCharAt(position_))) {
                rawSegmentKnownNonWhitespace = true;
            }
            if (textBufferMaterialized) {
                textBuffer.push_back(SourceCharAt(position_));
            }
            if (SourceCharAt(position_) == '\n') {
                ++lineNumber_;
                linePosition_ = 1;
            } else {
                ++linePosition_;
            }
            ++position_;
            continue;
        }

        const auto entityStart = position_;
        const auto entityToken = tokenizer.ScanEntityReference(position_);
        if (entityToken.end == std::string::npos) {
            Throw("Unterminated entity reference");
        }

        const std::string rawEntity = entityToken.raw;
        const std::string entity = entityToken.name;
        position_ = entityToken.end;
        linePosition_ += entityToken.end - entityStart;

        const auto predefined = ResolvePredefinedEntityReferenceValueView(entity);
        if (!predefined.empty()) {
            if (rawSegmentStart == std::string::npos) {
                rawSegmentStart = entityStart;
            }
            if (!textBufferMaterialized) {
                textBuffer = SourceSubstr(rawSegmentStart, entityStart - rawSegmentStart);
                textBufferMaterialized = true;
            }
            textBuffer.append(predefined.data(), predefined.size());
            continue;
        }

        if (!entity.empty() && entity.front() == '#') {
            if (rawSegmentStart == std::string::npos) {
                rawSegmentStart = entityStart;
            }
            if (!textBufferMaterialized) {
                textBuffer = SourceSubstr(rawSegmentStart, entityStart - rawSegmentStart);
                textBufferMaterialized = true;
            }
            unsigned int codePoint = 0;
            if (!TryParseNumericEntityReferenceCodePoint(entity, codePoint)) {
                Throw("Invalid numeric entity reference: &" + entity + ';');
            }
            AppendCodePointUtf8(textBuffer, codePoint);
            continue;
        }

        if (declaredEntityNames_.find(entity) == declaredEntityNames_.end()) {
            Throw("Unknown entity reference: &" + entity + ';');
        }

        flushText(entityStart);

        std::string resolvedValue;
        const auto declared = entityDeclarations_.find(entity);
        if (declared != entityDeclarations_.end()) {
            resolvedValue = DecodeEntityText(
                declared->second,
                [this](const std::string& nestedEntity) -> std::optional<std::string> {
                    const auto found = entityDeclarations_.find(nestedEntity);
                    return found == entityDeclarations_.end() ? std::nullopt : std::optional<std::string>(found->second);
                },
                [this](const std::string& message) {
                    Throw(message);
                });
            if (settings_.MaxCharactersFromEntities != 0) {
                entityCharactersRead_ += resolvedValue.size();
                if (entityCharactersRead_ > settings_.MaxCharactersFromEntities) {
                    Throw("The XML document exceeds the configured MaxCharactersFromEntities limit");
                }
            }
        } else if (settings_.Resolver != nullptr) {
            const auto external = externalEntitySystemIds_.find(entity);
            if (external != externalEntitySystemIds_.end()) {
                const std::string absoluteUri = settings_.Resolver->ResolveUri(baseUri_, external->second);
                resolvedValue = settings_.Resolver->GetEntity(absoluteUri);
                if (settings_.MaxCharactersFromEntities != 0) {
                    entityCharactersRead_ += resolvedValue.size();
                    if (entityCharactersRead_ > settings_.MaxCharactersFromEntities) {
                        Throw("The XML document exceeds the configured MaxCharactersFromEntities limit");
                    }
                }
            }
        }

        QueueNode(XmlNodeType::EntityReference, entity, {}, resolvedValue, textDepth, false, {}, {}, std::string::npos, std::string::npos, entityStart, entityToken.end);
        if (!resolvedValue.empty()) {
            const XmlNodeType resolvedNodeType = IsWhitespaceOnly(resolvedValue)
                ? (preserveSpace ? XmlNodeType::SignificantWhitespace : XmlNodeType::Whitespace)
                : XmlNodeType::Text;
            QueueNode(resolvedNodeType, {}, {}, resolvedValue, textDepth + 1, false, {}, resolvedValue);
        }
        QueueNode(XmlNodeType::EndEntity, entity, {}, {}, textDepth, false, {}, {}, std::string::npos, std::string::npos, entityStart, entityToken.end);
    }

    flushText(position_);
    if (!TryConsumeBufferedNode()) {
        Throw("Unexpected empty text segment");
    }
}

bool XmlReader::TryReadSimpleElementContentAsString(std::string& result, std::size_t& closeStart, std::size_t& closeEnd) {
    if (inputSource_ == nullptr
        || currentElementStart_ == std::string::npos
        || currentContentStart_ == std::string::npos
        || currentIsEmptyElement_) {
        return false;
    }

    const auto bounds = EnsureCurrentElementXmlBounds();
    closeStart = bounds.first;
    closeEnd = bounds.second;
    if (closeStart == std::string::npos || closeEnd == std::string::npos || closeStart < currentContentStart_) {
        return false;
    }

    XmlReaderTokenizer tokenizer(inputSource_);
    result.clear();
    result.reserve(closeStart - currentContentStart_);

    std::size_t position = currentContentStart_;
    while (position < closeStart) {
        const std::size_t nextSpecial = FindFirstOfFromInputSource(inputSource_.get(), "<&", position);
        const std::size_t segmentEnd = nextSpecial == std::string::npos || nextSpecial > closeStart
            ? closeStart
            : nextSpecial;

        if (segmentEnd > position) {
            AppendSourceSubstrTo(result, position, segmentEnd - position);
            position = segmentEnd;
            continue;
        }

        if (position >= closeStart) {
            break;
        }

        if (SourceCharAt(position) == '&') {
            const auto entityToken = tokenizer.ScanEntityReference(position);
            if (entityToken.end == std::string::npos) {
                return false;
            }

            const std::string entity = entityToken.name;
            const auto predefined = ResolvePredefinedEntityReferenceValueView(entity);
            if (!predefined.empty()) {
                result.append(predefined.data(), predefined.size());
                position = entityToken.end;
                continue;
            }

            if (!entity.empty() && entity.front() == '#') {
                unsigned int codePoint = 0;
                if (!TryParseNumericEntityReferenceCodePoint(entity, codePoint)) {
                    return false;
                }
                AppendCodePointUtf8(result, codePoint);
                position = entityToken.end;
                continue;
            }

            return false;
        }

        if (tokenizer.StartsWith(position, "<![CDATA[")) {
            const auto end = tokenizer.ScanDelimitedSectionEnd(position + 9, "]]>");
            if (end == std::string::npos || end > closeStart) {
                return false;
            }
            AppendSourceSubstrTo(result, position + 9, end - (position + 9));
            position = end + 3;
            continue;
        }

        if (tokenizer.StartsWith(position, "<!--")) {
            const auto end = tokenizer.ScanDelimitedSectionEnd(position + 4, "-->");
            if (end == std::string::npos || end > closeStart) {
                return false;
            }
            position = end + 3;
            continue;
        }

        if (tokenizer.StartsWith(position, "<?")) {
            const auto end = tokenizer.ScanDelimitedSectionEnd(position + 2, "?>");
            if (end == std::string::npos || end > closeStart) {
                return false;
            }
            position = end + 2;
            continue;
        }

        return false;
    }

    return position == closeStart;
}

std::pair<std::size_t, std::size_t> XmlReader::EnsureCurrentElementXmlBounds() const {
    if (currentCloseStart_ == std::string::npos || currentCloseEnd_ == std::string::npos) {
        const auto bounds = FindElementXmlBounds(currentElementStart_, currentContentStart_, currentName_);
        currentCloseStart_ = bounds.first;
        currentCloseEnd_ = bounds.second;
    }
    return {currentCloseStart_, currentCloseEnd_};
}

std::pair<std::size_t, std::size_t> XmlReader::FindElementXmlBounds(
    std::size_t,
    std::size_t contentStart,
    const std::string& elementName) const {
    XmlReaderTokenizer tokenizer(inputSource_);
    std::vector<std::string> elementNames;
    elementNames.push_back(elementName);

    auto throwAt = [this](const std::string& message, std::size_t errorPosition) -> void {
        const auto [line, column] = ComputeLineColumn(errorPosition);
        throw XmlException(message, line, column);
    };

    std::size_t position = contentStart;
    std::size_t closeStart = std::string::npos;
    std::size_t closeEnd = std::string::npos;

    // Fast path for the common text-only case: the first markup after content start
    // is the matching closing tag for the current element.
    const std::size_t firstMarkup = FindInSource("<", contentStart);
    if (firstMarkup != std::string::npos && tokenizer.StartsWith(firstMarkup, "</")) {
        std::size_t closeCursor = firstMarkup + 2;
        const std::string closeName = tokenizer.ParseNameAt(closeCursor);
        if (closeName == elementName && tokenizer.ConsumeEndTagClose(closeCursor)) {
            return {firstMarkup, closeCursor};
        }
    }

    while (HasSourceChar(position)) {
        if (SourceCharAt(position) != '<') {
            ++position;
            continue;
        }

        if (tokenizer.StartsWith(position, "<!--")) {
            const auto end = tokenizer.ScanDelimitedSectionEnd(position + 4, "-->");
            if (end == std::string::npos) {
                throwAt("Unterminated comment", position + 4);
            }
            position = end + 3;
            continue;
        }

        if (tokenizer.StartsWith(position, "<![CDATA[")) {
            const auto end = tokenizer.ScanDelimitedSectionEnd(position + 9, "]]>");
            if (end == std::string::npos) {
                throwAt("Unterminated CDATA section", position + 9);
            }
            position = end + 3;
            continue;
        }

        if (tokenizer.StartsWith(position, "<?")) {
            const auto end = tokenizer.ScanDelimitedSectionEnd(position + 2, "?>");
            if (end == std::string::npos) {
                std::size_t errorPosition = position + 2;
                const std::size_t nameStart = errorPosition;
                std::string target = tokenizer.ParseNameAt(errorPosition);
                if (target.empty()) {
                    errorPosition = nameStart;
                }
                throwAt("Unterminated processing instruction", errorPosition);
            }
            position = end + 2;
            continue;
        }

        if (tokenizer.StartsWith(position, "</")) {
            closeStart = position;
            position += 2;
            const std::string closeName = tokenizer.ParseNameAt(position);
            if (closeName.empty()) {
                break;
            }

            const std::size_t errorPosition = position;
            while (HasSourceChar(position) && SourceCharAt(position) != '>') {
                ++position;
            }
            if (!HasSourceChar(position)) {
                break;
            }
            ++position;

            if (elementNames.empty()) {
                throwAt("Unexpected closing tag: </" + closeName + ">", errorPosition);
            }
            if (closeName != elementNames.back()) {
                throwAt(
                    "Mismatched closing tag. Expected </" + elementNames.back() + "> but found </" + closeName + ">",
                    errorPosition);
            }

            elementNames.pop_back();
            if (elementNames.empty()) {
                closeEnd = position;
                break;
            }
            continue;
        }

        if (tokenizer.StartsWith(position, "<!")) {
            const auto end = tokenizer.ScanDelimitedSectionEnd(position + 2, ">");
            if (end == std::string::npos) {
                break;
            }
            position = end + 1;
            continue;
        }

        std::size_t namePosition = position + 1;
        const std::string openName = tokenizer.ParseNameAt(namePosition);
        bool isEmptyElement = false;
        if (!tokenizer.SkipTag(position, isEmptyElement)) {
            break;
        }
        if (!isEmptyElement) {
            elementNames.push_back(openName);
        }
    }

    if (closeStart == std::string::npos || closeEnd == std::string::npos) {
        if (!elementNames.empty()) {
            throwAt("Unexpected end of input inside element <" + elementNames.back() + ">", position);
        }
        Throw("Unterminated element");
    }

    return {closeStart, closeEnd};
}

std::pair<std::string, std::string> XmlReader::CaptureElementXml(
    std::size_t elementStart,
    std::size_t contentStart) const {
    const auto [closeStart, closeEnd] = EnsureCurrentElementXmlBounds();
    return {
        SourceSubstr(contentStart, closeStart - contentStart),
        SourceSubstr(elementStart, closeEnd - elementStart)};
}

void XmlReader::ParseElement() {
    XmlReaderTokenizer tokenizer(inputSource_);
    const auto start = position_;
    ReadChar();
    const std::string name = ParseName();
    const std::string elementPrefix{SplitQualifiedNameView(name).first};
    const bool topLevelElement = elementStack_.empty();
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<AttributeValueMetadata> attributeValueMetadata;
    std::vector<std::pair<std::string, std::string>> localNamespaceDeclarations;
    bool retainLocalNamespaceDeclarationsForAttributes = false;
    const bool inheritedPreserveSpace = !xmlSpacePreserveStack_.empty() && xmlSpacePreserveStack_.back();
    bool preserveSpace = inheritedPreserveSpace;

    auto lookupNamespaceInScopes = [this, &localNamespaceDeclarations](const std::string& prefix) {
        for (auto it = localNamespaceDeclarations.rbegin(); it != localNamespaceDeclarations.rend(); ++it) {
            if (it->first == prefix) {
                return it->second;
            }
        }
        return LookupNamespaceUri(prefix);
    };

    auto setLocalNamespaceDeclaration = [&localNamespaceDeclarations](std::string prefix, std::string namespaceUri) {
        for (auto it = localNamespaceDeclarations.rbegin(); it != localNamespaceDeclarations.rend(); ++it) {
            if (it->first == prefix) {
                it->second = std::move(namespaceUri);
                return;
            }
        }

        localNamespaceDeclarations.emplace_back(std::move(prefix), std::move(namespaceUri));
    };

    auto sourceRangeEqualsLiteral = [this](std::size_t rangeStart, std::size_t rangeEnd, std::string_view literal) {
        if (rangeStart == std::string::npos || rangeEnd == std::string::npos || rangeEnd < rangeStart) {
            return false;
        }

        const std::size_t length = rangeEnd - rangeStart;
        if (length != literal.size()) {
            return false;
        }

        for (std::size_t index = 0; index < length; ++index) {
            if (SourceCharAt(rangeStart + index) != literal[index]) {
                return false;
            }
        }

        return true;
    };

    while (true) {
        const auto whitespaceStart = position_;
        tokenizer.SkipWhitespace(position_);
        AdvanceLineInfoFromInputSource(inputSource_.get(), whitespaceStart, position_, lineNumber_, linePosition_);
        bool isEmptyElement = false;
        const auto closeStart = position_;
        if (tokenizer.ConsumeStartTagClose(position_, isEmptyElement)) {
            AdvanceLineInfoFromInputSource(inputSource_.get(), closeStart, position_, lineNumber_, linePosition_);
            if (isEmptyElement) {
                SetCurrentNode(
                    XmlNodeType::Element,
                    name,
                    lookupNamespaceInScopes(elementPrefix),
                    {},
                    static_cast<int>(elementStack_.size()),
                    true,
                    {},
                    {},
                    std::string::npos,
                    std::string::npos,
                    start,
                    position_,
                    std::move(attributes),
                    {},
                    start,
                    position_);
                currentLocalNamespaceDeclarations_ = retainLocalNamespaceDeclarationsForAttributes
                    ? localNamespaceDeclarations
                    : std::vector<std::pair<std::string, std::string>>{};
                currentAttributeValueMetadata_ = std::move(attributeValueMetadata);
                RefreshCurrentEarliestRetainedAttributeValueStart();
                if (topLevelElement) {
                    sawRootElement_ = true;
                    completedRootElement_ = true;
                }
                return;
            }
            break;
        }

        if (!tokenizer.HasChar(position_)) {
            Throw("Unexpected end of XML document");
        }

        const auto attributeStart = position_;
        auto attributeToken = tokenizer.ParseAttributeAssignment(position_);
        AdvanceLineInfoFromInputSource(inputSource_.get(), attributeStart, position_, lineNumber_, linePosition_);
        if (attributeToken.name.empty()) {
            if (tokenizer.CharAt(position_) == '/' || tokenizer.CharAt(position_) == '>') {
                Throw("Expected '>' after element attributes");
            }
            Throw("Invalid XML name");
        }
        if (!attributeToken.valid) {
            if (!attributeToken.sawEquals) {
                if (tokenizer.HasChar(position_)) {
                    ++position_;
                }
                Throw("Expected '=' after attribute name");
            }
            if (tokenizer.HasChar(position_)) {
                ++position_;
            }
            Throw("Expected quoted value");
        }
        const auto rawValueStart = attributeToken.rawValueStart;
        const auto rawValueEnd = attributeToken.rawValueEnd;
        const bool needsDecoding = SourceRangeContains(rawValueStart, rawValueEnd, '&');
        attributes.emplace_back(std::move(attributeToken.name), std::string{});
        const std::string& attributeName = attributes.back().first;
        const auto attributeColon = attributeName.find(':');
        if (attributeColon != std::string::npos
            && std::string_view(attributeName).substr(0, attributeColon) != "xmlns") {
            retainLocalNamespaceDeclarationsForAttributes = true;
        }
        attributeValueMetadata.push_back(AttributeValueMetadata{
            rawValueStart,
            rawValueEnd,
            static_cast<unsigned char>(needsDecoding ? kAttributeValueNeedsDecoding : 0)});
        if (attributeName == "xmlns") {
            std::string attributeValue = SourceSubstr(rawValueStart, rawValueEnd - rawValueStart);
            if (needsDecoding) {
                attributeValue = DecodeEntities(attributeValue);
            }
            setLocalNamespaceDeclaration({}, std::move(attributeValue));
        } else if (attributeName.rfind("xmlns:", 0) == 0) {
            std::string attributeValue = SourceSubstr(rawValueStart, rawValueEnd - rawValueStart);
            if (needsDecoding) {
                attributeValue = DecodeEntities(attributeValue);
            }
            setLocalNamespaceDeclaration(attributeName.substr(6), std::move(attributeValue));
        } else if (attributeName == "xml:space") {
            if (!needsDecoding) {
                if (sourceRangeEqualsLiteral(rawValueStart, rawValueEnd, "preserve")) {
                    preserveSpace = true;
                } else if (sourceRangeEqualsLiteral(rawValueStart, rawValueEnd, "default")) {
                    preserveSpace = false;
                }
            } else {
                std::string attributeValue = SourceSubstr(rawValueStart, rawValueEnd - rawValueStart);
                attributeValue = DecodeEntities(attributeValue);
                if (IsXmlSpacePreserve(attributeValue)) {
                    preserveSpace = true;
                } else if (IsXmlSpaceDefault(attributeValue)) {
                    preserveSpace = false;
                }
            }
        }
    }

    const bool pushedNamespaceScope = !localNamespaceDeclarations.empty();
    const std::string elementNamespaceUri = lookupNamespaceInScopes(elementPrefix);
    currentLocalNamespaceDeclarations_ = retainLocalNamespaceDeclarationsForAttributes
        ? localNamespaceDeclarations
        : std::vector<std::pair<std::string, std::string>>{};
    if (pushedNamespaceScope) {
        std::unordered_map<std::string, std::string> localScope;
        localScope.reserve(localNamespaceDeclarations.size());
        for (auto& [prefix, namespaceUri] : localNamespaceDeclarations) {
            localScope[std::move(prefix)] = std::move(namespaceUri);
        }
        namespaceScopes_.push_back(std::move(localScope));
    }
    SetCurrentNode(
        XmlNodeType::Element,
        name,
        std::move(elementNamespaceUri),
        {},
        static_cast<int>(elementStack_.size()),
        false,
        {},
        {},
        std::string::npos,
        std::string::npos,
        start,
        position_,
        std::move(attributes),
        {},
        start,
        position_);
    currentAttributeValueMetadata_ = std::move(attributeValueMetadata);
    RefreshCurrentEarliestRetainedAttributeValueStart();
    const bool pushedXmlSpacePreserve = preserveSpace != inheritedPreserveSpace;
    if (pushedXmlSpacePreserve) {
        xmlSpacePreserveStack_.push_back(preserveSpace);
    }
    namespaceScopeFramePushedStack_.push_back(pushedNamespaceScope);
    xmlSpacePreserveFramePushedStack_.push_back(pushedXmlSpacePreserve);
    elementStack_.push_back(name);
    if (topLevelElement) {
        sawRootElement_ = true;
    }
}

void XmlReader::ParseEndElement() {
    XmlReaderTokenizer tokenizer(inputSource_);
    const auto start = position_;
    std::size_t cursor = position_ + 2;
    const std::string name = tokenizer.ParseNameAt(cursor);
    const std::string namePrefix{SplitQualifiedNameView(name).first};
    position_ = cursor;
    if (elementStack_.empty()) {
        Throw("Unexpected closing tag: </" + name + ">");
    }
    if (elementStack_.back() != name) {
        Throw("Mismatched closing tag. Expected </" + elementStack_.back() + "> but found </" + name + ">");
    }
    if (!tokenizer.ConsumeEndTagClose(cursor)) {
        const std::size_t errorPosition = tokenizer.HasChar(cursor) ? cursor + 1 : cursor;
        const auto [line, column] = ComputeLineColumn(errorPosition);
        throw XmlException("Expected '>' after closing tag", line, column);
    }
    position_ = cursor;
    AdvanceLineInfoFromInputSource(inputSource_.get(), start, position_, lineNumber_, linePosition_);

    const int depth = static_cast<int>(elementStack_.size()) - 1;
    const std::string namespaceUri = LookupNamespaceUri(namePrefix);
    bool popNamespaceScope = false;
    if (!namespaceScopeFramePushedStack_.empty()) {
        popNamespaceScope = namespaceScopeFramePushedStack_.back();
        namespaceScopeFramePushedStack_.pop_back();
    }
    bool popXmlSpacePreserve = false;
    if (!xmlSpacePreserveFramePushedStack_.empty()) {
        popXmlSpacePreserve = xmlSpacePreserveFramePushedStack_.back();
        xmlSpacePreserveFramePushedStack_.pop_back();
    }
    elementStack_.pop_back();
    if (popNamespaceScope && namespaceScopes_.size() > 1) {
        namespaceScopes_.pop_back();
    }
    if (popXmlSpacePreserve && xmlSpacePreserveStack_.size() > 1) {
        xmlSpacePreserveStack_.pop_back();
    }
    if (elementStack_.empty() && sawRootElement_) {
        completedRootElement_ = true;
    }
    SetCurrentNode(
        XmlNodeType::EndElement,
        std::move(name),
        namespaceUri,
        {},
        depth,
        false,
        {},
        {},
        std::string::npos,
        std::string::npos,
        start,
        position_);
}

std::string XmlReader::LookupNamespaceUri(const std::string& prefix) const {
    for (auto it = namespaceScopes_.rbegin(); it != namespaceScopes_.rend(); ++it) {
        const auto found = it->find(prefix);
        if (found != it->end()) {
            return found->second;
        }
    }

    return {};
}

const std::vector<std::pair<std::string, std::string>>& XmlReader::CurrentAttributes() const {
    return currentAttributes_;
}

std::string XmlReader::CurrentLocalName() const {
    return std::string{SplitQualifiedNameView(currentName_).second};
}

std::string XmlReader::CurrentPrefix() const {
    return std::string{SplitQualifiedNameView(currentName_).first};
}

std::string XmlReader::CurrentAttributeLocalName(std::size_t index) const {
    if (index >= currentAttributes_.size()) {
        return {};
    }
    return std::string{SplitQualifiedNameView(currentAttributes_[index].first).second};
}

std::string XmlReader::CurrentAttributePrefix(std::size_t index) const {
    if (index >= currentAttributes_.size()) {
        return {};
    }
    return std::string{SplitQualifiedNameView(currentAttributes_[index].first).first};
}

const std::string& XmlReader::CurrentAttributeNamespaceUri(std::size_t index) const {
    if (index >= currentAttributes_.size()) {
        return EmptyString();
    }

    if (index >= currentAttributeNamespaceUris_.size()) {
        currentAttributeNamespaceUris_.resize(currentAttributes_.size());
    }
    if (index >= currentAttributeNamespaceUrisResolved_.size()) {
        currentAttributeNamespaceUrisResolved_.resize(currentAttributes_.size(), static_cast<unsigned char>(0));
    }
    if (currentAttributeNamespaceUrisResolved_[index] != 0) {
        return currentAttributeNamespaceUris_[index];
    }

    const std::string& attributeName = currentAttributes_[index].first;
    const auto colon = attributeName.find(':');
    const bool hasPrefix = colon != std::string::npos;
    const std::string_view attributePrefix = hasPrefix
        ? std::string_view(attributeName).substr(0, colon)
        : std::string_view{};

    std::string namespaceUri;
    if (attributeName == "xmlns" || attributePrefix == "xmlns") {
        namespaceUri = "http://www.w3.org/2000/xmlns/";
    } else if (hasPrefix) {
        for (const auto& entry : currentLocalNamespaceDeclarations_) {
            if (std::string_view(entry.first) == attributePrefix) {
                namespaceUri = entry.second;
                break;
            }
        }
        if (namespaceUri.empty()) {
            const std::string prefix(attributePrefix);
            namespaceUri = LookupNamespaceUri(prefix);
        }
    }

    currentAttributeNamespaceUris_[index] = std::move(namespaceUri);
    currentAttributeNamespaceUrisResolved_[index] = static_cast<unsigned char>(1);
    return currentAttributeNamespaceUris_[index];
}

void XmlReader::RefreshCurrentEarliestRetainedAttributeValueStart() const noexcept {
    currentEarliestRetainedAttributeValueStart_ = std::string::npos;
    for (const auto& metadata : currentAttributeValueMetadata_) {
        if ((metadata.flags & kAttributeValueDecoded) != 0) {
            continue;
        }
        if (metadata.valueStart == std::string::npos
            || metadata.valueEnd == std::string::npos
            || metadata.valueEnd < metadata.valueStart) {
            continue;
        }
        if (currentEarliestRetainedAttributeValueStart_ == std::string::npos
            || metadata.valueStart < currentEarliestRetainedAttributeValueStart_) {
            currentEarliestRetainedAttributeValueStart_ = metadata.valueStart;
        }
    }
}

void XmlReader::EnsureCurrentAttributeValueDecoded(std::size_t index) const {
    if (index >= currentAttributes_.size()) {
        return;
    }
    if (index >= currentAttributeValueMetadata_.size()) {
        return;
    }

    auto& metadata = currentAttributeValueMetadata_[index];
    if ((metadata.flags & kAttributeValueDecoded) != 0) {
        return;
    }

    currentAttributes_[index].second.clear();
    if (metadata.valueStart != std::string::npos
        && metadata.valueEnd != std::string::npos
        && metadata.valueEnd >= metadata.valueStart) {
        if ((metadata.flags & kAttributeValueNeedsDecoding) != 0) {
            AppendDecodedSourceRangeTo(currentAttributes_[index].second, metadata.valueStart, metadata.valueEnd);
        } else {
            currentAttributes_[index].second = SourceSubstr(metadata.valueStart, metadata.valueEnd - metadata.valueStart);
        }
    }
    metadata.flags = static_cast<unsigned char>(metadata.flags | kAttributeValueDecoded);

    if (metadata.valueStart != std::string::npos
        && metadata.valueEnd != std::string::npos
        && metadata.valueEnd >= metadata.valueStart
        && metadata.valueStart == currentEarliestRetainedAttributeValueStart_) {
        RefreshCurrentEarliestRetainedAttributeValueStart();
    }
}

const std::string& XmlReader::CurrentAttributeValue(std::size_t index) const {
    EnsureCurrentAttributeValueDecoded(index);
    return currentAttributes_[index].second;
}

void XmlReader::AppendCurrentAttributesForLoad(XmlElement& element) {
    if (currentAttributes_.empty()) {
        return;
    }

    const bool hasMetadataForAllAttributes = currentAttributeValueMetadata_.size() == currentAttributes_.size();
    std::size_t totalStorageBytes = 0;
    if (hasMetadataForAllAttributes) {
        for (std::size_t index = 0; index < currentAttributes_.size(); ++index) {
            const auto& attribute = currentAttributes_[index];
            const auto& metadata = currentAttributeValueMetadata_[index];
            totalStorageBytes += attribute.first.size();
            if ((metadata.flags & kAttributeValueDecoded) == 0
                && metadata.valueStart != std::string::npos
                && metadata.valueEnd != std::string::npos
                && metadata.valueEnd >= metadata.valueStart) {
                totalStorageBytes += metadata.valueEnd - metadata.valueStart;
                continue;
            }
            totalStorageBytes += attribute.second.size();
        }
    } else {
        for (std::size_t index = 0; index < currentAttributes_.size(); ++index) {
            const auto& attribute = currentAttributes_[index];
            totalStorageBytes += attribute.first.size();
            if (index < currentAttributeValueMetadata_.size()) {
                const auto& metadata = currentAttributeValueMetadata_[index];
                if ((metadata.flags & kAttributeValueDecoded) == 0
                    && metadata.valueStart != std::string::npos
                    && metadata.valueEnd != std::string::npos
                    && metadata.valueEnd >= metadata.valueStart) {
                    totalStorageBytes += metadata.valueEnd - metadata.valueStart;
                    continue;
                }
            } else {
                EnsureCurrentAttributeValueDecoded(index);
            }
            totalStorageBytes += attribute.second.size();
        }
    }

    currentEarliestRetainedAttributeValueStart_ = std::string::npos;

    element.ReserveAttributesForLoad(currentAttributes_.size(), totalStorageBytes);
    auto& pendingStorage = element.pendingLoadAttributeStorage_;
    auto& pendingAttributes = element.pendingLoadAttributes_;
    const auto appendPendingAttribute = [&pendingAttributes](
        std::size_t nameOffset,
        std::size_t nameLength,
        std::size_t valueOffset,
        std::size_t valueLength) {
        pendingAttributes.push_back(XmlElement::PendingLoadAttribute{
            nameOffset,
            nameLength,
            valueOffset,
            valueLength});
    };

    if (hasMetadataForAllAttributes) {
        for (std::size_t index = 0; index < currentAttributes_.size(); ++index) {
            const auto& attribute = currentAttributes_[index];
            const auto& metadata = currentAttributeValueMetadata_[index];
            const std::size_t nameOffset = pendingStorage.size();
            pendingStorage += attribute.first;

            const std::size_t valueOffset = pendingStorage.size();
            std::size_t valueLength = attribute.second.size();
            if ((metadata.flags & kAttributeValueDecoded) == 0
                && metadata.valueStart != std::string::npos
                && metadata.valueEnd != std::string::npos
                && metadata.valueEnd >= metadata.valueStart) {
                const std::size_t rawLength = metadata.valueEnd - metadata.valueStart;
                if ((metadata.flags & kAttributeValueNeedsDecoding) == 0) {
                    valueLength = rawLength;
                    AppendSourceSubstrTo(pendingStorage, metadata.valueStart, valueLength);
                } else {
                    valueLength = AppendDecodedSourceRangeTo(pendingStorage, metadata.valueStart, metadata.valueEnd);
                }

                appendPendingAttribute(nameOffset, attribute.first.size(), valueOffset, valueLength);
                continue;
            }

            pendingStorage += attribute.second;
            appendPendingAttribute(nameOffset, attribute.first.size(), valueOffset, valueLength);
        }
        return;
    }

    for (std::size_t index = 0; index < currentAttributes_.size(); ++index) {
        const auto& attribute = currentAttributes_[index];
        const std::size_t nameOffset = pendingStorage.size();
        pendingStorage += attribute.first;

        const std::size_t valueOffset = pendingStorage.size();
        std::size_t valueLength = attribute.second.size();

        if (index < currentAttributeValueMetadata_.size()) {
            const auto& metadata = currentAttributeValueMetadata_[index];
            if ((metadata.flags & kAttributeValueDecoded) == 0
                && metadata.valueStart != std::string::npos
                && metadata.valueEnd != std::string::npos
                && metadata.valueEnd >= metadata.valueStart) {
                const std::size_t rawLength = metadata.valueEnd - metadata.valueStart;
                if ((metadata.flags & kAttributeValueNeedsDecoding) == 0) {
                    valueLength = rawLength;
                    AppendSourceSubstrTo(pendingStorage, metadata.valueStart, valueLength);
                } else {
                    valueLength = AppendDecodedSourceRangeTo(pendingStorage, metadata.valueStart, metadata.valueEnd);
                }

                appendPendingAttribute(nameOffset, attribute.first.size(), valueOffset, valueLength);
                continue;
            }
        }

        pendingStorage += attribute.second;
        appendPendingAttribute(nameOffset, attribute.first.size(), valueOffset, valueLength);
    }
}

void XmlReader::InitializeInputState() {
    namespaceScopes_.clear();
    namespaceScopeFramePushedStack_.clear();
    xmlSpacePreserveStack_.clear();
    xmlSpacePreserveFramePushedStack_.clear();
    namespaceScopes_.push_back({});
    namespaceScopes_.back().emplace("xml", "http://www.w3.org/XML/1998/namespace");
    namespaceScopes_.back().emplace("xmlns", "http://www.w3.org/2000/xmlns/");
    xmlSpacePreserveStack_.push_back(false);
    if (HasSourceChar(2)
        && static_cast<unsigned char>(SourceCharAt(0)) == 0xEF
        && static_cast<unsigned char>(SourceCharAt(1)) == 0xBB
        && static_cast<unsigned char>(SourceCharAt(2)) == 0xBF) {
        position_ = 3;
    }
    eof_ = !HasSourceChar(position_);
    lineNumber_ = 1;
    linePosition_ = 1;
    discardedSourceOffset_ = 0;
    discardedLineNumber_ = 1;
    discardedLinePosition_ = 1;
}

XmlReader XmlReader::CreateFromValidatedString(std::shared_ptr<const std::string> xml, const XmlReaderSettings& settings) {
    const std::size_t sourceSize = xml == nullptr ? 0 : xml->size();
    XmlReader reader(settings);
    reader.inputSource_ = std::make_shared<StringXmlReaderInputSource>(std::move(xml));
    reader.InitializeInputState();
    if (reader.settings_.MaxCharactersInDocument != 0
        && sourceSize > reader.position_
        && sourceSize - reader.position_ > reader.settings_.MaxCharactersInDocument) {
        throw XmlException("The XML document exceeds the configured MaxCharactersInDocument limit");
    }
    return reader;
}

XmlReader XmlReader::Create(const std::string& xml, const XmlReaderSettings& settings) {
    auto sourceText = std::make_shared<std::string>(xml);
    ValidateXmlReaderInputAgainstSchemas(*sourceText, settings);
    return CreateFromValidatedString(sourceText, settings);
}

XmlReader XmlReader::Create(std::istream& stream, const XmlReaderSettings& settings) {
    if (settings.Validation == ValidationType::Schema) {
        const auto startPosition = stream.tellg();
        if (startPosition != std::streampos(-1)) {
            XmlReaderSettings validationSettings = settings;
            validationSettings.Validation = ValidationType::None;

            auto validatingReader = XmlReader::Create(stream, validationSettings);
            ValidateXmlReaderInputAgainstSchemas(validatingReader, settings);

            stream.clear();
            stream.seekg(startPosition);
            if (stream.good()) {
                XmlReader reader(settings);
                std::string initialBuffer;
                if (settings.MaxCharactersInDocument != 0) {
                    initialBuffer = ReadStreamPrefix(stream, settings.MaxCharactersInDocument + 4);
                    const std::size_t bomLength = GetUtf8BomLength(initialBuffer);
                    if (initialBuffer.size() > bomLength
                        && initialBuffer.size() - bomLength > settings.MaxCharactersInDocument) {
                        throw XmlException("The XML document exceeds the configured MaxCharactersInDocument limit");
                    }
                }
                reader.inputSource_ = std::make_shared<StreamXmlReaderInputSource>(stream, std::move(initialBuffer));
                reader.InitializeInputState();
                return reader;
            }

            stream.clear();
        }

        const auto replayPath = SpoolStreamToTemporaryFile(stream);
        try {
            std::ifstream validationStream(replayPath, std::ios::binary);
            if (!validationStream) {
                throw XmlException("Failed to open temporary XML replay file");
            }

            XmlReaderSettings validationSettings = settings;
            validationSettings.Validation = ValidationType::None;
            auto validatingReader = XmlReader::Create(validationStream, validationSettings);
            ValidateXmlReaderInputAgainstSchemas(validatingReader, settings);

            auto replayStream = OpenTemporaryXmlReplayStream(replayPath);
            XmlReader reader(settings);
            std::string initialBuffer;
            if (settings.MaxCharactersInDocument != 0) {
                initialBuffer = ReadStreamPrefix(*replayStream, settings.MaxCharactersInDocument + 4);
                const std::size_t bomLength = GetUtf8BomLength(initialBuffer);
                if (initialBuffer.size() > bomLength
                    && initialBuffer.size() - bomLength > settings.MaxCharactersInDocument) {
                    throw XmlException("The XML document exceeds the configured MaxCharactersInDocument limit");
                }
            }
            reader.inputSource_ = std::make_shared<StreamXmlReaderInputSource>(replayStream, std::move(initialBuffer));
            reader.InitializeInputState();
            return reader;
        } catch (...) {
            std::error_code error;
            std::filesystem::remove(replayPath, error);
            throw;
        }
    }

    XmlReader reader(settings);
    std::string initialBuffer;
    if (settings.MaxCharactersInDocument != 0) {
        initialBuffer = ReadStreamPrefix(stream, settings.MaxCharactersInDocument + 4);
        const std::size_t bomLength = GetUtf8BomLength(initialBuffer);
        if (initialBuffer.size() > bomLength
            && initialBuffer.size() - bomLength > settings.MaxCharactersInDocument) {
            throw XmlException("The XML document exceeds the configured MaxCharactersInDocument limit");
        }
    }
    reader.inputSource_ = std::make_shared<StreamXmlReaderInputSource>(stream, std::move(initialBuffer));
    reader.InitializeInputState();
    return reader;
}

XmlReader XmlReader::CreateFromFile(const std::string& path, const XmlReaderSettings& settings) {
    if (settings.Validation == ValidationType::Schema) {
        std::ifstream validationStream(std::filesystem::path(path), std::ios::binary);
        if (!validationStream) {
            throw XmlException("Failed to open XML file: " + path);
        }

        XmlReaderSettings validationSettings = settings;
        validationSettings.Validation = ValidationType::None;
        auto validatingReader = XmlReader::Create(validationStream, validationSettings);
        ValidateXmlReaderInputAgainstSchemas(validatingReader, settings);

        auto stream = std::make_shared<std::ifstream>(std::filesystem::path(path), std::ios::binary);
        if (!*stream) {
            throw XmlException("Failed to open XML file: " + path);
        }

        XmlReader reader(settings);
        reader.inputSource_ = std::make_shared<StreamXmlReaderInputSource>(
            std::static_pointer_cast<std::istream>(stream));
        reader.InitializeInputState();
        reader.baseUri_ = std::filesystem::absolute(std::filesystem::path(path)).string();
        return reader;
    }

    auto stream = std::make_shared<std::ifstream>(std::filesystem::path(path), std::ios::binary);
    if (!*stream) {
        throw XmlException("Failed to open XML file: " + path);
    }

    std::string initialBuffer;
    if (settings.MaxCharactersInDocument != 0) {
        initialBuffer = ReadStreamPrefix(*stream, settings.MaxCharactersInDocument + 4);
        const std::size_t bomLength = GetUtf8BomLength(initialBuffer);
        if (initialBuffer.size() > bomLength
            && initialBuffer.size() - bomLength > settings.MaxCharactersInDocument) {
            throw XmlException("The XML document exceeds the configured MaxCharactersInDocument limit");
        }
    }

    XmlReader reader(settings);
    reader.inputSource_ = std::make_shared<StreamXmlReaderInputSource>(
        std::static_pointer_cast<std::istream>(stream),
        std::move(initialBuffer));
    reader.InitializeInputState();
    reader.baseUri_ = std::filesystem::absolute(std::filesystem::path(path)).string();
    return reader;
}

bool XmlReader::Read() {
    XmlReaderTokenizer tokenizer(inputSource_);
    if (closed_) {
        return false;
    }
    attributeIndex_ = -1;
    started_ = true;

    while (true) {
        ResetCurrentNode();
        MaybeDiscardSourcePrefix();
        if (TryConsumeBufferedNode()) {
            if (settings_.IgnoreWhitespace
                && (currentNodeType_ == XmlNodeType::Whitespace || currentNodeType_ == XmlNodeType::SignificantWhitespace)) {
                continue;
            }
            FinalizeSuccessfulRead();
            return true;
        }

        if (!HasSourceChar(position_)) {
            MaybeDiscardSourcePrefix();
            eof_ = true;
            return false;
        }

        if (elementStack_.empty()) {
            const auto markupKind = tokenizer.ClassifyMarkup(position_);
            if (settings_.Conformance == ConformanceLevel::Document && completedRootElement_) {
                if (IsWhitespace(Peek())) {
                    ParseText();
                    if (settings_.IgnoreWhitespace
                        && (currentNodeType_ == XmlNodeType::Whitespace || currentNodeType_ == XmlNodeType::SignificantWhitespace)) {
                        continue;
                    }
                    FinalizeSuccessfulRead();
                    return true;
                }

                if (markupKind == XmlMarkupKind::Comment) {
                    ParseComment();
                    if (settings_.IgnoreComments) {
                        continue;
                    }
                    FinalizeSuccessfulRead();
                    return true;
                }

                if (markupKind == XmlMarkupKind::ProcessingInstruction) {
                    ParseProcessingInstruction();
                    if (settings_.IgnoreProcessingInstructions) {
                        continue;
                    }
                    FinalizeSuccessfulRead();
                    return true;
                }

                if (markupKind == XmlMarkupKind::DocumentType) {
                    Throw("DOCTYPE must appear before the root element");
                }

                Throw("Unexpected content after the root element");
            }

            if (settings_.Conformance == ConformanceLevel::Document
                && markupKind == XmlMarkupKind::DocumentType
                && sawDocumentType_) {
                Throw("XML document can only contain a single DOCTYPE declaration");
            }
        }

        if (Peek() != '<') {
            ParseText();
            if (elementStack_.empty()
                && currentNodeType_ != XmlNodeType::Whitespace
                && currentNodeType_ != XmlNodeType::SignificantWhitespace) {
                xmlDeclarationAllowed_ = false;
            }
            if (settings_.IgnoreWhitespace
                && (currentNodeType_ == XmlNodeType::Whitespace || currentNodeType_ == XmlNodeType::SignificantWhitespace)) {
                continue;
            }
            FinalizeSuccessfulRead();
            return true;
        }

        switch (tokenizer.ClassifyMarkup(position_)) {
        case XmlMarkupKind::XmlDeclaration:
            if (settings_.Conformance == ConformanceLevel::Fragment) {
                Throw("XML declaration is only allowed in document conformance mode");
            }
            ParseDeclaration();
            break;
        case XmlMarkupKind::Comment:
            ParseComment();
            if (elementStack_.empty()) {
                xmlDeclarationAllowed_ = false;
            }
            if (settings_.IgnoreComments) {
                continue;
            }
            break;
        case XmlMarkupKind::CData:
            ParseCData();
            break;
        case XmlMarkupKind::ProcessingInstruction:
            ParseProcessingInstruction();
            if (elementStack_.empty()) {
                xmlDeclarationAllowed_ = false;
            }
            if (settings_.IgnoreProcessingInstructions) {
                continue;
            }
            break;
        case XmlMarkupKind::EndTag:
            ParseEndElement();
            break;
        case XmlMarkupKind::DocumentType:
            if (settings_.Conformance == ConformanceLevel::Fragment) {
                Throw("DOCTYPE is only allowed in document conformance mode");
            }
            ParseDocumentType();
            if (elementStack_.empty()) {
                xmlDeclarationAllowed_ = false;
            }
            if (settings_.DtdProcessing == DtdProcessing::Ignore) {
                continue;
            }
            break;
        case XmlMarkupKind::UnsupportedDeclaration:
            Throw("Unsupported markup declaration");
            break;
        case XmlMarkupKind::Element:
            ParseElement();
            xmlDeclarationAllowed_ = false;
            if (settings_.Conformance == ConformanceLevel::Document && elementStack_.empty() && sawRootElement_ && completedRootElement_) {
                // Existing document-mode validation remains enforced by top-level checks.
            }
            break;
        case XmlMarkupKind::None:
            Throw("Unexpected content after the root element");
            break;
        }

        FinalizeSuccessfulRead();
        return true;
    }
}

bool XmlReader::IsEOF() const noexcept {
    return eof_;
}

void XmlReader::Close() {
    closed_ = true;
    eof_ = true;
    started_ = true;
    attributeIndex_ = -1;
    ResetCurrentNode();
    bufferedNodes_.clear();
    elementStack_.clear();
    namespaceScopes_.clear();
    namespaceScopeFramePushedStack_.clear();
    xmlSpacePreserveStack_.clear();
    xmlSpacePreserveFramePushedStack_.clear();
}

XmlNodeType XmlReader::NodeType() const {
    if (attributeIndex_ >= 0) {
        return XmlNodeType::Attribute;
    }
    if (!started_ || eof_) {
        return XmlNodeType::None;
    }
    return currentNodeType_;
}

const std::string& XmlReader::Name() const {
    if (attributeIndex_ >= 0) {
        return CurrentAttributes()[static_cast<std::size_t>(attributeIndex_)].first;
    }
    if (!started_ || eof_) {
        return EmptyString();
    }
    return currentName_;
}

std::string XmlReader::LocalName() const {
    if (attributeIndex_ >= 0) {
        return CurrentAttributeLocalName(static_cast<std::size_t>(attributeIndex_));
    }
    if (!started_ || eof_) {
        return {};
    }
    return CurrentLocalName();
}

std::string XmlReader::Prefix() const {
    if (attributeIndex_ >= 0) {
        return CurrentAttributePrefix(static_cast<std::size_t>(attributeIndex_));
    }
    if (!started_ || eof_) {
        return {};
    }
    return CurrentPrefix();
}

const std::string& XmlReader::NamespaceURI() const {
    if (attributeIndex_ >= 0) {
        return CurrentAttributeNamespaceUri(static_cast<std::size_t>(attributeIndex_));
    }
    return currentNamespaceUri_;
}

const std::string& XmlReader::Value() const {
    if (attributeIndex_ >= 0) {
        return CurrentAttributeValue(static_cast<std::size_t>(attributeIndex_));
    }
    if (!started_ || eof_) {
        return EmptyString();
    }
    if (currentNodeType_ == XmlNodeType::XmlDeclaration && currentValue_.empty()) {
        currentValue_ = BuildDeclarationValue(
            currentDeclarationVersion_,
            currentDeclarationEncoding_,
            currentDeclarationStandalone_);
    }
    if (currentValue_.empty()
        && currentValueStart_ != std::string::npos
        && currentValueEnd_ != std::string::npos) {
        currentValue_ = SourceSubstr(currentValueStart_, currentValueEnd_ - currentValueStart_);
    }
    return currentValue_;
}

int XmlReader::Depth() const noexcept {
    if (!started_ || eof_) {
        return 0;
    }
    return currentDepth_ + (attributeIndex_ >= 0 ? 1 : 0);
}

bool XmlReader::IsEmptyElement() const noexcept {
    return NodeType() == XmlNodeType::Element && started_ && !eof_ && currentIsEmptyElement_;
}

bool XmlReader::HasValue() const noexcept {
    switch (NodeType()) {
    case XmlNodeType::Attribute:
    case XmlNodeType::Text:
    case XmlNodeType::CDATA:
    case XmlNodeType::EntityReference:
    case XmlNodeType::ProcessingInstruction:
    case XmlNodeType::Comment:
    case XmlNodeType::Whitespace:
    case XmlNodeType::SignificantWhitespace:
    case XmlNodeType::XmlDeclaration:
        return true;
    default:
        return false;
    }
}

int XmlReader::AttributeCount() const noexcept {
    return static_cast<int>(CurrentAttributes().size());
}

bool XmlReader::HasAttributes() const noexcept {
    return !CurrentAttributes().empty();
}

ReadState XmlReader::GetReadState() const noexcept {
    if (closed_) return ReadState::Closed;
    if (eof_) return ReadState::EndOfFile;
    if (!started_) return ReadState::Initial;
    return ReadState::Interactive;
}

bool XmlReader::HasLineInfo() const noexcept {
    return true;
}

std::size_t XmlReader::LineNumber() const noexcept {
    return lineNumber_;
}

std::size_t XmlReader::LinePosition() const noexcept {
    return linePosition_;
}

std::string XmlReader::GetAttribute(const std::string& name) const {
    const auto& attributes = CurrentAttributes();
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        const std::string_view attributeName(attributes[i].first);
        if (attributeName == name) {
            return CurrentAttributeValue(i);
        }
        if (SplitQualifiedNameView(attributeName).second == name) {
            return CurrentAttributeValue(i);
        }
    }
    return {};
}

std::string XmlReader::GetAttribute(int index) const {
    const auto& attributes = CurrentAttributes();
    if (index < 0 || static_cast<std::size_t>(index) >= attributes.size()) {
        return {};
    }
    return CurrentAttributeValue(static_cast<std::size_t>(index));
}

std::string XmlReader::GetAttribute(const std::string& localName, const std::string& namespaceUri) const {
    const auto& attributes = currentAttributes_;
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        const std::string_view attributeName(attributes[i].first);
        if (SplitQualifiedNameView(attributeName).second != localName) {
            continue;
        }
        if (CurrentAttributeNamespaceUri(i) == namespaceUri) {
            return CurrentAttributeValue(i);
        }
    }
    return {};
}

bool XmlReader::MoveToAttribute(const std::string& name) {
    const auto& attributes = CurrentAttributes();
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        const std::string_view attributeName(attributes[i].first);
        if (attributeName == name || SplitQualifiedNameView(attributeName).second == name) {
            attributeIndex_ = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

bool XmlReader::MoveToAttribute(int index) {
    const auto& attributes = CurrentAttributes();
    if (index < 0 || static_cast<std::size_t>(index) >= attributes.size()) {
        return false;
    }
    attributeIndex_ = index;
    return true;
}

bool XmlReader::MoveToAttribute(const std::string& localName, const std::string& namespaceUri) {
    for (std::size_t i = 0; i < currentAttributes_.size(); ++i) {
        const std::string_view attributeName(currentAttributes_[i].first);
        if (SplitQualifiedNameView(attributeName).second != localName) {
            continue;
        }
        if (CurrentAttributeNamespaceUri(i) == namespaceUri) {
            attributeIndex_ = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

bool XmlReader::MoveToFirstAttribute() {
    if (NodeType() != XmlNodeType::Element || CurrentAttributes().empty()) {
        return false;
    }

    attributeIndex_ = 0;
    return true;
}

bool XmlReader::MoveToNextAttribute() {
    if (attributeIndex_ < 0) {
        return false;
    }
    if (static_cast<std::size_t>(attributeIndex_ + 1) >= CurrentAttributes().size()) {
        return false;
    }

    ++attributeIndex_;
    return true;
}

bool XmlReader::MoveToElement() {
    const bool moved = attributeIndex_ >= 0;
    attributeIndex_ = -1;
    return moved;
}

std::string XmlReader::LookupNamespace(const std::string& prefix) const {
    return LookupNamespaceUri(prefix);
}

std::string XmlReader::ReadInnerXml() const {
    if (attributeIndex_ >= 0) {
        return Value();
    }
    if (NodeType() != XmlNodeType::Element) {
        return {};
    }
    if (!currentIsEmptyElement_ && currentInnerXml_.empty() && currentElementStart_ != std::string::npos) {
        const auto captured = CaptureElementXml(currentElementStart_, currentContentStart_);
        currentInnerXml_ = captured.first;
        currentOuterXml_ = captured.second;
    }
    return currentInnerXml_;
}

std::string XmlReader::ReadOuterXml() const {
    if (attributeIndex_ >= 0) {
        return Name() + "=\"" + EscapeAttribute(Value(), XmlWriterSettings{}) + "\"";
    }
    if (!started_ || eof_) {
        return {};
    }
    if (currentNodeType_ == XmlNodeType::EndElement || currentNodeType_ == XmlNodeType::EndEntity) {
        return {};
    }
    if (currentOuterXml_.empty()) {
        if (currentNodeType_ == XmlNodeType::Element && currentElementStart_ != std::string::npos) {
            if (currentIsEmptyElement_) {
                currentOuterXml_ = SourceSubstr(currentElementStart_, currentContentStart_ - currentElementStart_);
            } else {
                const auto captured = CaptureElementXml(currentElementStart_, currentContentStart_);
                currentInnerXml_ = captured.first;
                currentOuterXml_ = captured.second;
            }
        } else if (currentNodeStart_ != std::string::npos && currentNodeEnd_ != std::string::npos) {
            currentOuterXml_ = SourceSubstr(currentNodeStart_, currentNodeEnd_ - currentNodeStart_);
        }
    }
    return currentOuterXml_;
}

std::string XmlReader::ReadContentAsString() {
    std::string result;
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::Text || nt == XmlNodeType::CDATA || nt == XmlNodeType::Whitespace ||
            nt == XmlNodeType::SignificantWhitespace) {
            AppendCurrentValueTo(result);
            Read();
        } else {
            break;
        }
    }
    return result;
}

int XmlReader::ReadContentAsInt() {
    return XmlConvert::ToInt32(ReadContentAsString());
}

long long XmlReader::ReadContentAsLong() {
    return XmlConvert::ToInt64(ReadContentAsString());
}

double XmlReader::ReadContentAsDouble() {
    return XmlConvert::ToDouble(ReadContentAsString());
}

bool XmlReader::ReadContentAsBoolean() {
    return XmlConvert::ToBoolean(ReadContentAsString());
}

std::string XmlReader::ReadString() {
    // Legacy API: accumulate Text/CDATA content until a non-text node
    if (NodeType() == XmlNodeType::Element) {
        if (!Read()) return {};
    }
    std::string result;
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::Text || nt == XmlNodeType::CDATA ||
            nt == XmlNodeType::Whitespace || nt == XmlNodeType::SignificantWhitespace) {
            AppendCurrentValueTo(result);
            Read();
        } else {
            break;
        }
    }
    return result;
}

int XmlReader::ReadBase64(std::vector<unsigned char>& buffer) {
    const std::size_t originalSize = buffer.size();
    unsigned int accumulator = 0;
    int bits = 0;

    while (true) {
        const auto nodeType = NodeType();
        if (nodeType == XmlNodeType::Text || nodeType == XmlNodeType::CDATA
            || nodeType == XmlNodeType::Whitespace || nodeType == XmlNodeType::SignificantWhitespace) {
            DecodeAndAppendCurrentBase64(buffer, accumulator, bits);
            Read();
        } else {
            break;
        }
    }

    return static_cast<int>(buffer.size() - originalSize);
}

XmlNodeType XmlReader::MoveToContent() {
    MoveToElement();
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::Element || nt == XmlNodeType::Text || nt == XmlNodeType::CDATA ||
            nt == XmlNodeType::EntityReference || nt == XmlNodeType::EndElement || nt == XmlNodeType::EndEntity) {
            return nt;
        }
        if (!Read()) {
            return NodeType();
        }
    }
}

bool XmlReader::IsStartElement() {
    return MoveToContent() == XmlNodeType::Element;
}

bool XmlReader::IsStartElement(const std::string& name) {
    return MoveToContent() == XmlNodeType::Element && Name() == name;
}

void XmlReader::ReadStartElement() {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadStartElement called when the reader is not positioned on an element");
    }
    Read();
}

void XmlReader::ReadStartElement(const std::string& name) {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadStartElement called when the reader is not positioned on an element");
    }
    if (Name() != name) {
        throw XmlException("Element '" + name + "' was not found. Current element is '" + Name() + "'");
    }
    Read();
}

void XmlReader::ReadEndElement() {
    if (MoveToContent() != XmlNodeType::EndElement) {
        throw XmlException("ReadEndElement called when the reader is not positioned on an end element");
    }
    Read();
}

std::string XmlReader::ReadElementContentAsString() {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadElementContentAsString called when the reader is not positioned on an element");
    }
    if (IsEmptyElement()) {
        Read();
        return {};
    }

    std::string result;
    std::size_t closeStart = std::string::npos;
    std::size_t closeEnd = std::string::npos;
    if (TryReadSimpleElementContentAsString(result, closeStart, closeEnd)) {
        const auto [lineNumber, linePosition] = ComputeLineColumn(closeStart);
        position_ = closeStart;
        lineNumber_ = lineNumber;
        linePosition_ = linePosition;
        ParseEndElement();
        Read();
        return result;
    }

    Read();  // move past start element
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::EndElement) {
            Read();  // consume end element
            return result;
        }
        if (nt == XmlNodeType::Text || nt == XmlNodeType::CDATA || nt == XmlNodeType::Whitespace ||
            nt == XmlNodeType::SignificantWhitespace) {
            AppendCurrentValueTo(result);
            Read();
        } else if (nt == XmlNodeType::Comment || nt == XmlNodeType::ProcessingInstruction) {
            Read();
        } else {
            throw XmlException("ReadElementContentAsString encountered unexpected node type");
        }
    }
}

std::string XmlReader::ReadElementString() {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadElementString called when the reader is not positioned on an element");
    }
    if (IsEmptyElement()) {
        Read();
        return {};
    }

    std::string result;
    std::size_t closeStart = std::string::npos;
    std::size_t closeEnd = std::string::npos;
    if (TryReadSimpleElementContentAsString(result, closeStart, closeEnd)) {
        const auto [lineNumber, linePosition] = ComputeLineColumn(closeStart);
        position_ = closeStart;
        lineNumber_ = lineNumber;
        linePosition_ = linePosition;
        ParseEndElement();
        Read();
        return result;
    }

    Read();
    while (true) {
        auto nt = NodeType();
        if (nt == XmlNodeType::EndElement) {
            Read();
            return result;
        }
        if (nt == XmlNodeType::Text || nt == XmlNodeType::CDATA || nt == XmlNodeType::Whitespace ||
            nt == XmlNodeType::SignificantWhitespace) {
            AppendCurrentValueTo(result);
            Read();
        } else if (nt == XmlNodeType::Comment || nt == XmlNodeType::ProcessingInstruction) {
            Read();
        } else {
            throw XmlException("ReadElementString does not support mixed content elements");
        }
    }
}

std::string XmlReader::ReadElementString(const std::string& name) {
    if (MoveToContent() != XmlNodeType::Element) {
        throw XmlException("ReadElementString called when the reader is not positioned on an element");
    }
    if (Name() != name) {
        throw XmlException("Element '" + name + "' was not found. Current element is '" + Name() + "'");
    }
    return ReadElementString();
}

void XmlReader::Skip() {
    MoveToElement();
    if (NodeType() == XmlNodeType::Element && !IsEmptyElement()) {
        int depth = Depth();
        while (Read()) {
            if (NodeType() == XmlNodeType::EndElement && Depth() == depth) {
                Read();
                return;
            }
        }
    } else {
        Read();
    }
}

bool XmlReader::ReadToFollowing(const std::string& name) {
    while (Read()) {
        if (NodeType() == XmlNodeType::Element && Name() == name) {
            return true;
        }
    }
    return false;
}

bool XmlReader::ReadToDescendant(const std::string& name) {
    if (NodeType() != XmlNodeType::Element || IsEmptyElement()) {
        return false;
    }
    int startDepth = Depth();
    while (Read()) {
        if (NodeType() == XmlNodeType::Element && Name() == name) {
            return true;
        }
        if (NodeType() == XmlNodeType::EndElement && Depth() == startDepth) {
            return false;
        }
    }
    return false;
}

bool XmlReader::ReadToNextSibling(const std::string& name) {
    int targetDepth = Depth();
    // Skip current node
    if (NodeType() == XmlNodeType::Element && !IsEmptyElement()) {
        Skip();
    } else {
        Read();
    }
    while (!IsEOF()) {
        if (NodeType() == XmlNodeType::Element && Depth() == targetDepth && Name() == name) {
            return true;
        }
        if (NodeType() == XmlNodeType::EndElement && Depth() < targetDepth) {
            return false;
        }
        if (NodeType() == XmlNodeType::Element && !IsEmptyElement()) {
            Skip();
        } else {
            Read();
        }
    }
    return false;
}

XmlReader XmlReader::ReadSubtree() {
    if (!started_ || eof_ || NodeType() != XmlNodeType::Element) {
        return XmlReader::Create(std::string{}, XmlReaderSettings{});
    }

    XmlReaderSettings subtreeSettings = settings_;
    subtreeSettings.Conformance = ConformanceLevel::Fragment;
    if (inputSource_ == nullptr || currentElementStart_ == std::string::npos) {
        return XmlReader::Create(ReadOuterXml(), subtreeSettings);
    }

    std::size_t subtreeLength = 0;
    std::size_t closeStart = std::string::npos;
    if (currentIsEmptyElement_) {
        subtreeLength = currentContentStart_ - currentElementStart_;
    } else {
        const auto bounds = EnsureCurrentElementXmlBounds();
        closeStart = bounds.first;
        const auto closeEnd = bounds.second;
        subtreeLength = closeEnd - currentElementStart_;
    }

    static constexpr std::size_t InMemorySubtreeMaterializeThreshold = 20 * 1024 * 1024;
    const bool streamBackedSource = dynamic_cast<const StreamXmlReaderInputSource*>(inputSource_.get()) != nullptr
        || dynamic_cast<const SubrangeXmlReaderInputSource*>(inputSource_.get()) != nullptr;
    const bool canUseMaterializedFastPath = subtreeSettings.Validation == ValidationType::None
        && subtreeSettings.MaxCharactersInDocument == 0
        && subtreeSettings.MaxCharactersFromEntities == 0;
    if (streamBackedSource
        && canUseMaterializedFastPath
        && subtreeLength <= InMemorySubtreeMaterializeThreshold) {
        return XmlReader::Create(SourceSubstr(currentElementStart_, subtreeLength), subtreeSettings);
    }

    inputSource_->EnableReplay();

    XmlReader reader(subtreeSettings);
    reader.inputSource_ = std::make_shared<SubrangeXmlReaderInputSource>(inputSource_, currentElementStart_, subtreeLength);
    reader.InitializeInputState();
    return reader;
}


}  // namespace System::Xml
