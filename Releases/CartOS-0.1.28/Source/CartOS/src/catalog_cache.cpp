#include "catalog_cache.hpp"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace catalog_cache {
namespace {
constexpr const char* kHeader="CartOSGameCatalog1";
constexpr std::size_t kMaxEntries=200000;
constexpr off_t kMaxFileBytes=64*1024*1024;

void mix(std::uint64_t& hash,const std::string& text){
    for(unsigned char byte:text){ hash^=byte; hash*=1099511628211ULL; }
    hash^=0xff;
    hash*=1099511628211ULL;
}

bool inside(const std::string& path,const std::string& root){
    return path.size()>root.size() && path.compare(0,root.size(),root)==0 && path[root.size()]=='/';
}

bool validEntry(const Entry& entry,const System& system){
    if(entry.title.empty() || entry.title.size()>4096 || entry.romPath.size()>4096 ||
       entry.artworkPath.size()>4096) return false;
    bool inRomRoot=false;
    for(const std::string& root:system.romPaths)
        if(inside(entry.romPath,root)){ inRomRoot=true; break; }
    if(!inRomRoot) return false;
    if(entry.artworkPath.empty()) return true;
    if(inside(entry.artworkPath,system.artworkPath)) return true;
    for(const std::string& root:system.romPaths)
        if(inside(entry.artworkPath,root)) return true;
    return false;
}
} // namespace

std::string fingerprint(const std::vector<System>& systems){
    std::uint64_t hash=14695981039346656037ULL;
    mix(hash,"CartOSGameCatalog1");
    for(const System& system:systems){
        mix(hash,system.id);
        mix(hash,system.label);
        mix(hash,system.launcherPath);
        mix(hash,system.artworkPath);
        for(const std::string& path:system.romPaths) mix(hash,path);
        mix(hash,"rom-paths-end");
        for(const std::string& extension:system.extensions) mix(hash,extension);
        mix(hash,"extensions-end");
    }
    std::ostringstream output;
    output<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;
    return output.str();
}

bool load(const std::string& path,const std::string& expected,
          const std::vector<System>& systems,Listings& listings){
    struct stat info{};
    if(stat(path.c_str(),&info)!=0 || !S_ISREG(info.st_mode) || info.st_size<0 ||
       info.st_size>kMaxFileBytes) return false;
    std::ifstream input(path);
    if(!input) return false;
    std::string header,actual;
    std::size_t systemCount=0;
    if(!(input>>header>>actual>>systemCount) || header!=kHeader || actual!=expected ||
       systemCount!=systems.size()) return false;
    Listings parsed;
    parsed.reserve(systemCount);
    std::size_t total=0;
    for(std::size_t i=0;i<systemCount;++i){
        char marker=0;
        std::string id;
        std::size_t count=0;
        if(!(input>>marker>>std::quoted(id)>>count) || marker!='S' ||
           id!=systems[i].id || count>kMaxEntries-total) return false;
        std::vector<Entry> entries;
        entries.reserve(count);
        for(std::size_t j=0;j<count;++j){
            Entry entry;
            if(!(input>>marker>>std::quoted(entry.title)>>std::quoted(entry.romPath)
                      >>std::quoted(entry.artworkPath)) || marker!='G' ||
               !validEntry(entry,systems[i])) return false;
            entries.push_back(std::move(entry));
        }
        total+=count;
        parsed.push_back(std::move(entries));
    }
    input>>std::ws;
    if(input.peek()!=std::char_traits<char>::eof()) return false;
    listings=std::move(parsed);
    return true;
}

bool save(const std::string& path,const std::string& signature,
          const std::vector<System>& systems,const Listings& listings){
    if(systems.size()!=listings.size()) return false;
    const std::size_t slash=path.find_last_of('/');
    if(slash==std::string::npos) return false;
    const std::string directory=path.substr(0,slash);
    if(mkdir(directory.c_str(),0755)!=0 && errno!=EEXIST) return false;
    const std::string temporary=path+".tmp";
    std::ofstream output(temporary,std::ios::trunc);
    if(!output) return false;
    output<<kHeader<<' '<<signature<<' '<<systems.size()<<'\n';
    for(std::size_t i=0;i<systems.size();++i){
        output<<"S "<<std::quoted(systems[i].id)<<' '<<listings[i].size()<<'\n';
        for(const Entry& entry:listings[i]){
            if(!validEntry(entry,systems[i])){ output.close(); unlink(temporary.c_str()); return false; }
            output<<"G "<<std::quoted(entry.title)<<' '<<std::quoted(entry.romPath)
                  <<' '<<std::quoted(entry.artworkPath)<<'\n';
        }
    }
    output.close();
    if(output && rename(temporary.c_str(),path.c_str())==0) return true;
    unlink(temporary.c_str());
    return false;
}
} // namespace catalog_cache
