#pragma once
#include <map>
#include <string>
#include <vector>

namespace artwork {
// Filename-only index. PNG decoding remains the preview renderer's job.
class Index {
public:
    bool open(const std::string& root, const std::string& cacheFile, bool rebuild = false);
    std::vector<std::string> images(const std::string& relativeRomPath) const;
    bool loadedFromDisk() const { return loadedFromDisk_; }
    size_t fileCount() const { return fileCount_; }
private:
    bool load(const std::string& path);
    bool scan(const std::string& relativeDirectory, unsigned depth);
    bool save(const std::string& path) const;
    std::string root_;
    std::map<std::string, std::vector<std::string>> directories_;
    size_t fileCount_ = 0;
    bool loadedFromDisk_ = false;
};
}
