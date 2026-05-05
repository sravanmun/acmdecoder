#pragma once

#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <memory>
#include <string>
#include <stdexcept>
#include <vector>
#include <algorithm>

#include <zstd.h>
#include <bzlib.h>


// Abstract base class for reading decompressed bytes
class InputStream {
public:
    virtual ~InputStream() = default;

    // Read up to `size` bytes into `buf`. Returns bytes read, 0 on EOF.
    virtual ssize_t read(void* buf, size_t size) = 0;

    // Close the underlying resource.
    virtual void close() = 0;
};


// ---- PlainStream -----------------------------------------------------------
class PlainStream : public InputStream {
public:
    explicit PlainStream(const std::string& path) {
        fd_ = ::open(path.c_str(), O_RDONLY);
        if (fd_ < 0)
            throw std::runtime_error("PlainStream: cannot open " + path +
                                     ": " + std::strerror(errno));
    }

    ~PlainStream() override { close(); }

    ssize_t read(void* buf, size_t size) override {
        ssize_t n = ::read(fd_, buf, size);
        if (n < 0) {
            if (errno == EINTR) return read(buf, size);
            throw std::runtime_error(std::string("PlainStream read: ") +
                                     std::strerror(errno));
        }
        return n;
    }

    void close() override {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

private:
    int fd_{-1};
};


// ---- ZstdStream ------------------------------------------------------------
class ZstdStream : public InputStream {
public:
    explicit ZstdStream(const std::string& path) {
        fd_ = ::open(path.c_str(), O_RDONLY);
        if (fd_ < 0)
            throw std::runtime_error("ZstdStream: cannot open " + path +
                                     ": " + std::strerror(errno));

        dctx_ = ZSTD_createDStream();
        if (!dctx_)
            throw std::runtime_error("ZstdStream: ZSTD_createDStream failed");

        size_t rc = ZSTD_initDStream(dctx_);
        if (ZSTD_isError(rc))
            throw std::runtime_error(std::string("ZstdStream: ZSTD_initDStream: ") +
                                     ZSTD_getErrorName(rc));

        in_buf_.resize(ZSTD_DStreamInSize());
    }

    ~ZstdStream() override { close(); }

    ssize_t read(void* buf, size_t size) override {
        ZSTD_outBuffer out{buf, size, 0};

        while (out.pos < out.size) {
            // If input is exhausted, read more compressed data
            if (zin_.pos >= zin_.size) {
                ssize_t n = ::read(fd_, in_buf_.data(), in_buf_.size());
                if (n < 0) {
                    if (errno == EINTR) continue;
                    throw std::runtime_error(std::string("ZstdStream read: ") +
                                             std::strerror(errno));
                }
                if (n == 0) break;  // EOF on compressed file
                zin_ = {in_buf_.data(), static_cast<size_t>(n), 0};
            }

            size_t rc = ZSTD_decompressStream(dctx_, &out, &zin_);
            if (ZSTD_isError(rc))
                throw std::runtime_error(std::string("ZstdStream decompress: ") +
                                         ZSTD_getErrorName(rc));
            if (rc == 0) break;  // frame complete
        }

        return static_cast<ssize_t>(out.pos);
    }

    void close() override {
        if (dctx_) { ZSTD_freeDStream(dctx_); dctx_ = nullptr; }
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

private:
    int fd_{-1};
    ZSTD_DStream* dctx_{nullptr};
    std::vector<uint8_t> in_buf_;
    ZSTD_inBuffer zin_{nullptr, 0, 0};
};


// ---- BZ2Stream -------------------------------------------------------------
class BZ2Stream : public InputStream {
public:
    explicit BZ2Stream(const std::string& path) {
        fd_ = ::open(path.c_str(), O_RDONLY);
        if (fd_ < 0)
            throw std::runtime_error("BZ2Stream: cannot open " + path +
                                     ": " + std::strerror(errno));

        std::memset(&bz_, 0, sizeof(bz_));
        int rc = BZ2_bzDecompressInit(&bz_, 0, 0);
        if (rc != BZ_OK)
            throw std::runtime_error("BZ2Stream: BZ2_bzDecompressInit failed (" +
                                     std::to_string(rc) + ")");
        init_ = true;

        in_buf_.resize(1024 * 1024);  // 1 MB compressed read buffer
    }

    ~BZ2Stream() override { close(); }

    ssize_t read(void* buf, size_t size) override {
        bz_.next_out  = static_cast<char*>(buf);
        bz_.avail_out = static_cast<unsigned>(size);

        while (bz_.avail_out > 0) {
            // If input is exhausted, read more compressed data
            if (bz_.avail_in == 0) {
                ssize_t n = ::read(fd_, in_buf_.data(), in_buf_.size());
                if (n < 0) {
                    if (errno == EINTR) continue;
                    throw std::runtime_error(std::string("BZ2Stream read: ") +
                                             std::strerror(errno));
                }
                if (n == 0) break;  // EOF on compressed file
                bz_.next_in  = reinterpret_cast<char*>(in_buf_.data());
                bz_.avail_in = static_cast<unsigned>(n);
            }

            int rc = BZ2_bzDecompress(&bz_);
            if (rc == BZ_STREAM_END) { eof_ = true; break; }
            if (rc != BZ_OK)
                throw std::runtime_error("BZ2Stream decompress failed (" +
                                         std::to_string(rc) + ")");
        }

        return static_cast<ssize_t>(size - bz_.avail_out);
    }

    void close() override {
        if (init_) { BZ2_bzDecompressEnd(&bz_); init_ = false; }
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

private:
    int fd_{-1};
    bz_stream bz_{};
    bool init_{false};
    bool eof_{false};
    std::vector<uint8_t> in_buf_;
};


// Helper for extension matching
static inline bool ends_with(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

// Factory: picks the right stream based on file extension
inline std::unique_ptr<InputStream> make_stream(const std::string& path) {
    if (ends_with(path, ".zst"))
        return std::make_unique<ZstdStream>(path);
    if (ends_with(path, ".bz2"))
        return std::make_unique<BZ2Stream>(path);
    return std::make_unique<PlainStream>(path);
}
