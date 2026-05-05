#include "stream.hpp"

#include <algorithm>


static bool ends_with(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}


std::unique_ptr<InputStream> make_stream(const std::string& path) {
    if (ends_with(path, ".zst"))
        return std::make_unique<ZstdStream>(path);
    if (ends_with(path, ".bz2"))
        return std::make_unique<BZ2Stream>(path);
    return std::make_unique<PlainStream>(path);
}
