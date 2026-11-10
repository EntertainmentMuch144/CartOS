#include "artwork_index.hpp"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace artwork {
namespace {
const std::string magic = "CARTOS-ART-1\n";
constexpr size_t maxIndexBytes = 16 * 1024 * 1024;
constexpr size_t maxFiles = 100000;
constexpr size_t maxString = 4096;

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
bool nameLess(const std::string& a, const std::string& b) { return lower(a) < lower(b); }
bool relativePath(const std::string& path) {
    if (!path.empty() && path.front() == '/') return false;
    if (path.find('\0') != std::string::npos) return false;
    size_t start = 0;
    while (start < path.size()) {
        const size_t end = path.find('/', start);
        const std::string part = path.substr(start, end == std::string::npos ? end : end-start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        start = end+1;
    }
    return path.empty() || path.back() != '/';
}
uint32_t checksum(const std::string& data, size_t length) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; ++i) {
        hash ^= static_cast<unsigned char>(data[i]);
        hash *= 16777619u;
    }
    return hash;
}
void putNumber(std::string& out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<char>(value >> (i*8)));
}
void putString(std::string& out, const std::string& value) {
    putNumber(out, static_cast<uint32_t>(value.size()));
    out += value;
}
struct Reader {
    const std::string& data;
    size_t at;
    bool number(uint32_t& value) {
        if (at > data.size() || data.size()-at < 4) return false;
        value = 0;
        for (unsigned i = 0; i < 4; ++i)
            value |= static_cast<uint32_t>(static_cast<unsigned char>(data[at++])) << (i*8);
        return true;
    }
    bool string(std::string& value) {
        uint32_t length;
        if (!number(length) || length > maxString || data.size()-at < length) return false;
        value = data.substr(at, length);
        at += length;
        return value.find('\0') == std::string::npos;
    }
};
bool makeDirectories(const std::string& path) {
    if (path.empty()) return true;
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != '/') continue;
        if (mkdir(path.substr(0, i).c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return true;
}
}

bool Index::open(const std::string& root, const std::string& cacheFile, bool rebuild) {
    root_ = root;
    while (root_.size() > 1 && root_.back() == '/') root_.pop_back();
    directories_.clear();
    fileCount_ = 0;
    loadedFromDisk_ = false;
    if (!rebuild && load(cacheFile)) {
        loadedFromDisk_ = true;
        return true;
    }
    directories_.clear();
    fileCount_ = 0;
    if (!scan("", 0)) {
        directories_.clear();
        fileCount_ = 0;
        return false;
    }
    // A read-only/full card still gets a usable in-memory index for this session.
    if (!save(cacheFile)) std::fprintf(stderr, "Artwork index could not be saved: %s\n", cacheFile.c_str());
    return true;
}

bool Index::scan(const std::string& relativeDirectory, unsigned depth) {
    if (depth > 16 || directories_.size() >= maxFiles) return false;
    const std::string directory = root_ + (relativeDirectory.empty() ? "" : "/"+relativeDirectory);
    DIR* handle = opendir(directory.c_str());
    if (!handle) return errno == ENOENT;
    auto& names = directories_[relativeDirectory];
    bool success = true;
    for (;;) {
        errno = 0;
        dirent* entry = readdir(handle);
        if (!entry) { success = errno == 0; break; }
        const std::string name = entry->d_name;
        if (name.empty() || name.front() == '.') continue;
        const std::string relative = relativeDirectory.empty() ? name : relativeDirectory+"/"+name;
        if (relative.size() > maxString) { success = false; break; }
        unsigned type = entry->d_type;
        if (type == DT_UNKNOWN) {
            struct stat info{};
            if (lstat((root_+"/"+relative).c_str(), &info) != 0) { success = false; break; }
            type = S_ISDIR(info.st_mode) ? DT_DIR : (S_ISREG(info.st_mode) ? DT_REG : DT_LNK);
        }
        if (type == DT_DIR) {
            if (!scan(relative, depth+1)) { success = false; break; }
        } else if (type == DT_REG && name.size() > 4 && lower(name.substr(name.size()-4)) == ".png") {
            if (++fileCount_ > maxFiles) { success = false; break; }
            names.push_back(name);
        }
    }
    closedir(handle);
    std::sort(names.begin(), names.end(), nameLess);
    return success;
}

bool Index::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto length = file.tellg();
    if (length < static_cast<std::streamoff>(magic.size()+12) || length > static_cast<std::streamoff>(maxIndexBytes)) return false;
    std::string data(static_cast<size_t>(length), '\0');
    file.seekg(0);
    if (!file.read(&data[0], length) || data.compare(0, magic.size(), magic) != 0) return false;
    Reader tail{data, data.size()-4};
    uint32_t expected;
    if (!tail.number(expected) || checksum(data, data.size()-4) != expected) return false;
    Reader input{data, magic.size()};
    std::string root;
    uint32_t count;
    if (!input.string(root) || root != root_ || !input.number(count) || count > maxFiles) return false;
    for (uint32_t i = 0; i < count; ++i) {
        std::string directory;
        uint32_t files;
        if (!input.string(directory) || !relativePath(directory) || directories_.count(directory) ||
            !input.number(files) || files > maxFiles-fileCount_) return false;
        auto& names = directories_[directory];
        for (uint32_t j = 0; j < files; ++j) {
            std::string name;
            if (!input.string(name) || name.empty() || name.front() == '.' || name.find('/') != std::string::npos ||
                name.size() <= 4 || lower(name.substr(name.size()-4)) != ".png") return false;
            names.push_back(std::move(name));
        }
        if (!std::is_sorted(names.begin(), names.end(), nameLess)) return false;
        fileCount_ += files;
    }
    return input.at == data.size()-4;
}

bool Index::save(const std::string& path) const {
    if (root_.size() > maxString) return false;
    std::string data = magic;
    putString(data, root_);
    putNumber(data, static_cast<uint32_t>(directories_.size()));
    for (const auto& directory : directories_) {
        putString(data, directory.first);
        putNumber(data, static_cast<uint32_t>(directory.second.size()));
        for (const std::string& name : directory.second) putString(data, name);
        if (data.size() > maxIndexBytes-4) return false;
    }
    putNumber(data, checksum(data, data.size()));
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && !makeDirectories(path.substr(0, slash))) return false;
    const std::string temporary = path+".tmp-"+std::to_string(getpid());
    FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) return false;
    bool success = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    if (std::fflush(file) != 0 || fsync(fileno(file)) != 0) success = false;
    if (std::fclose(file) != 0) success = false;
    if (success && std::rename(temporary.c_str(), path.c_str()) == 0) return true;
    std::remove(temporary.c_str());
    return false;
}

std::vector<std::string> Index::images(const std::string& relativeRomPath) const {
    if (relativeRomPath.empty() || !relativePath(relativeRomPath)) return {};
    const size_t slash = relativeRomPath.find_last_of('/');
    const std::string directory = slash == std::string::npos ? "" : relativeRomPath.substr(0, slash);
    const std::string filename = slash == std::string::npos ? relativeRomPath : relativeRomPath.substr(slash+1);
    const std::string title = filename.substr(0, filename.find_last_of('.'));
    const std::string single = lower(title+".png"), prefix = lower(title+" - ");
    std::vector<std::string> result;
    auto flat = directories_.find(directory);
    if (flat != directories_.end()) {
        const auto& names = flat->second;
        auto exact = std::lower_bound(names.begin(), names.end(), single,
            [](const std::string& name, const std::string& key) { return lower(name) < key; });
        for (; exact != names.end() && lower(*exact) == single; ++exact)
            result.push_back(root_+"/"+(directory.empty() ? "" : directory+"/")+*exact);
        auto view = std::lower_bound(names.begin(), names.end(), prefix,
            [](const std::string& name, const std::string& key) { return lower(name) < key; });
        for (; view != names.end(); ++view) {
            const std::string key = lower(*view);
            if (key.compare(0, prefix.size(), prefix) != 0) break;
            if (key.size() > prefix.size()+4)
                result.push_back(root_+"/"+(directory.empty() ? "" : directory+"/")+*view);
        }
    }
    auto legacy = directories_.find(relativeRomPath);
    if (legacy != directories_.end())
        for (const std::string& name : legacy->second) result.push_back(root_+"/"+relativeRomPath+"/"+name);
    std::sort(result.begin(), result.end(), [&single](const std::string& a, const std::string& b) {
        const std::string an = lower(a.substr(a.find_last_of('/')+1)), bn = lower(b.substr(b.find_last_of('/')+1));
        if ((an == single) != (bn == single)) return an == single;
        return an == bn ? lower(a) < lower(b) : an < bn;
    });
    return result;
}
}
