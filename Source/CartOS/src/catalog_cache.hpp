#pragma once

#include <string>
#include <vector>

namespace catalog_cache {

struct System {
    std::string id;
    std::string label;
    std::string launcherPath;
    std::string artworkPath;
    std::vector<std::string> romPaths;
    std::vector<std::string> extensions;
};

struct Entry {
    std::string title;
    std::string romPath;
    std::string artworkPath;
};

using Listings = std::vector<std::vector<Entry>>;

std::string fingerprint(const std::vector<System>& systems);
bool load(const std::string& path, const std::string& fingerprint,
          const std::vector<System>& systems, Listings& listings);
bool save(const std::string& path, const std::string& fingerprint,
          const std::vector<System>& systems, const Listings& listings);

} // namespace catalog_cache
