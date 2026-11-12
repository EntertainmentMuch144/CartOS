
#include "launcher.hpp"
#include "artwork.hpp"
#include "catalog_cache.hpp"
#include "draw.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dlfcn.h>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits.h>
#include <random>
#include <sstream>
#include <sys/stat.h>
#include <unordered_set>
#include <unistd.h>
#if defined(LAUNCHER_DEVICE)
#include <linux/fb.h>
#include <sys/ioctl.h>
#endif

namespace {
constexpr int kDesignW=752, kDesignH=560;
constexpr const char* kBrowserStatePath=".browser-state";
constexpr const char* kRecentHistoryPath="/App/CartOS/recent-history.txt";
constexpr const char* kGameCatalogPath="/App/CartOS/cache/game-catalog-v1.txt";
constexpr int kGeneralSettingsCount=13;
constexpr float kLaunchAnimationDuration=1.20f;
constexpr float kLaunchCenterDuration=0.16f;
constexpr float kLaunchReverseDuration=0.32f;
constexpr Uint64 kLaunchDoubleTapWindowMs=380;
void fillRect(SDL_Renderer* renderer,const SDL_Rect& rect,SDL_Color color);
void outlineRect(SDL_Renderer* renderer,const SDL_Rect& rect,SDL_Color color);
std::string shortLabel(std::string value,size_t limit);
constexpr const char* kSearchKeyboardRows[]={
    "ABCDEFGHIJ",
    "KLMNOPQRST",
    "UVWXYZ0123",
    "456789 -'."
};
SDL_Color white{240,240,240,255}, muted{125,128,134,255};

char searchKeyboardCharacter(int index){
    const int clamped=std::max(0,std::min(39,index));
    return kSearchKeyboardRows[clamped/10][clamped%10];
}

int wrapIndex(int index,int count){
    index%=count;
    return index<0 ? index+count : index;
}

std::string lower(std::string value){
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return value;
}

char titleInitialGroup(const std::string& title){
    if(title.empty()) return '#';
    const unsigned char first=static_cast<unsigned char>(title.front());
    return std::isalpha(first)?static_cast<char>(std::toupper(first)):'#';
}

bool supportedRom(const std::string& filename,const SystemInfo& system){
    const size_t dot=filename.find_last_of('.');
    if(dot==std::string::npos || dot==0) return false;
    const std::string ext=lower(filename.substr(dot));
    return std::find(system.extensions.begin(),system.extensions.end(),ext)!=system.extensions.end();
}

bool isUtilityEntry(const std::string& title,const std::string& filename){
    auto isUtilityName=[](std::string name){
        const size_t first=name.find_first_not_of(" \t\r\n");
        if(first==std::string::npos) return false;
        const size_t last=name.find_last_not_of(" \t\r\n");
        name=name.substr(first,last-first+1);
        const size_t extension=name.find_last_of('.');
        if(extension!=std::string::npos) name.resize(extension);
        name=lower(name);
        return name=="~filter" || name=="~refresh roms" || name=="~search" || name=="search";
    };
    return isUtilityName(title) || isUtilityName(filename);
}

std::string jsonString(const std::string& text,const std::string& key){
    const std::string marker="\""+key+"\"";
    size_t pos=text.find(marker);
    if(pos==std::string::npos) return {};
    pos=text.find(':',pos+marker.size());
    if(pos==std::string::npos) return {};
    ++pos;
    while(pos<text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if(pos>=text.size() || text[pos]!='\"') return {};
    ++pos;
    std::string value;
    bool escaped=false;
    for(;pos<text.size();++pos){
        const char c=text[pos];
        if(escaped){ value.push_back(c); escaped=false; }
        else if(c=='\\') escaped=true;
        else if(c=='\"') break;
        else value.push_back(c);
    }
    return value;
}

std::string readText(const std::string& path){
    std::ifstream file(path.c_str());
    if(!file) return {};
    std::ostringstream contents;
    contents<<file.rdbuf();
    return contents.str();
}

std::string sdPath(const std::string& path,const std::string& sdRoot);

std::vector<std::string> splitPipeList(const std::string& value){
    std::vector<std::string> result;
    std::istringstream input(value);
    std::string item;
    while(std::getline(input,item,'|')){
        const size_t first=item.find_first_not_of(" \t\r\n");
        if(first==std::string::npos) continue;
        const size_t last=item.find_last_not_of(" \t\r\n");
        item=item.substr(first,last-first+1);
        if(!item.empty()) result.push_back(item);
    }
    return result;
}

std::string resolveConfigPath(const std::string& value,const std::string& sdRoot,
                              const std::string& relativeTo){
    if(value.empty()) return {};
    if(value[0]=='/') return sdPath(value,sdRoot);
    return relativeTo+"/"+value;
}

int readBatteryPercent(){
    DIR* directory=opendir("/sys/class/power_supply");
    if(directory){
        int percentage=-1;
        while(const dirent* entry=readdir(directory)){
            const std::string name=entry->d_name;
            if(name=="." || name=="..") continue;
            const std::string base="/sys/class/power_supply/"+name+"/";
            std::string type=readText(base+"type");
            type.erase(std::remove_if(type.begin(),type.end(),[](unsigned char c){return std::isspace(c);}),type.end());
            if(lower(type)!="battery" && lower(name).find("battery")==std::string::npos) continue;
            std::istringstream capacity(readText(base+"capacity"));
            int value=-1;
            if(capacity>>value && value>=0 && value<=100){percentage=value;break;}
        }
        closedir(directory);
        if(percentage>=0) return percentage;
    }
    int percentage=-1;
    SDL_GetPowerInfo(nullptr,&percentage);
    return percentage>=0 && percentage<=100 ? percentage : -1;
}

std::string sdPath(const std::string& path,const std::string& sdRoot){
    static const std::string deviceRoot="/mnt/SDCARD";
    if(path.compare(0,deviceRoot.size(),deviceRoot)==0 &&
       (path.size()==deviceRoot.size() || path[deviceRoot.size()]=='/'))
        return sdRoot+path.substr(deviceRoot.size());
    return path;
}

std::string jsonEscape(const std::string& value);

std::string onionPath(const std::string& path,const std::string& sdRoot){
    if(path.compare(0,sdRoot.size(),sdRoot)==0 &&
       (path.size()==sdRoot.size() || path[sdRoot.size()]=='/'))
        return std::string("/mnt/SDCARD")+path.substr(sdRoot.size());
    return path;
}

std::string serializedListEntry(const Game& game,const std::string& sdRoot){
    const std::string romMarker="/Roms/";
    const size_t romsPos=game.romPath.find(romMarker);
    const size_t launcherSlash=game.launcherPath.find_last_of('/');
    if(romsPos==std::string::npos || launcherSlash==std::string::npos) return {};
    const std::string emuDir=onionPath(game.launcherPath.substr(0,launcherSlash),sdRoot);
    const std::string relativeRom=game.romPath.substr(romsPos+romMarker.size());
    const std::string romPath=emuDir+"/../../Roms/"+relativeRom;
    std::string imagePath;
    const size_t imagePos=game.artPath.find(romMarker);
    if(imagePos!=std::string::npos)
        imagePath=emuDir+"/../../Roms/"+game.artPath.substr(imagePos+romMarker.size());
    return "{\"label\":\""+jsonEscape(game.title)+"\",\"launch\":\""+
           jsonEscape(onionPath(game.launcherPath,sdRoot))+"\",\"type\":5,\"imgpath\":\""+
           jsonEscape(imagePath)+"\",\"rompath\":\""+jsonEscape(romPath)+"\"}";
}

std::string comparableRomPath(const std::string& path,const std::string& sdRoot){
    const std::string converted=sdPath(path,sdRoot);
    std::vector<std::string> components;
    std::istringstream input(converted);
    std::string component;
    while(std::getline(input,component,'/')){
        if(component.empty() || component==".") continue;
        if(component=="..") {if(!components.empty()) components.pop_back();continue;}
        components.push_back(component);
    }
    std::string normalized;
    for(const std::string& item:components) normalized+="/"+item;
    return normalized.empty()?"/":normalized;
}

std::string recentEntryKey(const Game& entry,const std::string& sdRoot){
    if(entry.kind==EntryKind::App){
        if(entry.workingDirectory.empty()) return "A "+entry.launcherPath;
        return "A "+onionPath(entry.workingDirectory,sdRoot);
    }
    if(entry.romPath.empty()) return {};
    return "G "+onionPath(comparableRomPath(entry.romPath,sdRoot),sdRoot);
}

bool regularFile(const std::string& path){
    struct stat info{};
    return stat(path.c_str(),&info)==0 && S_ISREG(info.st_mode);
}

std::string filenameWithoutExtension(const std::string& path){
    const size_t slash=path.find_last_of('/');
    std::string name=slash==std::string::npos?path:path.substr(slash+1);
    const size_t dot=name.find_last_of('.');
    if(dot!=std::string::npos && dot>0) name.resize(dot);
    return name;
}

std::string shellDoubleQuote(const std::string& value){
    std::string quoted="\"";
    for(char c:value){
        if(c=='\\' || c=='\"' || c=='$' || c=='`') quoted.push_back('\\');
        quoted.push_back(c);
    }
    quoted.push_back('\"');
    return quoted;
}

std::string shellPathArgument(const std::string& path){
    const bool simple=std::all_of(path.begin(),path.end(),[](unsigned char c){
        return std::isalnum(c) || c=='/' || c=='.' || c=='_' || c=='-';
    });
    return simple?path:shellDoubleQuote(path);
}

std::string onionAppLaunchCommand(const std::string& launcher,
                                  const std::string& workingDirectory,
                                  const std::string& sdRoot,
                                  bool musicPlayer){
    const std::string appDirectory=onionPath(workingDirectory,sdRoot);
    const std::string deviceLauncher=onionPath(launcher,sdRoot);
    std::string launchTarget=deviceLauncher;
    if(!workingDirectory.empty() && launcher.compare(0,workingDirectory.size(),workingDirectory)==0 &&
       launcher.size()>workingDirectory.size() && launcher[workingDirectory.size()]=='/')
        launchTarget="./"+launcher.substr(workingDirectory.size()+1);

    std::string command;
    // OnionOS 4.3.1 inspects the second whitespace-delimited field in this
    // command to locate an app's optional full_resolution marker. Keep a
    // plain safe path unquoted so that runtime.sh can parse the same command
    // shape MainUI emits. shellPathArgument still quotes paths with spaces or
    // shell-sensitive characters.
    if(!appDirectory.empty()) command="cd "+shellPathArgument(appDirectory)+"; ";
    command+="chmod a+x "+shellPathArgument(launchTarget)+"; ";
    // GMU (OnionOS Music Player) stops OnionOS's audio server and starts its
    // own SDL 1.2 stack. Give the freshly handed-off runtime a moment to
    // settle before GMU performs that transition. Other apps keep the normal
    // MainUI-compatible command with no added delay.
    if(musicPlayer) command+="sleep 1; ";
    command+="LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so "+
             shellPathArgument(launchTarget);
    return command;
}

std::string launchInvocation(const std::string& path){
    const size_t dot=path.find_last_of('.');
    if(dot!=std::string::npos && lower(path.substr(dot))==".sh")
        return "/bin/sh "+shellDoubleQuote(path);
    return shellDoubleQuote(path);
}

std::string jsonEscape(const std::string& value){
    std::string escaped;
    escaped.reserve(value.size()+8);
    for(unsigned char c:value){
        switch(c){
            case '"': escaped+="\\\""; break;
            case '\\': escaped+="\\\\"; break;
            case '\b': escaped+="\\b"; break;
            case '\f': escaped+="\\f"; break;
            case '\n': escaped+="\\n"; break;
            case '\r': escaped+="\\r"; break;
            case '\t': escaped+="\\t"; break;
            default:
                if(c<0x20){
                    static const char hex[]="0123456789abcdef";
                    escaped+="\\u00";
                    escaped.push_back(hex[(c>>4)&0xf]);
                    escaped.push_back(hex[c&0xf]);
                }else escaped.push_back(static_cast<char>(c));
        }
    }
    return escaped;
}

bool writeOnionRecentGame(const Game& game){
    const std::string deviceRoot="/mnt/SDCARD";
    const std::string romsMarker="/Roms/";
    const size_t romsPos=game.romPath.find(romsMarker);
    const size_t launcherSlash=game.launcherPath.find_last_of('/');
    if(romsPos==std::string::npos || launcherSlash==std::string::npos) return false;

    const std::string emuDir=game.launcherPath.substr(0,launcherSlash);
    const std::string relativeRom=game.romPath.substr(romsPos+romsMarker.size());
    const std::string recentRomPath=emuDir+"/../../Roms/"+relativeRom;
    std::string recentImagePath;
    const size_t imageRomsPos=game.artPath.find(romsMarker);
    if(imageRomsPos!=std::string::npos)
        recentImagePath=emuDir+"/../../Roms/"+game.artPath.substr(imageRomsPos+romsMarker.size());

    const std::string recentFile=deviceRoot+"/Roms/recentlist.json";
    const std::string temporaryFile=recentFile+".cartos.tmp";
    const std::string newLine="{\"label\":\""+jsonEscape(game.title)+"\",\"rompath\":\""+
                             jsonEscape(recentRomPath)+"\",\"imgpath\":\""+jsonEscape(recentImagePath)+
                             "\",\"launch\":\""+jsonEscape(game.launcherPath)+"\",\"type\":5}\n";
    {
        std::ofstream output(temporaryFile.c_str(),std::ios::out|std::ios::trunc);
        if(!output) return false;
        output<<newLine;
        std::ifstream existing(recentFile.c_str());
        std::string line;
        while(std::getline(existing,line)){
            if(jsonString(line,"rompath")==recentRomPath) continue;
            output<<line<<'\n';
        }
        output.close();
        if(!output){unlink(temporaryFile.c_str()); return false;}
    }
    if(rename(temporaryFile.c_str(),recentFile.c_str())!=0){
        unlink(temporaryFile.c_str());
        return false;
    }
    return true;
}

#if defined(LAUNCHER_DEVICE)
void resetFramebufferToTopPage(){
    const int framebuffer=open("/dev/fb0",O_RDWR);
    if(framebuffer<0) return;
    fb_var_screeninfo mode{};
    if(ioctl(framebuffer,FBIOGET_VSCREENINFO,&mode)==0 && mode.yoffset!=0){
        mode.yoffset=0;
        mode.activate=FB_ACTIVATE_NOW;
        ioctl(framebuffer,FBIOPAN_DISPLAY,&mode);
    }
    close(framebuffer);
}
#endif

void clearArtwork(std::vector<Game>& entries){
    for(Game& entry:entries){
        if(entry.artwork){ SDL_DestroyTexture(entry.artwork); entry.artwork=nullptr; }
        entry.artworkAttempted=false;
    }
}
}

Launcher::Launcher() = default;

Category* Launcher::currentCategory(){
    return categories_.empty()?nullptr:&categories_[categoryIndex_];
}
const Category* Launcher::currentCategory() const{
    return categories_.empty()?nullptr:&categories_[categoryIndex_];
}

size_t Launcher::adjacentVisibleCategory(int direction) const{
    if(categories_.empty()) return 0;
    const int count=static_cast<int>(categories_.size());
    int candidate=static_cast<int>(categoryIndex_);
    for(int step=0;step<count;++step){
        candidate=wrapIndex(candidate+direction,count);
        if(!systemHiddenFor(categories_[static_cast<size_t>(candidate)].systemId))
            return static_cast<size_t>(candidate);
    }
    return categoryIndex_;
}

size_t Launcher::adjacentReorderCategory(int direction) const{
    if(categories_.empty() || direction==0) return categories_.size();
    int candidate=static_cast<int>(categoryIndex_)+direction;
    while(candidate>=0 && candidate<static_cast<int>(categories_.size())){
        if(!systemHiddenFor(categories_[static_cast<size_t>(candidate)].systemId))
            return static_cast<size_t>(candidate);
        candidate+=direction;
    }
    return categories_.size();
}

bool Launcher::init(){
    using StartupClock=std::chrono::steady_clock;
    const auto started=StartupClock::now();
#if defined(LAUNCHER_DEVICE)
    // The Miyoo SDL backend defaults to unsynchronized presentation. Request
    // page-flip vsync before SDL initializes the framebuffer; it falls back
    // to adaptive vsync on panels that cannot page flip.
    SDL_SetHint("SDL_MMIYOO_VSYNC_MODE","strict");
#endif
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS)!=0){std::fprintf(stderr,"SDL init: %s\n",SDL_GetError());return false;}
#if !defined(LAUNCHER_DEVICE)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software");
#endif
#if defined(LAUNCHER_DEVICE)
    const Uint32 windowFlags=SDL_WINDOW_FULLSCREEN;
#else
    const Uint32 windowFlags=SDL_WINDOW_RESIZABLE;
#endif
    window_=SDL_CreateWindow("Cartridge Launcher",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,kDesignW,kDesignH,windowFlags);
    if(!window_){std::fprintf(stderr,"window: %s\n",SDL_GetError());return false;}
#if defined(LAUNCHER_DEVICE)
    renderer_=SDL_CreateRenderer(window_,-1,SDL_RENDERER_PRESENTVSYNC);
    if(renderer_){
        rendererVsync_=true;
    }else{
        std::fprintf(stderr,"vsync renderer unavailable (%s); using unsynchronized fallback\n",SDL_GetError());
        renderer_=SDL_CreateRenderer(window_,-1,0);
    }
#else
    renderer_=SDL_CreateRenderer(window_,-1,SDL_RENDERER_SOFTWARE);
#endif
    if(!renderer_){std::fprintf(stderr,"renderer: %s\n",SDL_GetError());return false;}
    SDL_RendererInfo rendererInfo{};
    if(SDL_GetRendererInfo(renderer_,&rendererInfo)==0) {
        std::fprintf(stdout,"Renderer: %s (flags=0x%x)\n",rendererInfo.name?rendererInfo.name:"unknown",rendererInfo.flags);
        std::fflush(stdout);
    }
    std::fprintf(stdout,"Input devices: %d joystick(s)\n",SDL_NumJoysticks());
    for(int i=0;i<SDL_NumJoysticks();++i){
        SDL_Joystick* probe=SDL_JoystickOpen(i);
        if(probe){
            std::fprintf(stdout,"Joystick %d: %s, %d button(s), %d hat(s), game-controller=%s\n",
                         i,SDL_JoystickName(probe)?SDL_JoystickName(probe):"unknown",
                         SDL_JoystickNumButtons(probe),SDL_JoystickNumHats(probe),
                         SDL_IsGameController(i)?"yes":"no");
            SDL_JoystickClose(probe);
        }
    }
    SDL_RenderSetLogicalSize(renderer_,kDesignW,kDesignH);
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
    const auto displayReady=StartupClock::now();
    const char* configuredFont=std::getenv("CARTOS_FONT");
    std::string fontPath=(configuredFont && *configuredFont)?configuredFont:
                         (access("assets/font_atlas.png",R_OK)==0?"assets/font_atlas.png":"res/font_atlas.png");
    if(!draw::initializeFont(renderer_,fontPath)){
        std::fprintf(stderr,"font atlas could not be loaded: %s\n",fontPath.c_str());
        return false;
    }
    const auto fontReady=StartupClock::now();
    discoverGames();
    const auto catalogReady=StartupClock::now();
    reloadBackgroundChoices();
    const auto backgroundReady=StartupClock::now();
    const char* skipStartupAnimation=std::getenv("CARTOS_SKIP_STARTUP_ANIMATION");
    const bool returningFromOnion=skipStartupAnimation && std::string(skipStartupAnimation)=="1";
    restoreBrowserState(returningFromOnion);
    const Category* initialCategory=currentCategory();
    startupAnimation_=startupAnimationEnabled_ && !accessibility_.reducedMotion && !returningFromOnion &&
        initialCategory && !initialCategory->entries.empty();
    startupAnimationElapsed_=0.0f;
    for(int i=0;i<SDL_NumJoysticks();++i){
        if(SDL_IsGameController(i)) controller_=SDL_GameControllerOpen(i);
        if(controller_) break;
        if(!joystick_) joystick_=SDL_JoystickOpen(i);
    }
    vibrationAvailable_=controller_?SDL_GameControllerHasRumble(controller_):
        joystick_?SDL_JoystickHasRumble(joystick_):false;
    if(!vibrationAvailable_) accessibility_.vibration=false;
    if(controller_){
        char* mapping=SDL_GameControllerMapping(controller_);
        if(mapping){
            std::fprintf(stdout,"Controller mapping: %s\n",mapping);
            std::fflush(stdout);
            SDL_free(mapping);
        }
    }
#if defined(LAUNCHER_DEVICE)
    // Beta-only startup timings distinguish CartOS work from OnionOS boot.
    const auto ready=StartupClock::now();
    auto milliseconds=[](StartupClock::time_point a,StartupClock::time_point b){
        return std::chrono::duration_cast<std::chrono::milliseconds>(b-a).count();
    };
    std::ofstream timing(sdRoot_+"/App/CartOS/startup-timing.log",std::ios::app);
    if(timing){
        timing<<std::time(nullptr)<<" catalog="<<(lastCatalogLoaded_?"cached":"scanned")
              <<" display_ms="<<milliseconds(started,displayReady)
              <<" font_ms="<<milliseconds(displayReady,fontReady)
              <<" games_ms="<<milliseconds(fontReady,catalogReady)
              <<" background_ms="<<milliseconds(catalogReady,backgroundReady)
              <<" finish_ms="<<milliseconds(backgroundReady,ready)
              <<" total_ms="<<milliseconds(started,ready)<<'\n';
    }
#endif
    return true;
}

void Launcher::discoverGames(bool forceRomScan){
    const std::string sdRoot=sdRoot_.empty()?"/mnt/SDCARD":sdRoot_;
    const std::string root=sdRoot+"/Roms";
    sdRoot_=sdRoot;
    systems_.clear();
    categories_.clear();
    artworkIndexes_.clear();
    discoverSystems(sdRoot);
    loadSettings();
    int variant=0;
    discoverRecent(sdRoot);
    std::vector<catalog_cache::System> catalogSystems;
    catalogSystems.reserve(systems_.size());
    for(const SystemInfo& system:systems_)
        catalogSystems.push_back({system.id,system.label,system.launcherPath,
                                  system.artworkPath,system.romPaths,system.extensions});
    const std::string catalogPath=sdRoot+ kGameCatalogPath;
    const std::string catalogSignature=catalog_cache::fingerprint(catalogSystems);
    catalog_cache::Listings catalogListings;
    const bool catalogLoaded=!forceRomScan &&
        catalog_cache::load(catalogPath,catalogSignature,catalogSystems,catalogListings);
    lastCatalogLoaded_=catalogLoaded;
    if(!catalogLoaded) catalogListings.reserve(systems_.size());
    std::unordered_set<std::string> systemsWithGames;
    for(size_t systemIndex=0;systemIndex<systems_.size();++systemIndex){
        const SystemInfo& system=systems_[systemIndex];
        Category category;
        category.label=system.label;
        category.systemId=system.id;
        category.orderKey="system:"+system.id;
        if(catalogLoaded){
            for(const catalog_cache::Entry& entry:catalogListings[systemIndex]){
                if(isUtilityEntry(entry.title,entry.romPath)) continue;
                Game game;
                game.title=entry.title;
                game.system=system.label;
                game.console=system.id;
                game.romPath=entry.romPath;
                game.defaultArtPath=entry.artworkPath;
                game.artPath=entry.artworkPath;
                game.launcherPath=system.launcherPath;
                game.variant=variant++;
                category.entries.push_back(std::move(game));
            }
        }else{
        std::unordered_set<std::string> artworkFiles;
        const std::string artworkDir=system.artworkPath;
        DIR* artworkHandle=opendir(artworkDir.c_str());
        if(artworkHandle){
            while(dirent* artworkEntry=readdir(artworkHandle)){
                const std::string artworkName=artworkEntry->d_name;
                if(artworkName.size()>4 && lower(artworkName.substr(artworkName.size()-4))==".png")
                    artworkFiles.insert(artworkName);
            }
            closedir(artworkHandle);
        }
        std::function<void(const std::string&,int)> scanDirectory;
        scanDirectory=[&](const std::string& dirPath,int depth){
            if(depth>8) return;
            DIR* dir=opendir(dirPath.c_str());
            if(!dir) return;
            while(dirent* entry=readdir(dir)){
                const std::string filename=entry->d_name;
                if(filename.empty() || filename[0]=='.' || filename=="Imgs") continue;
                const std::string path=dirPath+"/"+filename;
                struct stat pathInfo{};
                const bool isDirectory=entry->d_type==DT_DIR ||
                    (entry->d_type==DT_UNKNOWN && stat(path.c_str(),&pathInfo)==0 && S_ISDIR(pathInfo.st_mode));
                if(isDirectory){
                    scanDirectory(path,depth+1);
                    continue;
                }
                if(entry->d_type!=DT_REG && !regularFile(path)) continue;
                if(!supportedRom(filename,system)) continue;
                const std::string title=filename.substr(0,filename.find_last_of('.'));
                if(isUtilityEntry(title,filename)) continue;
                Game game;
                game.title=title;
                game.system=system.label;
                game.console=system.id;
                game.romPath=path;
                const std::string imageName=title+".png";
                if(artworkFiles.count(imageName)) game.defaultArtPath=artworkDir+"/"+imageName;
                else {
                    const std::string siblingImage=dirPath+"/Imgs/"+imageName;
                    if(regularFile(siblingImage)) game.defaultArtPath=siblingImage;
                }
                // Populate every category through the same artwork loader
                // after Recent and saved lists have also been discovered.
                game.artPath=game.defaultArtPath;
                game.launcherPath=system.launcherPath;
                game.variant=variant++;
                category.entries.push_back(std::move(game));
            }
            closedir(dir);
        };
        for(const std::string& romRoot:system.romPaths)
            scanDirectory(romRoot,0);
        }
        std::sort(category.entries.begin(),category.entries.end(),[](const Game& a,const Game& b){return lower(a.title)<lower(b.title);});
        if(!catalogLoaded){
            std::vector<catalog_cache::Entry> cached;
            cached.reserve(category.entries.size());
            for(const Game& game:category.entries)
                cached.push_back({game.title,game.romPath,game.defaultArtPath});
            catalogListings.push_back(std::move(cached));
        }
        // OnionOS can contain dozens of system configs. Keep the browser
        // focused on systems with at least one ROM actually present.
        if(!category.entries.empty()){
            systemsWithGames.insert(system.id);
            categories_.push_back(std::move(category));
        }
    }
    if(!catalogLoaded && !catalog_cache::save(catalogPath,catalogSignature,catalogSystems,catalogListings))
        std::fprintf(stderr,"Game catalog cache could not be saved; scanning will run next launch\n");
    systems_.erase(std::remove_if(systems_.begin(),systems_.end(),[&](const SystemInfo& system){
        return systemsWithGames.find(system.id)==systemsWithGames.end();
    }),systems_.end());
    discoverCollections(sdRoot);
    discoverApps(sdRoot);
    mergeRecentHistory(categories_.front(),categories_.back());
    applyListOrder();
    rememberedSelections_.assign(categories_.size(),0);
    std::size_t total=0;
    for(const Category& category:categories_) total+=category.entries.size();
    std::fprintf(stdout,"Discovered %zu entries across %zu lists under %s (%s catalog)\n",
                 total,categories_.size(),root.c_str(),catalogLoaded?"cached":"scanned");
    std::fflush(stdout);
}

void Launcher::discoverSystems(const std::string& sdRoot){
    const std::string emuRoot=sdRoot+"/Emu";
    DIR* directory=opendir(emuRoot.c_str());
    if(!directory) return;
    while(dirent* entry=readdir(directory)){
        const std::string id=entry->d_name;
        if(id.empty() || id[0]=='.' || id=="_COLLECTIONS") continue;
        const std::string systemDir=emuRoot+"/"+id;
        const std::string config=readText(systemDir+"/config.json");
        if(config.empty()) continue;
        const std::string label=jsonString(config,"label");
        if(isUtilityEntry(label,id)) continue;
        const std::string launch=jsonString(config,"launch");
        const std::string romPaths=jsonString(config,"rompath");
        const std::string extlist=jsonString(config,"extlist");
        if(label.empty() || launch.empty() || romPaths.empty() || extlist.empty()) continue;

        SystemInfo system;
        system.id=id;
        system.label=label;
        system.launcherPath=resolveConfigPath(launch,sdRoot,systemDir);
        if(!regularFile(system.launcherPath)) continue;
        for(const std::string& extension:splitPipeList(extlist)){
            const std::string normalized=extension[0]=='.'?lower(extension):"."+lower(extension);
            system.extensions.push_back(normalized);
        }
        if(system.extensions.empty()) continue;
        for(const std::string& romPath:splitPipeList(romPaths)){
            // OnionOS paths are commonly relative to Emu/<system> (for
            // example ../../Roms/SCUMMVM/Shortcuts), not to the Roms root.
            std::string resolved=resolveConfigPath(romPath,sdRoot,systemDir);
            if(access(resolved.c_str(),R_OK)==0) system.romPaths.push_back(resolved);
        }
        if(system.romPaths.empty()) continue;
        system.artworkPath=resolveConfigPath(jsonString(config,"imgpath"),sdRoot,systemDir);
        if(system.artworkPath.empty()) system.artworkPath=system.romPaths.front()+"/Imgs";
        systems_.push_back(std::move(system));
    }
    closedir(directory);
    std::sort(systems_.begin(),systems_.end(),[](const SystemInfo& a,const SystemInfo& b){return lower(a.label)<lower(b.label);});
}

const SystemInfo* Launcher::systemForLauncher(const std::string& launcher) const{
    const std::string normalized=comparableRomPath(launcher,sdRoot_);
    for(const SystemInfo& system:systems_){
        if(normalized==comparableRomPath(system.launcherPath,sdRoot_)) return &system;
    }
    return nullptr;
}

const SystemInfo* Launcher::systemForRomPath(const std::string& romPath) const{
    const std::string normalized=comparableRomPath(romPath,sdRoot_);
    for(const SystemInfo& system:systems_){
        for(const std::string& root:system.romPaths){
            const std::string prefix=comparableRomPath(root,sdRoot_);
            if(normalized.compare(0,prefix.size(),prefix)==0 &&
               (normalized.size()==prefix.size() || normalized[prefix.size()]=='/')) return &system;
        }
    }
    return nullptr;
}

int Launcher::cartridgeScaleFor(const std::string& systemId) const{
    const auto found=cartridgeScaleBySystem_.find(systemId);
    return found==cartridgeScaleBySystem_.end()?1:found->second;
}

bool Launcher::systemHiddenFor(const std::string& systemId) const{
    const auto found=hiddenSystemById_.find(systemId);
    return found!=hiddenSystemById_.end() && found->second;
}

bool Launcher::readRecentGame(const std::string& romPath,const std::string& title,
                              const SystemInfo* system,Game& game) const{
    if(!regularFile(romPath)) return false;
    if(!system) system=systemForRomPath(romPath);
    const std::string filename=romPath.substr(romPath.find_last_of('/')+1);
    if(!system || !supportedRom(filename,*system) || isUtilityEntry(title,filename)) return false;
    const std::string stem=filename.substr(0,filename.find_last_of('.'));
    game.title=title.empty()?stem:title;
    game.system=system->label;
    game.console=system->id;
    game.romPath=romPath;
    game.launcherPath=system->launcherPath;
    const std::string imagePath=system->artworkPath+"/"+stem+".png";
    if(regularFile(imagePath)) game.artPath=imagePath;
    return true;
}

void Launcher::discoverRecent(const std::string& sdRoot){
    Category recent;
    recent.label="recent";
    recent.orderKey="recent";
    std::unordered_set<std::string> seenPaths;

    auto addRecent=[&](const std::string& rawPath,const std::string& rawTitle,const SystemInfo* hintedSystem){
        if(rawPath.empty()) return;
        const std::string romPath=sdPath(rawPath,sdRoot);
        const std::string normalized=comparableRomPath(romPath,sdRoot);
        Game game;
        if(seenPaths.count(normalized) || !readRecentGame(romPath,rawTitle,hintedSystem,game)) return;
        seenPaths.insert(normalized);
        game.variant=static_cast<int>(recent.entries.size());
        recent.entries.push_back(std::move(game));
    };

    // RetroArch maintains this history itself. We only read it; the emulator
    // remains responsible for its normal history and save updates.
    std::ifstream history(sdRoot+"/Saves/CurrentProfile/lists/content_history.lpl");
    std::string line;
    while(std::getline(history,line)){
        const std::string rawPath=jsonString(line,"path");
        if(rawPath.empty()) continue;
        const std::string rawTitle=jsonString(line,"label");
        addRecent(rawPath,rawTitle,nullptr);
    }

    // If RetroArch history is unavailable or empty, use Onion's recent list as
    // a read-only fallback, excluding apps and non-ROM shortcuts.
    if(recent.entries.empty()){
        std::ifstream onionRecent(sdRoot+"/Roms/recentlist.json");
        while(std::getline(onionRecent,line)){
            const std::string rawPath=jsonString(line,"rompath");
            const std::string launcher=jsonString(line,"launch");
            const SystemInfo* system=systemForRomPath(sdPath(rawPath,sdRoot));
            if(!system) system=systemForLauncher(launcher);
            if(system) addRecent(rawPath,jsonString(line,"label"),system);
        }
    }
    categories_.push_back(std::move(recent));
}

void Launcher::mergeRecentHistory(Category& recent,const Category& apps){
    nativeRecentGames_.clear();
    for(const Game& game:recent.entries) nativeRecentGames_.push_back(recentEntryKey(game,sdRoot_));

    std::ifstream input(sdRoot_+kRecentHistoryPath);
    std::string header;
    if(!std::getline(input,header) || header!="CartOSRecent1") return;
    std::vector<std::string> savedOrder;
    std::unordered_map<std::string,size_t> priorNativeRank;
    char section;
    std::string key;
    while(input>>section>>std::quoted(key)){
        if(section=='M') savedOrder.push_back(key);
        else if(section=='N') priorNativeRank.emplace(key,priorNativeRank.size());
        else return;
    }
    if(savedOrder.empty()) return;

    // The native list moves played games to the front. Its longest suffix
    // still in the previous order is unchanged, even if older items fell off.
    size_t firstUnchanged=nativeRecentGames_.size();
    size_t nextRank=priorNativeRank.size();
    while(firstUnchanged>0){
        const auto rank=priorNativeRank.find(nativeRecentGames_[firstUnchanged-1]);
        if(rank==priorNativeRank.end() || rank->second>=nextRank) break;
        nextRank=rank->second;
        --firstUnchanged;
    }

    std::unordered_map<std::string,const Game*> available;
    for(size_t i=0;i<recent.entries.size();++i)
        available.emplace(nativeRecentGames_[i],&recent.entries[i]);
    for(const Game& app:apps.entries)
        if(!app.workingDirectory.empty()) available.emplace(recentEntryKey(app,sdRoot_),&app);

    std::vector<Game> merged;
    std::unordered_set<std::string> seen;
    auto append=[&](const std::string& id){
        if(!seen.insert(id).second) return;
        const auto found=available.find(id);
        if(found!=available.end()) merged.push_back(*found->second);
        else if(id.compare(0,2,"G ")==0 && !priorNativeRank.count(id)){
            // Standalone games launched through CartOS can be absent from
            // RetroArch's history. Validate them again against the SD card.
            Game game;
            if(readRecentGame(sdPath(id.substr(2),sdRoot_),{},nullptr,game)){
                game.variant=static_cast<int>(merged.size());
                merged.push_back(std::move(game));
            }
        }
    };
    for(size_t i=0;i<firstUnchanged;++i) append(nativeRecentGames_[i]);
    for(const std::string& id:savedOrder) append(id);
    for(const std::string& id:nativeRecentGames_) append(id);
    recent.entries=std::move(merged);
}

bool Launcher::rememberRecentLaunch(const Game& entry) const{
    const std::string key=recentEntryKey(entry,sdRoot_);
    if(key.empty() || (entry.kind==EntryKind::App && entry.workingDirectory.empty())) return true;

    const std::string path=sdRoot_+kRecentHistoryPath;
    const std::string temporary=path+".tmp";
    std::ofstream output(temporary,std::ios::trunc);
    if(!output) return false;
    output<<"CartOSRecent1\nM "<<std::quoted(key)<<'\n';
    std::unordered_set<std::string> saved{key};
    for(const Category& category:categories_){
        if(category.orderKey!="recent") continue;
        for(const Game& item:category.entries){
            const std::string id=recentEntryKey(item,sdRoot_);
            if(!id.empty() && saved.insert(id).second) output<<"M "<<std::quoted(id)<<'\n';
        }
        break;
    }
    for(const std::string& id:nativeRecentGames_) output<<"N "<<std::quoted(id)<<'\n';
    output.close();
    if(output && rename(temporary.c_str(),path.c_str())==0) return true;
    unlink(temporary.c_str());
    return false;
}

void Launcher::discoverCollections(const std::string& sdRoot){
    const std::string collectionDir=sdRoot+"/Emu/_COLLECTIONS/COLLECTIONS";
    DIR* dir=opendir(collectionDir.c_str());
    if(!dir) return;

    auto loadList=[&](const std::string& filePath,const std::string& label,bool keepEmpty){
        if(isUtilityEntry(label,"")) return;
        Category category;
        category.label=label;
        category.orderKey=(label=="FAVOURITE")?"favourite":"collection:"+label;
        std::ifstream file(filePath.c_str());
        std::string line;
        int variant=0;
        while(std::getline(file,line)){
            const std::string romRaw=jsonString(line,"rompath");
            const std::string launcherRaw=jsonString(line,"launch");
            const std::string title=jsonString(line,"label");
            if(romRaw.empty() || title.empty() || launcherRaw.find("/_COLLECTIONS/")!=std::string::npos) continue;
            const std::string romPath=sdPath(romRaw,sdRoot);
            const SystemInfo* system=systemForRomPath(romPath);
            if(!system) system=systemForLauncher(launcherRaw);
            if(!system) continue;
            const std::string filename=romPath.substr(romPath.find_last_of('/')==std::string::npos?0:romPath.find_last_of('/')+1);
            if(!supportedRom(filename,*system) || !regularFile(romPath) || isUtilityEntry(title,filename)) continue;
            Game game;
            game.title=title;
            game.system=system->label;
            game.console=system->id;
            game.romPath=romPath;
            game.launcherPath=sdPath(launcherRaw,sdRoot);
            const std::string imageRaw=jsonString(line,"imgpath");
            if(!imageRaw.empty()) game.artPath=sdPath(imageRaw,sdRoot);
            if(game.artPath.empty() || access(game.artPath.c_str(),R_OK)!=0){
                const std::string titleNoExt=filename.substr(0,filename.find_last_of('.'));
                game.artPath=romPath.substr(0,romPath.find_last_of('/')+1)+"Imgs/"+titleNoExt+".png";
                if(access(game.artPath.c_str(),R_OK)!=0) game.artPath.clear();
            }
            game.variant=variant++;
            category.entries.push_back(std::move(game));
        }
        if(keepEmpty || !category.entries.empty()){
            std::sort(category.entries.begin(),category.entries.end(),[](const Game& a,const Game& b){return lower(a.title)<lower(b.title);});
            categories_.push_back(std::move(category));
        }
    };

    // Onion stores the active Favourite list as favourite.json, while these
    // manifests preserve each named custom list independently.
    loadList(sdRoot+"/Roms/favourite.json","FAVOURITE",false);
    std::vector<std::pair<std::string,std::string>> files;
    while(dirent* entry=readdir(dir)){
        const std::string name=entry->d_name;
        if(name.empty() || name[0]=='.' || name[0]=='-' || name.size()<6 || lower(name.substr(name.size()-5))!=".json") continue;
        files.push_back({name.substr(0,name.size()-5),collectionDir+"/"+name});
    }
    closedir(dir);
    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return lower(a.first)<lower(b.first);});
    for(const auto& file:files) loadList(file.second,file.first,true);
}

void Launcher::discoverApps(const std::string& sdRoot){
    const std::string appRoot=sdRoot+"/App";
    DIR* dir=opendir(appRoot.c_str());
    Category apps;
    apps.label="APPS";
    apps.orderKey="apps";
    int variant=0;
    if(dir){
    while(dirent* entry=readdir(dir)){
        const std::string folder=entry->d_name;
        if(folder.empty() || folder[0]=='.' || folder=="CartOS") continue;
        const std::string appDir=appRoot+"/"+folder;
        const std::string configPath=appDir+"/config.json";
        if(!regularFile(configPath)) continue;
        const std::string config=readText(configPath);
        const std::string label=jsonString(config,"label");
        std::string launch=jsonString(config,"launch");
        // Older Onion app packages may omit the launch key; their conventional
        // entry point is launch.sh. Newer packages are followed exactly as
        // declared in config.json.
        if(launch.empty()) launch="launch.sh";
        const std::string launchPath=resolveConfigPath(launch,sdRoot,appDir);
        if(label.empty() || isUtilityEntry(label,folder) || !regularFile(launchPath) || access(launchPath.c_str(),R_OK)!=0) continue;
        Game app;
        app.kind=EntryKind::App;
        app.title=label;
        app.system="ONION APP";
        app.launcherPath=launchPath;
        app.workingDirectory=appDir;
        const std::string icon=jsonString(config,"icon");
        if(!icon.empty()){
            app.artPath=resolveConfigPath(icon,sdRoot,appDir);
            if(!regularFile(app.artPath) || access(app.artPath.c_str(),R_OK)!=0) app.artPath.clear();
        }
        app.variant=variant++;
        apps.entries.push_back(std::move(app));
    }
    closedir(dir);
    }
    std::sort(apps.entries.begin(),apps.entries.end(),[](const Game& a,const Game& b){return lower(a.title)<lower(b.title);});
    Game mainUi;
    mainUi.kind=EntryKind::App;
    mainUi.title="Main UI";
    mainUi.system="ONION SYSTEM";
    mainUi.launcherPath="builtin:onion-main-ui";
    mainUi.artPath=sdRoot+"/App/CartOS/res/onionos_logo.png";
    if(access(mainUi.artPath.c_str(),R_OK)!=0)
        mainUi.artPath=access("assets/onionos_logo.png",R_OK)==0?"assets/onionos_logo.png":"";
    mainUi.onionMainUi=true;
    mainUi.variant=variant++;
    apps.entries.push_back(std::move(mainUi));
    Game settings;
    settings.kind=EntryKind::App;
    settings.title="CartOS settings";
    settings.system="CARTOS APP";
    settings.launcherPath="builtin:cartos-settings";
    settings.artPath=sdRoot+"/App/CartOS/res/icon/cartos.png";
    if(access(settings.artPath.c_str(),R_OK)!=0)
        settings.artPath=access("res/icon/cartos.png",R_OK)==0?"res/icon/cartos.png":"";
    settings.cartLauncherSettings=true;
    settings.variant=variant++;
    apps.entries.push_back(std::move(settings));
    Game accessible;
    accessible.kind=EntryKind::App;
    accessible.title="CartOS Accessibility";
    accessible.system="CARTOS APP";
    accessible.launcherPath="builtin:cartos-accessibility";
    accessible.artPath=sdRoot+"/App/CartOS/res/icon/accessibility.png";
    if(access(accessible.artPath.c_str(),R_OK)!=0)
        accessible.artPath=access("assets/accessibility.png",R_OK)==0?"assets/accessibility.png":"res/icon/accessibility.png";
    accessible.cartAccessibility=true;
    accessible.variant=variant++;
    apps.entries.push_back(std::move(accessible));
    std::sort(apps.entries.begin(),apps.entries.end(),[](const Game& a,const Game& b){return lower(a.title)<lower(b.title);});
    categories_.push_back(std::move(apps));
}

void Launcher::applyListOrder(){
    // Older builds stored the order in list-order.txt. Import it once when
    // settings.ini has no saved order, then persist it with the other settings.
    if(listOrder_.empty()){
        const std::string legacyPath=sdRoot_+"/App/CartOS/list-order.txt";
        std::ifstream legacyFile(legacyPath.c_str());
        std::string line;
        while(std::getline(legacyFile,line)){
            const size_t first=line.find_first_not_of(" \t\r\n");
            if(first==std::string::npos || line[first]=='#') continue;
            const size_t last=line.find_last_not_of(" \t\r\n");
            listOrder_.push_back(line.substr(first,last-first+1));
        }
    }

    std::vector<Category> ordered;
    ordered.reserve(categories_.size());
    std::vector<bool> used(categories_.size(),false);
    for(const std::string& key:listOrder_){
        for(size_t index=0;index<categories_.size();++index){
            if(!used[index] && categories_[index].orderKey==key){
                ordered.push_back(std::move(categories_[index]));
                used[index]=true;
                break;
            }
        }
    }
    for(size_t index=0;index<categories_.size();++index)
        if(!used[index]) ordered.push_back(std::move(categories_[index]));
    categories_.swap(ordered);
    saveListOrder();
}

void Launcher::saveListOrder(){
    listOrder_.clear();
    for(const Category& category:categories_)
        listOrder_.push_back(category.orderKey);
    saveSettings();
}

void Launcher::moveList(int direction){
    if(direction==0 || categoryIndex_>=categories_.size()) return;
    const size_t neighbor=adjacentReorderCategory(direction);
    if(neighbor>=categories_.size()) return;
    std::swap(categories_[categoryIndex_],categories_[neighbor]);
    std::swap(rememberedSelections_[categoryIndex_],rememberedSelections_[neighbor]);
    categoryIndex_=neighbor;
    const int selection=rememberedSelections_[categoryIndex_];
    position_=target_=static_cast<float>(selection);
    saveListOrder();
    statusMessage_=std::string("Moved list ")+(direction<0?"up":"down");
    statusUntil_=SDL_GetTicks64()+1800;
    overlay_=Overlay::None;
}

void Launcher::shutdown(){
    navigationAudio_.close();
    if(vibrationAvailable_){
        if(controller_)SDL_GameControllerRumble(controller_,0,0,0);
        else if(joystick_)SDL_JoystickRumble(joystick_,0,0,0);
    }
    if(backgroundArtwork_){SDL_DestroyTexture(backgroundArtwork_);backgroundArtwork_=nullptr;}
    for(Category& category:categories_) clearArtwork(category.entries);
    draw::shutdownFont();
    if(controller_){SDL_GameControllerClose(controller_);controller_=nullptr;}
    if(joystick_){SDL_JoystickClose(joystick_);joystick_=nullptr;}
    if(renderer_){SDL_DestroyRenderer(renderer_);renderer_=nullptr;}
    if(window_){SDL_DestroyWindow(window_);window_=nullptr;}
    SDL_Quit();
#if defined(LAUNCHER_DEVICE)
    // The Miyoo framebuffer is double-height. SDL page flips can leave scanout
    // on the lower page (yoffset=480); OnionOS apps expect the top page.
    resetFramebufferToTopPage();
#endif
}

void Launcher::renderBackground(){
    SDL_SetRenderDrawColor(renderer_,0,0,0,255);
    SDL_RenderClear(renderer_);
    if(!accessibility_.highContrast){
        draw::background(renderer_,backgroundArtwork_,kDesignW,kDesignH);
        if(accessibility_.dimming){
            static const Uint8 levels[]={0,40,88,136};
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_,0,0,0,levels[accessibility_.dimming]);
            SDL_RenderFillRect(renderer_,nullptr);
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
        }
    }
}

void Launcher::move(int direction){
    Category* category=currentCategory();
    if(category && !category->entries.empty())
        target_+=static_cast<float>(direction);
}

void Launcher::skipLetter(int direction){
    const Category* category=currentCategory();
    if(!category || category->systemId.empty() || category->entries.size()<2 || direction==0) return;
    const int count=static_cast<int>(category->entries.size());
    const int selected=wrapIndex(static_cast<int>(std::lround(target_)),count);
    const char currentGroup=titleInitialGroup(category->entries[static_cast<size_t>(selected)].title);
    for(int step=1;step<count;++step){
        int candidate=selected+direction*step;
        candidate=wrapIndex(candidate,count);
        if(titleInitialGroup(category->entries[static_cast<size_t>(candidate)].title)!=currentGroup){
            position_=target_=static_cast<float>(candidate);
            rememberedSelections_[categoryIndex_]=candidate;
            return;
        }
    }
}

void Launcher::moveCategory(int direction){
    if(categories_.empty()) return;
    if(Category* category=currentCategory()) clearArtwork(category->entries);
    categoryIndex_=adjacentVisibleCategory(direction);
    const int selection=rememberedSelections_[categoryIndex_];
    position_=target_=static_cast<float>(selection);
}

void Launcher::setScrollHeld(int direction,bool held){
    if(direction<0 || direction>=static_cast<int>(scrollHeld_.size())) return;
    const size_t index=static_cast<size_t>(direction);
    if(held){
        if(scrollHeld_[index]) return;
        scrollHeld_[index]=true;
        scrollSettingsMode_[index]=(overlay_==Overlay::Settings || overlay_==Overlay::Accessibility);
        scrollRepeatAt_[index]=SDL_GetTicks64()+accessibility_.repeatDelay();
        if(scrollSettingsMode_[index]){
            if(direction>=2){
                if(overlay_==Overlay::Accessibility)moveAccessibilitySelection(direction==2?-1:1);
                else moveSettingsSelection(direction==2?-1:1);
            }
        }else{
            if(direction==0) move(-1);
            else if(direction==1) move(1);
            else if(direction==2) moveCategory(-1);
            else moveCategory(1);
        }
    }else{
        scrollHeld_[index]=false;
    }
}

void Launcher::updateHeldScroll(Uint64 now){
    if(launchAnimation_) return;
    for(size_t index=0;index<scrollHeld_.size();++index){
        if(!scrollHeld_[index] || now<scrollRepeatAt_[index]) continue;
        if(scrollSettingsMode_[index]){
            if(index<2)continue;
            if(overlay_==Overlay::Accessibility)moveAccessibilitySelection(index==2?-1:1);
            else if(overlay_==Overlay::Settings)moveSettingsSelection(index==2?-1:1);
            else continue;
        }else{
            if(overlay_!=Overlay::None) continue;
            if(index==0) move(-1);
            else if(index==1) move(1);
            else if(index==2) moveCategory(-1);
            else moveCategory(1);
        }
        // Schedule from the current frame instead of catching up missed ticks,
        // which keeps scrolling bounded even if rendering briefly stalls.
        scrollRepeatAt_[index]=now+accessibility_.repeatInterval();
        noteInput("SCROLL");
    }
}

void Launcher::launchSelected(bool quickLaunch){
    Category* category=currentCategory();
    if(!category || category->entries.empty()) return;
    const int selected=wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(category->entries.size()));
    Game& entry=category->entries[static_cast<size_t>(selected)];
    if(quickLaunch && (menuButtonHeld_ || entry.kind!=EntryKind::Game || !regularFile(entry.romPath))) return;
    if(entry.cartAccessibility){openAccessibility();return;}
    if(entry.cartLauncherSettings){
        settingsSelection_=0;
        settingsScrollOffset_=0;
        overlay_=Overlay::Settings;
        return;
    }
    if(entry.kind==EntryKind::Game){
        beginLaunchAnimation(categoryIndex_,static_cast<size_t>(selected));
        // Use the double-A path: play the whole existing animation without
        // pausing at step one or discovering unused gallery images.
        if(quickLaunch)requestGameLaunch();
        return;
    }
    launchGame(entry);
}

void Launcher::beginLaunchAnimation(size_t categoryIndex,size_t entryIndex){
    if(categoryIndex>=categories_.size() || entryIndex>=categories_[categoryIndex].entries.size()) return;
    launchAnimationCategory_=categoryIndex;
    launchAnimationEntry_=entryIndex;
    previewShowingDetails_=false;
    resetPreviewArtwork();
    launchAnimationElapsed_=0.0f;
    launchCenterDuration_=overlay_==Overlay::Search?0.0f:kLaunchCenterDuration;
    launchCenteringOffset_=0.0f;
    launchCenteringPosition_=std::round(position_);
    launchReverseElapsed_=0.0f;
    if(launchCenterDuration_>0.0f){
        const float selected=launchCenteringPosition_;
        const float spacing=carouselSpacing_==0?215.0f:(carouselSpacing_==2?255.0f:240.0f);
        // Bring the already-selected cartridge to the center before growing it.
        // This matters when A is pressed while the carousel is still settling
        // between entries: position_ may be fractional, so the selected cart is
        // drawn slightly to one side in the normal browser view.
        launchCenteringOffset_=(position_-selected)*spacing;
    }
    Game& game=categories_[categoryIndex].entries[entryIndex];
    if(renderer_ && !game.artwork && !game.artworkAttempted && !game.artPath.empty()){
        game.artworkAttempted=true;
        game.artwork=artwork::loadPng(renderer_,game.artPath);
    }
    launchPrimarySize_={0,0};
    if(game.artwork)SDL_QueryTexture(game.artwork,nullptr,nullptr,&launchPrimarySize_.x,&launchPrimarySize_.y);
    const bool search=overlay_==Overlay::Search;
    const bool largeLayout=accessibility_.textSize==2 || accessibility_.largeArtwork || accessibility_.largeControls;
    const int consoleScale=cartridgeScaleFor(game.console);
    const float consolePreference=consoleScale==0?.90f:consoleScale==2?1.10f:1.0f;
    const float artworkPreference=accessibility_.largeArtwork?1.18f:1.0f;
    const int centerY=search?(searchResultsFocused_?238:226):254;
    const SDL_Rect viewport=search?(searchResultsFocused_?SDL_Rect{20,126,712,250}:SDL_Rect{20,126,712,180}):
        SDL_Rect{12,largeLayout?118:100,728,largeLayout?298:316};
    const float distance=search?0.0f:std::min(1.0f,std::fabs(position_-launchCenteringPosition_));
    const float factor=artworkPreference*(search?1.0f:consolePreference);
    const int width=static_cast<int>((search?220:280-95*distance)*factor);
    const int height=static_cast<int>((search?275:350-120*distance)*factor);
    const int sourceX=376-static_cast<int>(search?0.0f:launchCenteringOffset_);
    launchStartArtwork_=draw::artworkRect(game.artwork,{sourceX-width/2,centerY-height/2,width,height},viewport);
    const int returnWidth=static_cast<int>((search?220:280)*factor);
    const int returnHeight=static_cast<int>((search?275:350)*factor);
    launchReturnArtwork_=draw::artworkRect(game.artwork,{376-returnWidth/2,centerY-returnHeight/2,returnWidth,returnHeight},viewport);
    const float fullFactor=1.34f*consolePreference*artworkPreference;
    const int previewWidth=static_cast<int>(280*fullFactor),previewHeight=static_cast<int>(350*fullFactor);
    const int previewY=search?274:254;
    launchPreviewContainer_={376-previewWidth/2,previewY-previewHeight/2,previewWidth,previewHeight};
    launchPreviewArtwork_=draw::artworkRect(game.artwork,launchPreviewContainer_,{20,22,712,426});
    // Centring may move the cart, but must never resize it. An already-centred
    // selection starts growing immediately from the exact browser rectangle.
    if(search || std::abs(launchStartArtwork_.x+launchStartArtwork_.w/2-376)<=1)launchCenterDuration_=0.0f;
    launchAwaitingConfirm_=false;
    launchArtworkLoading_=false;
    launchReversing_=false;
    launchStepTwoConfirmed_=false;
    launchFirstPressAt_=SDL_GetTicks64();
    launchAnimation_=true;
    scrollHeld_.fill(false);
    if(accessibility_.reducedMotion){
        position_=target_=launchCenteringPosition_;
        launchCenteringOffset_=0.0f;
        launchAnimationElapsed_=launchCenterDuration_+0.50f;
        launchAwaitingConfirm_=true;
        launchArtworkLoading_=true;
    }
}

SDL_Rect Launcher::launchArtworkRect() const{
    auto smooth=[](float t){t=std::max(0.0f,std::min(1.0f,t));return t*t*(3.0f-2.0f*t);};
    auto mix=[](SDL_Rect a,SDL_Rect b,float t){
        return SDL_Rect{static_cast<int>(std::lround(a.x+(b.x-a.x)*t)),static_cast<int>(std::lround(a.y+(b.y-a.y)*t)),
            static_cast<int>(std::lround(a.w+(b.w-a.w)*t)),static_cast<int>(std::lround(a.h+(b.h-a.h)*t))};
    };
    if(launchReversing_)return mix(launchPreviewArtwork_,launchReturnArtwork_,smooth(launchReverseElapsed_/kLaunchReverseDuration));
    SDL_Rect centred=launchStartArtwork_;
    if(launchCenterDuration_>0.0f)centred.x=376-centred.w/2;
    if(launchAnimationElapsed_<launchCenterDuration_ && !accessibility_.reducedMotion)
        return mix(launchStartArtwork_,centred,smooth(launchAnimationElapsed_/launchCenterDuration_));
    const float time=accessibility_.reducedMotion?.50f:std::max(0.0f,launchAnimationElapsed_-launchCenterDuration_);
    SDL_Rect image=mix(centred,launchPreviewArtwork_,smooth(time/.38f));
    const float lift=smooth((time-.50f)/.14f),drop=smooth((time-.64f)/.36f);
    image.y-=static_cast<int>(std::lround(30*lift));
    image.y+=static_cast<int>(std::lround((kDesignH+30-image.y)*drop));
    return image;
}

void Launcher::drawLaunchingCartridge(Game& game){
    SDL_Rect image=launchArtworkRect();
    const bool secondary=launchAwaitingConfirm_ && game.artSelection>0;
    if(secondary)image=draw::artworkRect(game.artwork,launchPreviewContainer_,{20,22,712,426});
    int width=0,height=0;
    if(game.artwork)SDL_QueryTexture(game.artwork,nullptr,nullptr,&width,&height);
    if(game.artwork && (secondary || (width==launchPrimarySize_.x && height==launchPrimarySize_.y))){
        SDL_RenderCopy(renderer_,game.artwork,nullptr,&image);
        draw::selection(renderer_,image);
    }else draw::cartridge(renderer_,image,game.system,game.title,game.variant,true,game.artwork);
}

Game* Launcher::launchingGame(){
    if(launchAnimationCategory_>=categories_.size() ||
       launchAnimationEntry_>=categories_[launchAnimationCategory_].entries.size()) return nullptr;
    return &categories_[launchAnimationCategory_].entries[launchAnimationEntry_];
}

artwork::Index& Launcher::artworkIndexFor(const SystemInfo& system,bool rebuild){
    auto cached=artworkIndexes_.find(system.id);
    if(cached!=artworkIndexes_.end() && !rebuild) return cached->second;
    const Uint64 before=SDL_GetPerformanceCounter();
    // Hex encoding keeps every system ID inside our app-owned cache folder.
    std::string key;
    static const char hex[]="0123456789abcdef";
    for(unsigned char c:system.id){key.push_back(hex[c>>4]);key.push_back(hex[c&15]);}
    artwork::Index& index=artworkIndexes_[system.id];
    const bool ready=index.open(system.artworkPath+"/CartOS","cache/artwork/"+key+".idx",rebuild);
    const double ms=1000.0*static_cast<double>(SDL_GetPerformanceCounter()-before)/SDL_GetPerformanceFrequency();
    std::fprintf(stdout,"Artwork index [%s]: %s, %zu PNGs, %.2f ms\n",system.id.c_str(),
                 !ready?"scan failed":(index.loadedFromDisk()?"loaded":"rebuilt"),index.fileCount(),ms);
    std::fflush(stdout);
    return index;
}

void Launcher::loadArtworkVariants(Game& game){
    if(game.kind!=EntryKind::Game) return;
    if(game.artVariants.empty()){
        const std::string primaryImage=!game.defaultArtPath.empty()?game.defaultArtPath:game.artPath;
        if(!primaryImage.empty() && regularFile(primaryImage))
            game.artVariants.push_back(primaryImage);
        if(!game.artPath.empty() && regularFile(game.artPath) &&
           std::find(game.artVariants.begin(),game.artVariants.end(),game.artPath)==game.artVariants.end())
            game.artVariants.push_back(game.artPath);
        const SystemInfo* system=systemForRomPath(game.romPath);
        if(system){
            if(game.artVariants.empty()){
                const std::string conventional=system->artworkPath+"/"+filenameWithoutExtension(game.romPath)+".png";
                if(regularFile(conventional)) game.artVariants.push_back(conventional);
            }
            std::string relativeRomPath;
            // OnionOS and RetroArch histories may store the same ROM through
            // paths containing a different SD root or `..` components.
            // Normalize both sides before deriving the artwork subfolder.
            const std::string normalizedGamePath=comparableRomPath(game.romPath,sdRoot_);
            size_t rootIndex=0;
            for(size_t i=0;i<system->romPaths.size();++i){
                const std::string& romRoot=system->romPaths[i];
                const std::string normalizedRoot=comparableRomPath(romRoot,sdRoot_);
                if(normalizedGamePath.compare(0,normalizedRoot.size(),normalizedRoot)==0 &&
                   normalizedGamePath.size()>normalizedRoot.size() && normalizedGamePath[normalizedRoot.size()]=='/'){
                    relativeRomPath=normalizedGamePath.substr(normalizedRoot.size()+1);
                    rootIndex=i;
                    break;
                }
            }
            if(!relativeRomPath.empty()){
                if(system->romPaths.size()>1){
                    const std::string& romRoot=system->romPaths[rootIndex];
                    const size_t rootSlash=romRoot.find_last_of('/');
                    const std::string rootLabel=rootSlash==std::string::npos?romRoot:romRoot.substr(rootSlash+1);
                    relativeRomPath=rootLabel+"/"+relativeRomPath;
                }
                const auto additionalImages=artworkIndexFor(*system).images(relativeRomPath);
                for(const std::string& image:additionalImages){
                    if(game.artVariants.size()>=20) break;
                    game.artVariants.push_back(image);
                }
            }
        }
        if(!game.artVariants.empty() && game.defaultArtPath.empty()) game.defaultArtPath=game.artVariants.front();
    }
    if(game.artVariants.empty()) return;
    if(game.artSelection>=game.artVariants.size()) game.artSelection=0;
    game.artPath=game.artVariants[game.artSelection];
}

void Launcher::selectPreviewArtwork(size_t selection){
    Game* game=launchingGame();
    if(!game || game->artVariants.empty()) return;
    game->artSelection=std::min(selection,game->artVariants.size()-1);
    const std::string& image=game->artVariants[game->artSelection];
    if(game->artPath==image) return;
    game->artPath=image;
    if(game->artwork){SDL_DestroyTexture(game->artwork);game->artwork=nullptr;}
    game->artworkAttempted=false;
}

void Launcher::browsePreviewPage(int direction){
    Game* game=launchingGame();
    if(!game) return;
    const size_t imageCount=game->artVariants.size();
    if(previewShowingDetails_){
        previewShowingDetails_=false;
        if(imageCount>0)
            selectPreviewArtwork(direction>0?0:imageCount-1);
        return;
    }
    if(imageCount==0){
        previewShowingDetails_=true;
        return;
    }
    if(direction>0 && game->artSelection+1<imageCount){
        selectPreviewArtwork(game->artSelection+1);
    }else if(direction<0 && game->artSelection>0){
        selectPreviewArtwork(game->artSelection-1);
    }else{
        previewShowingDetails_=true;
    }
}

void Launcher::resetPreviewArtwork(){
    Game* game=launchingGame();
    previewShowingDetails_=false;
    if(!game || game->artVariants.empty()) return;
    selectPreviewArtwork(0);
}

void Launcher::drawPreviewGameDetails(const Game& game){
    const SDL_Rect panel{76,116,600,328};
    fillRect(renderer_,panel,{4,7,15,248});
    outlineRect(renderer_,panel,{70,82,100,255});
    const SDL_Color white{238,241,247,255};
    const SDL_Color muted{150,158,171,255};
    const std::string heading="GAME DETAILS";
    draw::text(renderer_,(kDesignW-draw::textWidth(heading,25))/2,132,heading,25,white);
    draw::text(renderer_,100,178,"TITLE",15,muted);
    draw::text(renderer_,100,198,shortLabel(game.title,48),20,white);
    draw::text(renderer_,100,236,"SYSTEM",15,muted);
    draw::text(renderer_,100,256,game.system,20,white);
    draw::text(renderer_,100,294,"PLAY TIME",15,muted);
    const bool activityTrackerInstalled=access((sdRoot_+"/App/PlayActivity/launch.sh").c_str(),R_OK)==0;
    const long long playedSeconds=activityTrackerInstalled?playtimeSecondsFor(game):-1;
    std::string playtime=activityTrackerInstalled?"No playtime":"Install Activity Tracker to see playtime";
    if(activityTrackerInstalled && playedSeconds>=3600)
        playtime=std::to_string(playedSeconds/3600)+"h "+std::to_string((playedSeconds%3600)/60)+"m";
    else if(activityTrackerInstalled && playedSeconds>=60)
        playtime=std::to_string(playedSeconds/60)+"m";
    else if(activityTrackerInstalled && playedSeconds>=0)
        playtime=std::to_string(playedSeconds)+"s";
    const int playtimeSize=activityTrackerInstalled?20:16;
    draw::text(renderer_,100,314,shortLabel(playtime,56),playtimeSize,activityTrackerInstalled?white:muted);
    draw::text(renderer_,100,352,"ROM FILE",15,muted);
    draw::text(renderer_,100,372,draw::fitText(game.romPath,16,568),16,white);
}

void Launcher::loadSettings(){
    accessibility_=accessibility::Preferences{};
    startupAnimationEnabled_=true;
    showGameCount_=showBatteryLevel_=showControlHints_=true;
    invertTextColour_=textBackground_=hideGameTitle_=false;
    restoreBrowserPosition_=false; // Existing settings without this key stay opted out.
    backgroundFile_="BG.png";
    carouselSpacing_=animationSpeed_=titleSize_=1;
    cartridgeScaleBySystem_.clear();
    hiddenSystemById_.clear();
    const std::string data=readText(sdRoot_+"/App/CartOS/settings.ini");
    std::istringstream lines(data);
    std::string line;
    std::vector<std::pair<int,std::string>> storedListOrder;
    int legacyScale=-1;
    while(std::getline(lines,line)){
        if(!line.empty() && line.back()=='\r')line.pop_back();
        const size_t separator=line.find('=');
        if(separator==std::string::npos) continue;
        const std::string key=line.substr(0,separator), value=line.substr(separator+1);
        if(accessibility_.read(key,value))continue;
        if(key=="startup_animation") startupAnimationEnabled_=(value!="0");
        else if(key=="show_game_count") showGameCount_=(value!="0");
        else if(key=="show_battery_level") showBatteryLevel_=(value!="0");
        else if(key=="show_control_hints") showControlHints_=(value!="0");
        else if(key=="invert_text_colour") invertTextColour_=(value=="1");
        else if(key=="text_background") textBackground_=(value=="1");
        else if(key=="hide_game_title") hideGameTitle_=(value=="1");
        else if(key=="restore_browser_position") restoreBrowserPosition_=(value=="1");
        else if(key=="background_file") backgroundFile_=value;
        else if(key.compare(0,11,"list_order_")==0){
            const std::string indexText=key.substr(11);
            if(!indexText.empty() && indexText.find_first_not_of("0123456789")==std::string::npos){
                const int index=std::atoi(indexText.c_str());
                if(index>=0 && index<4096) storedListOrder.emplace_back(index,value);
            }
        }
        else if(key=="cartridge_scale") legacyScale=std::max(0,std::min(2,std::atoi(value.c_str())));
        else if(key.compare(0,16,"cartridge_scale_")==0){
            const std::string id=key.substr(16);
            if(!id.empty()){
                cartridgeScaleBySystem_[id]=std::max(0,std::min(2,std::atoi(value.c_str())));
            }
        }
        else if(key.compare(0,14,"hidden_system_")==0){
            const std::string id=key.substr(14);
            if(!id.empty()) hiddenSystemById_[id]=(value=="1");
        }
        else if(key=="carousel_spacing") carouselSpacing_=std::max(0,std::min(2,std::atoi(value.c_str())));
        else if(key=="animation_speed") animationSpeed_=std::max(0,std::min(2,std::atoi(value.c_str())));
        else if(key=="title_size") titleSize_=std::max(0,std::min(2,std::atoi(value.c_str())));
    }
    accessibility_.validate();
    applyAccessibility();
    std::sort(storedListOrder.begin(),storedListOrder.end(),[](const std::pair<int,std::string>& a,const std::pair<int,std::string>& b){
        return a.first<b.first;
    });
    listOrder_.clear();
    for(const std::pair<int,std::string>& item:storedListOrder)
        listOrder_.push_back(item.second);
    for(const SystemInfo& system:systems_){
        if(cartridgeScaleBySystem_.find(system.id)==cartridgeScaleBySystem_.end())
            cartridgeScaleBySystem_[system.id]=(legacyScale>=0?legacyScale:1);
        if(hiddenSystemById_.find(system.id)==hiddenSystemById_.end())
            hiddenSystemById_[system.id]=false;
    }
}

void Launcher::saveSettings() const{
    const std::string path=sdRoot_+"/App/CartOS/settings.ini";
    std::ostringstream file;
    file<<"startup_animation="<<(startupAnimationEnabled_?1:0)<<"\n"
        <<"show_game_count="<<(showGameCount_?1:0)<<"\n"
        <<"show_battery_level="<<(showBatteryLevel_?1:0)<<"\n"
        <<"show_control_hints="<<(showControlHints_?1:0)<<"\n"
        <<"invert_text_colour="<<(invertTextColour_?1:0)<<"\n"
        <<"text_background="<<(textBackground_?1:0)<<"\n"
        <<"hide_game_title="<<(hideGameTitle_?1:0)<<"\n"
        <<"restore_browser_position="<<(restoreBrowserPosition_?1:0)<<"\n"
        <<"background_file="<<backgroundFile_<<"\n"
        <<"carousel_spacing="<<carouselSpacing_<<"\n"
        <<"animation_speed="<<animationSpeed_<<"\n"
        <<"title_size="<<titleSize_<<"\n";
    for(size_t index=0;index<listOrder_.size();++index)
        file<<"list_order_"<<index<<"="<<listOrder_[index]<<"\n";
    for(const auto& scale:cartridgeScaleBySystem_)
        file<<"cartridge_scale_"<<scale.first<<"="<<scale.second<<"\n";
    for(const auto& hidden:hiddenSystemById_)
        file<<"hidden_system_"<<hidden.first<<"="<<(hidden.second?1:0)<<"\n";
    std::vector<std::pair<std::string,std::string>> values=accessibility_.entries();
    std::istringstream lines(file.str());
    std::string line;
    while(std::getline(lines,line)){
        const size_t equal=line.find('=');
        if(equal!=std::string::npos)values.emplace_back(line.substr(0,equal),line.substr(equal+1));
    }
    accessibility::updateConfig(path,values,{"list_order_","cartridge_scale","hidden_system_","a11y_simple_navigation"});
}

void Launcher::reloadBackgroundChoices(){
    const std::string directory=sdRoot_+"/App/CartOS";
    backgroundChoices_.clear();
    backgroundChoices_.push_back({"","NONE"});

    std::vector<BackgroundOption> named;
    DIR* handle=opendir(directory.c_str());
    if(handle){
        while(const dirent* entry=readdir(handle)){
            const std::string filename=entry->d_name;
            if(filename=="." || filename==".." || filename.compare(0,2,"._")==0) continue;
            const std::string folded=lower(filename);
            if(folded.size()<4 || folded.substr(folded.size()-4)!=".png") continue;
            if(!regularFile(directory+"/"+filename)) continue;
            if(folded=="bg.png") continue;
            if(folded.size()<=7 || folded.compare(0,3,"bg_")!=0) continue;

            std::string label=filename.substr(3,filename.size()-7);
            if(label.size()>=2 && label.front()=='(' && label.back()==')')
                label=label.substr(1,label.size()-2);
            if(label.empty()) continue;
            named.push_back({filename,label});
        }
        closedir(handle);
    }
    std::sort(named.begin(),named.end(),[](const BackgroundOption& a,const BackgroundOption& b){
        return lower(a.label)<lower(b.label);
    });

    const std::string defaultPath=directory+"/BG.png";
    const bool hasDefault=regularFile(defaultPath);
    backgroundChoices_.push_back({"BG.png",hasDefault?"DEFAULT":"DEFAULT (MISSING)"});
    backgroundChoices_.insert(backgroundChoices_.end(),named.begin(),named.end());

    if(!backgroundFile_.empty() && backgroundFile_!="BG.png" &&
       std::none_of(backgroundChoices_.begin(),backgroundChoices_.end(),[&](const BackgroundOption& choice){
           return choice.filename==backgroundFile_;
       })){
        // If a named image was removed, fall back to BG.png when available,
        // otherwise clear the selection. This also removes stale choices after
        // the user refreshes the browser.
        backgroundFile_=hasDefault?"BG.png":"";
    }

    backgroundSelection_=0;
    for(size_t i=0;i<backgroundChoices_.size();++i){
        if(backgroundChoices_[i].filename==backgroundFile_){
            backgroundSelection_=static_cast<int>(i);
            break;
        }
    }

    SDL_Texture* replacement=nullptr;
    if(!backgroundFile_.empty()){
        const std::string path=directory+"/"+backgroundFile_;
        if(regularFile(path)){
            std::string failure;
            replacement=artwork::loadPng(renderer_,path,2048,&failure);
            std::ofstream log(directory+"/background-debug.log",std::ios::out|std::ios::app);
            if(log) log<<"Background choice load: "<<path<<" -> "<<(replacement?"loaded":failure)<<"\n";
        }
    }
    if(backgroundArtwork_) SDL_DestroyTexture(backgroundArtwork_);
    backgroundArtwork_=replacement;
}

bool Launcher::selectBackground(int direction){
    if(backgroundChoices_.empty()) reloadBackgroundChoices();
    if(backgroundChoices_.empty()) return false;
    const int next=wrapIndex(backgroundSelection_+direction,static_cast<int>(backgroundChoices_.size()));
    const BackgroundOption& choice=backgroundChoices_[static_cast<size_t>(next)];
    SDL_Texture* replacement=nullptr;
    if(!choice.filename.empty()){
        const std::string path=sdRoot_+"/App/CartOS/"+choice.filename;
        if(regularFile(path)){
            std::string failure;
            replacement=artwork::loadPng(renderer_,path,2048,&failure);
            if(!replacement){
                statusMessage_="Could not load background: "+choice.label;
                statusUntil_=SDL_GetTicks64()+2200;
                return false;
            }
        }
    }
    if(backgroundArtwork_) SDL_DestroyTexture(backgroundArtwork_);
    backgroundArtwork_=replacement;
    backgroundSelection_=next;
    backgroundFile_=choice.filename;
    statusMessage_=choice.filename.empty()?"Background disabled":"Background: "+choice.label;
    statusUntil_=SDL_GetTicks64()+1600;
    return true;
}

void Launcher::changeSetting(int direction){
    const int systemCount=static_cast<int>(systems_.size());
    bool systemVisibilityChanged=false;
    if(settingsSelection_>=kGeneralSettingsCount){
        const int systemSetting=settingsSelection_-kGeneralSettingsCount;
        const int systemIndex=systemSetting/2;
        if(systemIndex>=0 && systemIndex<systemCount){
            const std::string& id=systems_[static_cast<size_t>(systemIndex)].id;
            if(systemSetting%2==0)
                cartridgeScaleBySystem_[id]=wrapIndex(cartridgeScaleFor(id)+direction,3);
            else{
                hiddenSystemById_[id]=!systemHiddenFor(id);
                systemVisibilityChanged=true;
            }
        }
    }else{
        switch(settingsSelection_){
            case 0: animationSpeed_=wrapIndex(animationSpeed_+direction,3); break;
            case 1: titleSize_=wrapIndex(titleSize_+direction,3); break;
            case 2: showGameCount_=!showGameCount_; break;
            case 3: showBatteryLevel_=!showBatteryLevel_; break;
            case 4: showControlHints_=!showControlHints_; break;
            case 5: hideGameTitle_=!hideGameTitle_; break;
            case 6: carouselSpacing_=wrapIndex(carouselSpacing_+direction,3); break;
            case 7: startupAnimationEnabled_=!startupAnimationEnabled_; break;
            case 8: selectBackground(direction); break;
            case 9: invertTextColour_=!invertTextColour_; break;
            case 10: textBackground_=!textBackground_; break;
            case 11:
                restoreBrowserPosition_=!restoreBrowserPosition_;
                if(!restoreBrowserPosition_) unlink(kBrowserStatePath);
                break;
            case 12:
                restoreBrowserPosition_=false;
                unlink(kBrowserStatePath);
                startupAnimationEnabled_=true;
                for(auto& scale:cartridgeScaleBySystem_) scale.second=1;
                for(const SystemInfo& system:systems_) cartridgeScaleBySystem_[system.id]=1;
                for(auto& hidden:hiddenSystemById_) hidden.second=false;
                for(const SystemInfo& system:systems_) hiddenSystemById_[system.id]=false;
                animationSpeed_=1; carouselSpacing_=1; titleSize_=1;
                showGameCount_=true; showBatteryLevel_=true; showControlHints_=true;
                invertTextColour_=false; textBackground_=false; hideGameTitle_=false;
                backgroundFile_="BG.png";
                reloadBackgroundChoices();
                statusMessage_="Settings reset";
                statusUntil_=SDL_GetTicks64()+1800;
                break;
        }
    }
    saveSettings();
    if(systemVisibilityChanged){
        const bool hidden=systemHiddenFor(systems_[static_cast<size_t>((settingsSelection_-kGeneralSettingsCount)/2)].id);
        statusMessage_=hidden?"System hidden":"System shown";
        statusUntil_=SDL_GetTicks64()+1800;
    }
}

void Launcher::moveSettingsSelection(int direction){
    settingsSelection_=wrapIndex(settingsSelection_+direction,settingsRowCount());
    const int visibleRows=accessibility_.largeControls?6:8;
    if(settingsSelection_<settingsScrollOffset_)
        settingsScrollOffset_=settingsSelection_;
    else if(settingsSelection_>=settingsScrollOffset_+visibleRows)
        settingsScrollOffset_=settingsSelection_-visibleRows+1;
}

int Launcher::settingsRowCount() const{
    return static_cast<int>(systems_.size())*2+kGeneralSettingsCount;
}

bool Launcher::saveBrowserState(size_t categoryIndex,const std::string& romPath) const{
    if(!restoreBrowserPosition_) return true; // Disabled: skip the snapshot without blocking launch.
    if(categoryIndex>=categories_.size() || categories_[categoryIndex].orderKey.empty() || romPath.empty())
        return false;
    // Stable identities survive list reordering and alphabetical ROM rescans.
    std::ostringstream contents;
    contents<<"CartOSBrowser1\n"<<std::quoted(categories_[categoryIndex].orderKey)<<'\n'
            <<std::quoted(onionPath(romPath,sdRoot_))<<'\n';
    const std::string text=contents.str();
    if(text.size()>16384) return false;
    const std::string temporary=std::string(kBrowserStatePath)+".tmp";
    FILE* file=std::fopen(temporary.c_str(),"wb");
    if(!file) return false;
    bool saved=std::fwrite(text.data(),1,text.size(),file)==text.size();
    if(std::fflush(file)!=0 || fsync(fileno(file))!=0) saved=false;
    if(std::fclose(file)!=0) saved=false;
    if(saved && rename(temporary.c_str(),kBrowserStatePath)==0) return true;
    unlink(temporary.c_str());
    return false;
}

void Launcher::restoreBrowserState(bool returningFromOnion){
    // Cold starts and unavailable saved positions always open Recent.
    categoryIndex_=0;
    for(size_t c=0;c<categories_.size();++c)
        if(categories_[c].orderKey=="recent"){categoryIndex_=c;break;}
    position_=target_=0.0f;
    if(!restoreBrowserPosition_ || !returningFromOnion){unlink(kBrowserStatePath);return;}
    struct stat info{};
    if(stat(kBrowserStatePath,&info)!=0) return;
    if(!S_ISREG(info.st_mode) || info.st_size<=0 || info.st_size>16384){
        unlink(kBrowserStatePath);
        return;
    }
    std::ifstream input(kBrowserStatePath,std::ios::binary);
    std::string header,listKey,romPath;
    std::getline(input,header);
    const bool parsed=header=="CartOSBrowser1" && static_cast<bool>(input>>std::quoted(listKey)>>std::quoted(romPath));
    input>>std::ws;
    const bool complete=input.eof();
    input.close();
    // Each handoff is consumed once; a later manual launch must not reuse it.
    unlink(kBrowserStatePath);
    if(!parsed || !complete || listKey.empty() || romPath.empty()) return;
    const std::string expected=comparableRomPath(romPath,sdRoot_);
    for(size_t c=0;c<categories_.size();++c){
        const Category& category=categories_[c];
        if(category.orderKey!=listKey || systemHiddenFor(category.systemId)) continue;
        for(size_t i=0;i<category.entries.size();++i){
            const Game& game=category.entries[i];
            if(game.kind!=EntryKind::Game || comparableRomPath(game.romPath,sdRoot_)!=expected) continue;
            categoryIndex_=c;
            position_=target_=static_cast<float>(i);
            if(c<rememberedSelections_.size()) rememberedSelections_[c]=static_cast<int>(i);
            std::fprintf(stdout,"Restored CartOS browser: %s, %s\n",listKey.c_str(),romPath.c_str());
            return;
        }
        return;
    }
}

void Launcher::openOnionGameSwitcher(){
#if defined(LAUNCHER_DEVICE)
    const std::string switcherFlag=sdRoot_+"/.tmp_update/.runGameSwitcher";
    const int switcher=open(switcherFlag.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(switcher<0){
        statusMessage_="Could not open OnionOS Game Switcher";
        statusUntil_=SDL_GetTicks64()+2500;
        return;
    }
    if(fsync(switcher)!=0){
        close(switcher);
        unlink(switcherFlag.c_str());
        statusMessage_="Could not save Game Switcher request";
        statusUntil_=SDL_GetTicks64()+2500;
        return;
    }
    close(switcher);
    const int returnFlag=open("/tmp/cartos_return_to_launcher",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(returnFlag<0){
        unlink(switcherFlag.c_str());
        statusMessage_="Could not queue return to CartOS";
        statusUntil_=SDL_GetTicks64()+2500;
        return;
    }
    fsync(returnFlag);
    close(returnFlag);
    if(const Category* category=currentCategory()){
        if(!category->entries.empty()){
            const size_t selected=static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),
                static_cast<int>(category->entries.size())));
            if(category->entries[selected].kind==EntryKind::Game)
                saveBrowserState(categoryIndex_,category->entries[selected].romPath);
        }
    }
    // OnionOS reads the handoff markers as soon as CartOS exits. Flush both
    // directory entries before leaving, especially on the first cold launch.
    sync();
    std::fprintf(stdout,"Queued OnionOS Game Switcher from Menu button\n");
    std::fflush(stdout);
    running_=false;
#else
    statusMessage_="Menu opens OnionOS Game Switcher on device";
    statusUntil_=SDL_GetTicks64()+2000;
#endif
}

void Launcher::beginMenuPress(){
    menuButtonHeld_=true;
    menuButtonChord_=false;
}

void Launcher::endMenuPress(){
    if(!menuButtonHeld_) return;
    const bool openSwitcher=!menuButtonChord_;
    menuButtonHeld_=false;
    menuButtonChord_=false;
    if(openSwitcher) openOnionGameSwitcher();
}

void Launcher::launchGame(const Game& entry){
    std::fprintf(stdout,"Selected: %s [%s]\n",entry.title.c_str(),entry.system.c_str());
    std::fflush(stdout);
    if(entry.launcherPath=="builtin:onion-main-ui"){
#if defined(LAUNCHER_DEVICE)
        shutdown();
        _exit(0);
#else
        running_=false;
        return;
#endif
    }
#if defined(LAUNCHER_DEVICE)
    if(entry.launcherPath.empty()) return;
    const EntryKind kind=entry.kind;
    const std::string launcher=entry.launcherPath;
    const std::string argument=entry.romPath;
    const std::string workingDirectory=entry.workingDirectory;
    const size_t appFolderSlash=workingDirectory.find_last_of('/');
    const std::string appFolder=appFolderSlash==std::string::npos
        ?workingDirectory:workingDirectory.substr(appFolderSlash+1);
    const bool musicPlayer=kind==EntryKind::App &&
        (lower(entry.title)=="music player" || lower(appFolder)=="gmu");
    if(kind==EntryKind::App && regularFile(launcher) && !rememberRecentLaunch(entry))
        std::fprintf(stderr,"Could not save CartOS recent app\n");
    if(kind==EntryKind::Game && !saveBrowserState(launchAnimationCategory_,entry.romPath))
        std::fprintf(stderr,"Could not save CartOS browser position\n");
    shutdown();
    if(kind==EntryKind::Game){
        // Replace this app's pending Onion command with a native game command.
        // Onion's runtime then owns the game session and handles GameSwitcher,
        // recents, CPU/audio setup, and normal emulator teardown.
        const std::string commandPath="/mnt/SDCARD/.tmp_update/cmd_to_run.sh";
        const std::string temporaryPath=commandPath+".cartos.tmp";
        const std::string persistentReturnMarker=sdRoot_+"/.tmp_update/cartos_return_to_launcher";
        const int returnMarker=open("/tmp/cartos_return_to_launcher",O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(returnMarker>=0) close(returnMarker);
        else std::perror("mark CartOS game session");
        if(!writeOnionRecentGame(entry))
            std::fprintf(stderr,"Could not update OnionOS recent games list\n");
        const size_t slash=launcher.find_last_of('/');
        const std::string emuDirectory=slash==std::string::npos?"/mnt/SDCARD/Emu":launcher.substr(0,slash);
        const std::string command="cd "+shellDoubleQuote(emuDirectory)+" && EMU_DIR="+
                                  shellDoubleQuote(emuDirectory)+
                                  " LD_PRELOAD=/mnt/SDCARD/miyoo/lib/libpadsp.so "+
                                  launchInvocation(launcher)+" "+shellDoubleQuote(argument)+"\n";
        {
            std::ofstream output(temporaryPath.c_str(),std::ios::out|std::ios::trunc);
            if(!output){
                std::perror("write OnionOS game command");
                _exit(127);
            }
            output<<command;
            output.close();
            if(!output){
                std::fprintf(stderr,"Could not finish writing OnionOS game command\n");
                unlink("/tmp/cartos_return_to_launcher");
                _exit(127);
            }
        }
        // Keep a durable session marker on the SD card as well as the /tmp
        // marker. OnionOS may resume this game after a full power-off, which
        // clears /tmp before GameSwitcher returns to its main UI.
        const int persistentMarker=open(persistentReturnMarker.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(persistentMarker>=0){
            const bool markerFlushed=fsync(persistentMarker)==0;
            const bool markerClosed=close(persistentMarker)==0;
            if(markerFlushed && markerClosed)
                sync();
            else {
                std::perror("flush CartOS game return marker");
                unlink(persistentReturnMarker.c_str());
            }
        }else
            std::perror("save CartOS game return marker");
        if(chmod(temporaryPath.c_str(),0755)!=0 || rename(temporaryPath.c_str(),commandPath.c_str())!=0){
            std::perror("install OnionOS game command");
            unlink("/tmp/cartos_return_to_launcher");
            unlink(persistentReturnMarker.c_str());
            _exit(127);
        }
        const int quickSwitch=open("/tmp/quick_switch",O_WRONLY|O_CREAT,0644);
        if(quickSwitch<0){
            std::perror("enable OnionOS game handoff");
            _exit(127);
        }
        close(quickSwitch);
        if(!rememberRecentLaunch(entry)) std::fprintf(stderr,"Could not save CartOS recent game\n");
        std::fprintf(stdout,"Queued OnionOS game command: %s\n",launcher.c_str());
        std::fflush(stdout);
        _exit(0);
    }
    if(!workingDirectory.empty() && chdir(workingDirectory.c_str())!=0)
        std::perror("change to app directory");
    if(kind==EntryKind::App){
        // Let OnionOS run apps through its normal command loop, just like ROMs.
        // This ensures its runtime handles app teardown and the return hook can
        // reopen CartOS instead of dropping back to MainUI.
        const std::string commandPath="/mnt/SDCARD/.tmp_update/cmd_to_run.sh";
        const std::string temporaryPath=commandPath+".cartos.tmp";
        const std::string markerPath="/tmp/cartos_return_to_launcher";
        const int marker=open(markerPath.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(marker>=0) close(marker);
        else {
            std::perror("mark CartOS app session; launching directly");
            setenv("LD_PRELOAD","/mnt/SDCARD/miyoo/app/../lib/libpadsp.so",1);
            chmod(launcher.c_str(),0755);
            if(launchInvocation(launcher).compare(0,8,"/bin/sh ")==0)
                execl("/bin/sh","sh",launcher.c_str(),static_cast<char*>(nullptr));
            else execl(launcher.c_str(),launcher.c_str(),static_cast<char*>(nullptr));
            std::perror("launch OnionOS entry");
            _exit(127);
        }
        // Mirror the command OnionOS MainUI queues for an app: enter its app
        // directory, make the configured target executable, then launch it
        // with OnionOS's audio/input preload. The runtime also reads the app
        // directory from this command when deciding whether to switch display
        // resolution, so keep the same command shape as MainUI.
        const std::string command=onionAppLaunchCommand(launcher,workingDirectory,sdRoot_,musicPlayer)+"\n";
        {
            std::ofstream output(temporaryPath.c_str(),std::ios::out|std::ios::trunc);
            if(output){
                output<<command;
                output.close();
            }
            if(!output){
                std::fprintf(stderr,"Could not write OnionOS app command; launching directly\n");
                unlink(temporaryPath.c_str());
                unlink(markerPath.c_str());
            }else if(chmod(temporaryPath.c_str(),0755)!=0 || rename(temporaryPath.c_str(),commandPath.c_str())!=0){
                std::perror("install OnionOS app command; launching directly");
                unlink(temporaryPath.c_str());
                unlink(markerPath.c_str());
            }else{
                const int quickSwitch=open("/tmp/quick_switch",O_WRONLY|O_CREAT,0644);
                if(quickSwitch>=0){
                    close(quickSwitch);
                    std::fprintf(stdout,"Queued OnionOS app command: %s\n",launcher.c_str());
                    std::fflush(stdout);
                    _exit(0);
                }
                std::perror("enable OnionOS app handoff; launching directly");
                unlink(markerPath.c_str());
            }
        }
    }
    if(kind==EntryKind::App){
        setenv("LD_PRELOAD","/mnt/SDCARD/miyoo/app/../lib/libpadsp.so",1);
        chmod(launcher.c_str(),0755);
    }
    if(launchInvocation(launcher).compare(0,8,"/bin/sh ")==0)
        execl("/bin/sh","sh",launcher.c_str(),static_cast<char*>(nullptr));
    else execl(launcher.c_str(),launcher.c_str(),static_cast<char*>(nullptr));
    std::perror("launch OnionOS entry");
    _exit(127);
#endif
}

std::string Launcher::collectionPath(const std::string& label) const{
    if(label=="FAVOURITE") return sdRoot_+"/Roms/favourite.json";
    if(label.empty() || label[0]=='-' || label.find('/')!=std::string::npos || label.find('\\')!=std::string::npos ||
       label=="." || label=="..") return {};
    return sdRoot_+"/Emu/_COLLECTIONS/COLLECTIONS/"+label+".json";
}

std::vector<std::string> Launcher::customListNames() const{
    std::vector<std::string> names;
    const std::string directory=sdRoot_+"/Emu/_COLLECTIONS/COLLECTIONS";
    DIR* dir=opendir(directory.c_str());
    if(!dir) return names;
    while(dirent* entry=readdir(dir)){
        const std::string name=entry->d_name;
        if(name.size()<6 || name[0]=='.' || name[0]=='-' || lower(name.substr(name.size()-5))!=".json") continue;
        names.push_back(name.substr(0,name.size()-5));
    }
    closedir(dir);
    std::sort(names.begin(),names.end(),[](const std::string& a,const std::string& b){return lower(a)<lower(b);});
    return names;
}

bool Launcher::writeListEntry(const std::string& path,const Game& game,bool removeExisting){
    const std::string serialized=serializedListEntry(game,sdRoot_);
    if(serialized.empty()) return false;
    const std::string rom=comparableRomPath(game.romPath,sdRoot_);
    const std::string temporary=path+".cartos.tmp";
    std::ofstream output(temporary.c_str(),std::ios::out|std::ios::trunc);
    if(!output) return false;
    std::ifstream input(path.c_str());
    if(access(path.c_str(),F_OK)==0 && !input){output.close();unlink(temporary.c_str());return false;}
    std::string line;
    bool found=false;
    while(std::getline(input,line)){
        const std::string existing=jsonString(line,"rompath");
        if(!existing.empty() && comparableRomPath(existing,sdRoot_)==rom){
            found=true;
            if(removeExisting) continue;
        }
        output<<line<<'\n';
    }
    if(!found || removeExisting) output<<serialized<<'\n';
    output.close();
    if(!output){unlink(temporary.c_str());return false;}
    if(rename(temporary.c_str(),path.c_str())!=0){unlink(temporary.c_str());return false;}
    return true;
}

bool Launcher::removeListEntry(const std::string& path,const Game& game){
    std::ifstream input(path.c_str());
    if(!input) return false;
    const std::string temporary=path+".cartos.tmp";
    std::ofstream output(temporary.c_str(),std::ios::out|std::ios::trunc);
    if(!output) return false;
    const std::string rom=comparableRomPath(game.romPath,sdRoot_);
    std::string line;
    bool removed=false;
    while(std::getline(input,line)){
        const std::string existing=jsonString(line,"rompath");
        if(!existing.empty() && comparableRomPath(existing,sdRoot_)==rom){removed=true;continue;}
        output<<line<<'\n';
    }
    output.close();
    if(!output){unlink(temporary.c_str());return false;}
    if(rename(temporary.c_str(),path.c_str())!=0){unlink(temporary.c_str());return false;}
    return removed;
}

bool Launcher::isSelectedFavorite() const{
    const Category* category=currentCategory();
    if(!category || category->entries.empty()) return false;
    const int selected=wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(category->entries.size()));
    const std::string path=sdRoot_+"/Roms/favourite.json";
    std::ifstream input(path.c_str());
    const std::string target=comparableRomPath(category->entries[static_cast<size_t>(selected)].romPath,sdRoot_);
    std::string line;
    while(std::getline(input,line)){
        const std::string existing=jsonString(line,"rompath");
        if(!existing.empty() && comparableRomPath(existing,sdRoot_)==target) return true;
    }
    return false;
}

void Launcher::openSelectMenu(){
    Category* category=currentCategory();
    if(!category) return;
    menuOptions_.clear();
    if(category->entries.empty()){
        menuOptions_.push_back("Create new list");
        if(adjacentReorderCategory(-1)<categories_.size()) menuOptions_.push_back("Move list up");
        if(adjacentReorderCategory(1)<categories_.size()) menuOptions_.push_back("Move list down");
        const std::vector<std::string> names=customListNames();
        if(std::find(names.begin(),names.end(),category->label)!=names.end()) menuOptions_.push_back("Delete list...");
        if(menuOptions_.empty()) return;
        overlaySelection_=0;
        overlay_=Overlay::SelectMenu;
        return;
    }
    const Game& selected=category->entries[static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(category->entries.size())))];
    if(selected.kind!=EntryKind::Game){
        if(adjacentReorderCategory(-1)<categories_.size()) menuOptions_.push_back("Move list up");
        if(adjacentReorderCategory(1)<categories_.size()) menuOptions_.push_back("Move list down");
        if(menuOptions_.empty()) return;
        overlaySelection_=0;
        overlay_=Overlay::SelectMenu;
        return;
    }
    menuOptions_.push_back(isSelectedFavorite()?"Remove from favourites":"Add to favourites");
    menuOptions_.push_back("Add to list");
    menuOptions_.push_back("Create new list");
    if(adjacentReorderCategory(-1)<categories_.size()) menuOptions_.push_back("Move list up");
    if(adjacentReorderCategory(1)<categories_.size()) menuOptions_.push_back("Move list down");
    const std::vector<std::string> names=customListNames();
    const bool currentIsCustomList=std::find(names.begin(),names.end(),category->label)!=names.end();
    menuOptions_.push_back("Game details");
    menuOptions_.push_back("Play random game");
    menuOptions_.push_back("Delete game");
    if(category->label=="FAVOURITE" || currentIsCustomList)
        menuOptions_.push_back("Remove from this list");
    if(currentIsCustomList) menuOptions_.push_back("Delete list...");
    overlaySelection_=0;
    overlay_=Overlay::SelectMenu;
}

void Launcher::addSelectedToList(const std::string& label){
    Category* current=currentCategory();
    if(!current || current->entries.empty()) return;
    const Game game=current->entries[static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(current->entries.size())))];
    const std::string path=collectionPath(label);
    if(path.empty() || !writeListEntry(path,game,true)){
        statusMessage_="Could not update list";
        statusUntil_=SDL_GetTicks64()+2200;
        return;
    }
    for(Category& category:categories_){
        if(category.label!=label) continue;
        const std::string target=comparableRomPath(game.romPath,sdRoot_);
        const bool exists=std::any_of(category.entries.begin(),category.entries.end(),[&](const Game& item){return comparableRomPath(item.romPath,sdRoot_)==target;});
        if(!exists){Game copy=game;copy.artwork=nullptr;category.entries.push_back(std::move(copy));}
        std::sort(category.entries.begin(),category.entries.end(),[](const Game& a,const Game& b){return lower(a.title)<lower(b.title);});
        break;
    }
    statusMessage_="Added to "+label;
    statusUntil_=SDL_GetTicks64()+2200;
    overlay_=Overlay::None;
}

void Launcher::toggleSelectedFavorite(){
    Category* category=currentCategory();
    if(!category || category->entries.empty()) return;
    const Game game=category->entries[static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(category->entries.size())))];
    const std::string path=sdRoot_+"/Roms/favourite.json";
    const bool wasFavorite=isSelectedFavorite();
    bool success=false;
    if(wasFavorite){
        success=removeListEntry(path,game);
        if(success) for(Category& item:categories_) if(item.label=="FAVOURITE"){
                for(Game& entry:item.entries) if(comparableRomPath(entry.romPath,sdRoot_)==comparableRomPath(game.romPath,sdRoot_) && entry.artwork){SDL_DestroyTexture(entry.artwork);entry.artwork=nullptr;}
                item.entries.erase(std::remove_if(item.entries.begin(),item.entries.end(),[&](const Game& entry){return comparableRomPath(entry.romPath,sdRoot_)==comparableRomPath(game.romPath,sdRoot_);}),item.entries.end());
            }
    }else{
        success=writeListEntry(path,game,true);
        if(success){
            auto found=std::find_if(categories_.begin(),categories_.end(),[](const Category& item){return item.label=="FAVOURITE";});
            if(found==categories_.end()){
                Category favorites; favorites.label="FAVOURITE"; Game copy=game;copy.artwork=nullptr;favorites.entries.push_back(std::move(copy));
                auto systemApps=std::find_if(categories_.begin(),categories_.end(),[](const Category& item){return item.label=="APPS";});
                const size_t index=static_cast<size_t>(systemApps-categories_.begin());
                categories_.insert(systemApps,std::move(favorites));
                rememberedSelections_.insert(rememberedSelections_.begin()+static_cast<std::ptrdiff_t>(index),0);
                if(index<=categoryIndex_) ++categoryIndex_;
            }else{
                const bool already=std::any_of(found->entries.begin(),found->entries.end(),[&](const Game& entry){return comparableRomPath(entry.romPath,sdRoot_)==comparableRomPath(game.romPath,sdRoot_);});
                if(!already){Game copy=game;copy.artwork=nullptr;found->entries.push_back(std::move(copy));}
                std::sort(found->entries.begin(),found->entries.end(),[](const Game& a,const Game& b){return lower(a.title)<lower(b.title);});
            }
        }
    }
    statusMessage_=success?(wasFavorite?"Removed from favorites":"Added to favorites"):"Could not update favorites";
    statusUntil_=SDL_GetTicks64()+2200;
    overlay_=Overlay::None;
}

void Launcher::removeSelectedFromList(){
    Category* category=currentCategory();
    if(!category || category->entries.empty()) return;
    const Game game=category->entries[static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(category->entries.size())))];
    const std::string path=collectionPath(category->label);
    if(path.empty() || !removeListEntry(path,game)){
        statusMessage_="Could not remove game from list";
        statusUntil_=SDL_GetTicks64()+2200;
        return;
    }
    for(Game& item:category->entries){
        if(comparableRomPath(item.romPath,sdRoot_)==comparableRomPath(game.romPath,sdRoot_) && item.artwork){SDL_DestroyTexture(item.artwork);item.artwork=nullptr;}
    }
    category->entries.erase(std::remove_if(category->entries.begin(),category->entries.end(),[&](const Game& item){return comparableRomPath(item.romPath,sdRoot_)==comparableRomPath(game.romPath,sdRoot_);}),category->entries.end());
    position_=target_=0.0f;
    rememberedSelections_[categoryIndex_]=0;
    statusMessage_="Removed from "+category->label;
    statusUntil_=SDL_GetTicks64()+2200;
    overlay_=Overlay::None;
}

void Launcher::createList(){
    std::string name=newListName_;
    while(!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
    while(!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
    if(name.empty() || name[0]=='-' || name=="." || name==".."){
        statusMessage_="Enter a list name first";
        statusUntil_=SDL_GetTicks64()+2200;
        SDL_StartTextInput();
        return;
    }
    for(char& c:name) if(c=='/' || c=='\\' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|') c='_';
    const std::string path=collectionPath(name);
    if(path.empty() || access(path.c_str(),F_OK)==0){
        statusMessage_=path.empty()?"Invalid list name":"That list already exists";
        statusUntil_=SDL_GetTicks64()+2200;
        SDL_StartTextInput();
        return;
    }
    std::ofstream file(path.c_str(),std::ios::out|std::ios::trunc);
    if(!file){statusMessage_="Could not create list";statusUntil_=SDL_GetTicks64()+2200;SDL_StartTextInput();return;}
    file.close();
    if(!file){statusMessage_="Could not create list";statusUntil_=SDL_GetTicks64()+2200;SDL_StartTextInput();return;}
    Category created; created.label=name;
    auto systemApps=std::find_if(categories_.begin(),categories_.end(),[](const Category& item){return item.label=="APPS";});
    const size_t index=static_cast<size_t>(systemApps-categories_.begin());
    categories_.insert(systemApps,std::move(created));
    rememberedSelections_.insert(rememberedSelections_.begin()+static_cast<std::ptrdiff_t>(index),0);
    if(index<=categoryIndex_) ++categoryIndex_;
    SDL_StopTextInput();
    newListName_.clear();
    overlay_=Overlay::None;
    statusMessage_="Created list: "+name;
    statusUntil_=SDL_GetTicks64()+2200;
}

void Launcher::deleteSelectedList(){
    Category* category=currentCategory();
    if(!category) return;
    const std::vector<std::string> names=customListNames();
    if(std::find(names.begin(),names.end(),category->label)==names.end()){
        statusMessage_="Only named collections can be deleted here";
        statusUntil_=SDL_GetTicks64()+2400;
        overlay_=Overlay::None;
        return;
    }
    const std::string label=category->label;
    const std::string path=collectionPath(label);
    const size_t removedIndex=categoryIndex_;
    clearArtwork(category->entries);
    if(unlink(path.c_str())!=0){
        statusMessage_="Could not delete list";
        statusUntil_=SDL_GetTicks64()+2200;
        overlay_=Overlay::None;
        return;
    }
    categories_.erase(categories_.begin()+static_cast<std::ptrdiff_t>(removedIndex));
    rememberedSelections_.erase(rememberedSelections_.begin()+static_cast<std::ptrdiff_t>(removedIndex));
    if(categories_.empty()) categoryIndex_=0;
    else if(categoryIndex_>=categories_.size()) categoryIndex_=categories_.size()-1;
    position_=target_=static_cast<float>(rememberedSelections_.empty()?0:rememberedSelections_[categoryIndex_]);
    statusMessage_="Deleted list: "+label;
    statusUntil_=SDL_GetTicks64()+2200;
    overlay_=Overlay::None;
}

void Launcher::refreshAll(){
    std::string priorCategory;
    std::string priorEntry;
    if(const Category* category=currentCategory()){
        priorCategory=category->label;
        if(!category->entries.empty()){
            const int index=wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(category->entries.size()));
            priorEntry=recentEntryKey(category->entries[static_cast<size_t>(index)],sdRoot_);
        }
    }
    for(Category& category:categories_) clearArtwork(category.entries);
    categories_.clear();
    rememberedSelections_.clear();
    categoryIndex_=0;
    position_=target_=0.0f;
    discoverGames(true);
    // X explicitly rebuilds every active system, even indexes not opened yet.
    for(const SystemInfo& system:systems_) artworkIndexFor(system,true);
    reloadBackgroundChoices();
    for(size_t c=0;c<categories_.size();++c){
        if(categories_[c].label!=priorCategory) continue;
        categoryIndex_=c;
        if(!priorEntry.empty()){
            for(size_t i=0;i<categories_[c].entries.size();++i){
                if(recentEntryKey(categories_[c].entries[i],sdRoot_)==priorEntry){
                    position_=target_=static_cast<float>(i);
                    rememberedSelections_[c]=static_cast<int>(i);
                    break;
                }
            }
        }
        break;
    }
    if(overlay_==Overlay::Search) updateSearchResults();
    statusMessage_="ROMs and lists refreshed";
    statusUntil_=SDL_GetTicks64()+2200;
}

void Launcher::deleteSelectedGame(){
    Category* category=currentCategory();
    if(!category || category->entries.empty()) return;
    const Game game=category->entries[static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(category->entries.size())))];
    const bool knownConsole=std::any_of(systems_.begin(),systems_.end(),[&](const SystemInfo& system){return game.console==system.id;});
    char resolved[PATH_MAX];
    struct stat romInfo{};
    const std::string expectedPrefix=sdRoot_+"/Roms/"+game.console+"/";
    if(!knownConsole || lstat(game.romPath.c_str(),&romInfo)!=0 || !S_ISREG(romInfo.st_mode) ||
       !realpath(game.romPath.c_str(),resolved) || std::string(resolved).compare(0,expectedPrefix.size(),expectedPrefix)!=0 || unlink(resolved)!=0){
        statusMessage_="Could not safely delete this ROM";
        statusUntil_=SDL_GetTicks64()+2600;
        overlay_=Overlay::None;
        return;
    }
    // A deleted ROM must not reappear from the next boot's saved catalog.
    unlink((sdRoot_+kGameCatalogPath).c_str());
    const std::string target=comparableRomPath(game.romPath,sdRoot_);
    std::vector<std::string> manifests{sdRoot_+"/Roms/favourite.json",sdRoot_+"/Roms/recentlist.json"};
    for(const std::string& name:customListNames()) manifests.push_back(collectionPath(name));
    for(const std::string& manifest:manifests) removeListEntry(manifest,game);
    for(size_t c=0;c<categories_.size();++c){
        std::vector<Game>& entries=categories_[c].entries;
        for(Game& item:entries) if(comparableRomPath(item.romPath,sdRoot_)==target && item.artwork){SDL_DestroyTexture(item.artwork);item.artwork=nullptr;}
        entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const Game& item){return comparableRomPath(item.romPath,sdRoot_)==target;}),entries.end());
        if(c==categoryIndex_ && entries.empty()) position_=target_=0.0f;
    }
    statusMessage_="Deleted game ROM (saves untouched)";
    statusUntil_=SDL_GetTicks64()+2600;
    overlay_=Overlay::None;
}

void Launcher::startSearch(){
    searchQuery_.clear();
    searchResultsFocused_=false;
    searchResultSelection_=0;
    updateSearchResults();
    SDL_StartTextInput();
    overlay_=Overlay::Search;
}

void Launcher::updateSearchResults(){
    searchResults_.clear();
    searchResultSelection_=0;
    std::array<std::vector<std::pair<size_t,size_t>>,3> ranked;
    std::unordered_set<std::string> seen;
    const std::string query=lower(searchQuery_);
    // An empty query is the search screen's recent-games view. Do not treat an
    // empty substring as matching the entire ROM library: that also exposes
    // stale/cached entries and makes the initial view unpredictable.
    size_t firstCategory=0;
    size_t lastCategory=categories_.size();
    if(query.empty()){
        const auto recent=std::find_if(categories_.begin(),categories_.end(),[](const Category& category){return lower(category.label)=="recent";});
        if(recent==categories_.end()) return;
        firstCategory=static_cast<size_t>(std::distance(categories_.begin(),recent));
        lastCategory=firstCategory+1;
    }
    for(size_t c=firstCategory;c<lastCategory;++c){
        for(size_t i=0;i<categories_[c].entries.size();++i){
            const Game& game=categories_[c].entries[i];
            if(game.kind!=EntryKind::Game || game.title.empty()) continue;
            const std::string title=lower(game.title);
            size_t match=title.find(query);
            if(match==std::string::npos) continue;
            const std::string key=comparableRomPath(game.romPath,sdRoot_);
            if(seen.find(key)!=seen.end()) continue;
            // Search can outlive edits made from OnionOS or the computer while
            // CartOS stays open. Hide ROMs removed since the library scan.
            if(!regularFile(game.romPath)) continue;
            seen.insert(key);
            int rank=2;
            for(;match!=std::string::npos;match=title.find(query,match+1)){
                if(match==0){rank=0;break;}
                if(!std::isalnum(static_cast<unsigned char>(title[match-1]))){rank=1;break;}
            }
            ranked[rank].push_back({c,i});
        }
    }
    for(const auto& group:ranked) searchResults_.insert(searchResults_.end(),group.begin(),group.end());
}

void Launcher::moveSearchSelection(int direction){
    if(searchResults_.empty()){searchResultSelection_=0;return;}
    searchResultSelection_=wrapIndex(searchResultSelection_+direction,static_cast<int>(searchResults_.size()));
}

void Launcher::handleSearchInput(const SDL_Event& e){
    if(e.type==SDL_TEXTINPUT && !searchResultsFocused_){
        searchQuery_+=e.text.text;
        updateSearchResults();
        return;
    }
    if(e.type==SDL_KEYDOWN && !searchResultsFocused_){
        const SDL_Keycode key=e.key.keysym.sym;
        if((key>=SDLK_a && key<=SDLK_z) || (key>=SDLK_0 && key<=SDLK_9) || key==SDLK_SPACE || key==SDLK_MINUS){
            searchQuery_+=static_cast<char>(key==SDLK_SPACE?' ':key);
            updateSearchResults();
        }
    }
}

bool Launcher::selectedInSearch(const Game*& game) const{
    if(searchResults_.empty()) return false;
    const auto ref=searchResults_[static_cast<size_t>(wrapIndex(searchResultSelection_,static_cast<int>(searchResults_.size())))];
    if(ref.first>=categories_.size() || ref.second>=categories_[ref.first].entries.size()) return false;
    game=&categories_[ref.first].entries[ref.second];
    return true;
}

long long Launcher::playtimeSecondsFor(const Game& game){
    if(playtimeLookupRom_==game.romPath) return playtimeLookupSeconds_;
    playtimeLookupRom_=game.romPath;
    playtimeLookupSeconds_=-1;

    const std::string romPrefix=sdRoot_+"/Roms/";
    if(game.romPath.compare(0,romPrefix.size(),romPrefix)!=0) return playtimeLookupSeconds_;
    const std::string relativePath=game.romPath.substr(romPrefix.size());
    const std::string databasePath=sdRoot_+"/Saves/CurrentProfile/play_activity/play_activity_db.sqlite";

    // Activity Tracker ships SQLite with OnionOS. Load its C API dynamically
    // so the handheld build and older device toolchains do not need SQLite headers.
    void* library=dlopen((sdRoot_+"/.tmp_update/lib/libsqlite3.so").c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!library) library=dlopen("libsqlite3.so.0",RTLD_NOW|RTLD_LOCAL);
    if(!library) library=dlopen("libsqlite3.so",RTLD_NOW|RTLD_LOCAL);
#ifdef __APPLE__
    if(!library) library=dlopen("/usr/lib/libsqlite3.dylib",RTLD_NOW|RTLD_LOCAL);
#endif
    if(!library) return playtimeLookupSeconds_;

    using Database=void;
    using Statement=void;
    using Open= int(*)(const char*,Database**,int,const char*);
    using Close= int(*)(Database*);
    using Prepare= int(*)(Database*,const char*,int,Statement**,const char**);
    using BindText= int(*)(Statement*,int,const char*,int,void(*)(void*));
    using Step= int(*)(Statement*);
    using ColumnInt64= long long(*)(Statement*,int);
    using Finalize= int(*)(Statement*);
    const auto open=reinterpret_cast<Open>(dlsym(library,"sqlite3_open_v2"));
    const auto close=reinterpret_cast<Close>(dlsym(library,"sqlite3_close"));
    const auto prepare=reinterpret_cast<Prepare>(dlsym(library,"sqlite3_prepare_v2"));
    const auto bindText=reinterpret_cast<BindText>(dlsym(library,"sqlite3_bind_text"));
    const auto step=reinterpret_cast<Step>(dlsym(library,"sqlite3_step"));
    const auto columnInt64=reinterpret_cast<ColumnInt64>(dlsym(library,"sqlite3_column_int64"));
    const auto finalize=reinterpret_cast<Finalize>(dlsym(library,"sqlite3_finalize"));
    if(!open || !close || !prepare || !bindText || !step || !columnInt64 || !finalize){
        dlclose(library);
        return playtimeLookupSeconds_;
    }

    Database* database=nullptr;
    if(open(databasePath.c_str(),&database,1,nullptr)!=0 || !database){
        if(database) close(database);
        dlclose(library);
        return playtimeLookupSeconds_;
    }
    const char* sql="SELECT COALESCE(SUM(pa.play_time),0), COUNT(*) FROM rom r JOIN play_activity pa ON pa.rom_id=r.id WHERE r.file_path=?1";
    Statement* statement=nullptr;
    const bool prepared=prepare(database,sql,-1,&statement,nullptr)==0 && statement;
    if(prepared){
        const auto transient=reinterpret_cast<void(*)(void*)>(static_cast<intptr_t>(-1));
        if(bindText(statement,1,relativePath.c_str(),-1,transient)==0 && step(statement)==100){
            const long long sessions=columnInt64(statement,1);
            if(sessions>0) playtimeLookupSeconds_=columnInt64(statement,0);
        }
        finalize(statement);
    }
    close(database);
    dlclose(library);
    return playtimeLookupSeconds_;
}

void Launcher::launchSearchSelected(){
    if(searchResults_.empty()) return;
    const auto ref=searchResults_[static_cast<size_t>(wrapIndex(searchResultSelection_,static_cast<int>(searchResults_.size())))];
    if(ref.first>=categories_.size() || ref.second>=categories_[ref.first].entries.size()) return;
    beginLaunchAnimation(ref.first,ref.second);
}

std::vector<std::pair<size_t,size_t>> Launcher::randomGameCandidates() const{
    std::vector<std::pair<size_t,size_t>> candidates;
    std::unordered_set<std::string> seen;
    for(size_t c=0;c<categories_.size();++c){
        const Category& category=categories_[c];
        // System libraries contain the whole device library, including hidden
        // systems. Recent and saved-list copies must not bias the selection.
        if(category.systemId.empty()) continue;
        for(size_t i=0;i<category.entries.size();++i){
            const Game& game=category.entries[i];
            if(game.kind!=EntryKind::Game || game.romPath.empty() || game.launcherPath.empty() ||
               isUtilityEntry(game.title,game.romPath)) continue;
            if(seen.insert(comparableRomPath(game.romPath,sdRoot_)).second)
                candidates.emplace_back(c,i);
        }
    }
    return candidates;
}

void Launcher::playRandomGame(){
    const auto candidates=randomGameCandidates();
    overlay_=Overlay::None;
    if(candidates.empty()){
        statusMessage_="No games available";
        statusUntil_=SDL_GetTicks64()+2200;
        return;
    }
    // Seed once per process, and give every unique ROM an equal chance.
    static std::mt19937 generator(static_cast<unsigned int>(SDL_GetPerformanceCounter()) ^
        static_cast<unsigned int>(std::time(nullptr)) ^ static_cast<unsigned int>(getpid()));
    const auto selected=candidates[std::uniform_int_distribution<size_t>(0,candidates.size()-1)(generator)];
    if(const Category* category=currentCategory()){
        if(!category->entries.empty() && categoryIndex_<rememberedSelections_.size())
            rememberedSelections_[categoryIndex_]=wrapIndex(static_cast<int>(std::lround(position_)),
                static_cast<int>(category->entries.size()));
    }
    categoryIndex_=selected.first;
    position_=target_=static_cast<float>(selected.second);
    if(categoryIndex_<rememberedSelections_.size())
        rememberedSelections_[categoryIndex_]=static_cast<int>(selected.second);
    launchSelected();
}

void Launcher::activateSelectMenu(){
    if(overlaySelection_<0 || static_cast<size_t>(overlaySelection_)>=menuOptions_.size()) return;
    const std::string action=menuOptions_[static_cast<size_t>(overlaySelection_)];
    if(action=="Move list up"){
        moveList(-1);
    }else if(action=="Move list down"){
        moveList(1);
    }else if(action=="Add to list"){
        listOptions_=customListNames();
        if(listOptions_.empty()){
            statusMessage_="Create a list first";
            statusUntil_=SDL_GetTicks64()+2200;
            return;
        }
        overlaySelection_=0;
        overlay_=Overlay::AddToList;
    }else if(action=="Add to favourites" || action=="Remove from favourites"){
        toggleSelectedFavorite();
    }else if(action=="Create new list"){
        newListName_.clear();
        virtualKey_=0;
        SDL_StartTextInput();
        overlay_=Overlay::CreateList;
    }else if(action=="Remove from this list"){
        removeSelectedFromList();
    }else if(action=="Game details"){
        playtimeLookupRom_.clear();
        overlay_=Overlay::GameInfo;
    }else if(action=="Play random game"){
        playRandomGame();
    }else if(action=="Delete game"){
        overlaySelection_=1;
        overlay_=Overlay::DeleteConfirm;
    }else if(action=="Delete list..."){
        overlaySelection_=1;
        overlay_=Overlay::DeleteListConfirm;
    }
}

void Launcher::handle(const SDL_Event& physical){
    // The backend emits raw joystick events as well as mapped controller
    // events. Process one stream only, so one press never acts twice.
    if(controller_ && (physical.type==SDL_JOYBUTTONDOWN || physical.type==SDL_JOYBUTTONUP ||
       physical.type==SDL_JOYHATMOTION || physical.type==SDL_JOYAXISMOTION))return;
    // Y is a search control on the handheld, but a printable character on a
    // desktop keyboard. Let SDL text input deliver it while Search is open.
    const bool typingSearchY=overlay_==Overlay::Search &&
        physical.type==SDL_KEYDOWN && physical.key.keysym.sym==SDLK_y;
    const SDL_Event e=typingSearchY?physical:accessibility::translate(physical,accessibility_);
    if(e.type==SDL_QUIT){running_=false;return;}
    if(e.type==SDL_KEYUP){
        switch(e.key.keysym.sym){
            case SDLK_LEFT: case SDLK_a: setScrollHeld(0,false); break;
            case SDLK_RIGHT: case SDLK_d: setScrollHeld(1,false); break;
            case SDLK_UP: case SDLK_w: setScrollHeld(2,false); break;
            case SDLK_DOWN: case SDLK_s: setScrollHeld(3,false); break;
            default: break;
        }
    }else if(e.type==SDL_CONTROLLERBUTTONUP){
        switch(e.cbutton.button){
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: setScrollHeld(0,false); break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: setScrollHeld(1,false); break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP: setScrollHeld(2,false); break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: setScrollHeld(3,false); break;
            default: break;
        }
    }else if(e.type==SDL_JOYBUTTONUP && !controller_){
        switch(e.jbutton.button){
            case 2: setScrollHeld(0,false); break;
            case 3: setScrollHeld(1,false); break;
            case 0: setScrollHeld(2,false); break;
            case 1: setScrollHeld(3,false); break;
            default: break;
        }
    }
    // SELECT is also OnionOS's brightness modifier. Wait for its release
    // before opening browser options, and consume CartOS actions in a chord.
    // keymon reads the physical buttons separately and still sees the shortcut.
    if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_LOST){
        selectButtonHeld_=selectButtonChord_=selectOptionsPending_=false;
        startButtonHeld_=startButtonChord_=false;
        scrollHeld_.fill(false);
    }
    const bool selectKey=(e.type==SDL_KEYDOWN || e.type==SDL_KEYUP) &&
        (e.key.keysym.sym==SDLK_RSHIFT || e.key.keysym.sym==SDLK_RCTRL);
    const bool selectController=(e.type==SDL_CONTROLLERBUTTONDOWN || e.type==SDL_CONTROLLERBUTTONUP) &&
        e.cbutton.button==SDL_CONTROLLER_BUTTON_BACK;
    const bool selectJoystick=!controller_ &&
        (e.type==SDL_JOYBUTTONDOWN || e.type==SDL_JOYBUTTONUP) &&
        e.jbutton.button==12; // MMIYOO_BUTTON_SELECT in the bundled SDL backend
    const bool buttonDown=e.type==SDL_KEYDOWN || e.type==SDL_CONTROLLERBUTTONDOWN ||
        (e.type==SDL_JOYBUTTONDOWN && !controller_);
    const bool startEvent=((e.type==SDL_CONTROLLERBUTTONDOWN || e.type==SDL_CONTROLLERBUTTONUP) && e.cbutton.button==SDL_CONTROLLER_BUTTON_START) ||
        ((e.type==SDL_KEYDOWN || e.type==SDL_KEYUP) && e.key.keysym.sym==SDLK_RETURN) ||
        (!controller_ && (e.type==SDL_JOYBUTTONDOWN || e.type==SDL_JOYBUTTONUP) && e.jbutton.button==13);
    if(selectButtonHeld_ && startEvent && buttonDown){
        // Physical SELECT+START always recovers controls, independent of maps.
        selectButtonChord_=true;startButtonHeld_=false;
        accessibility_.resetBindings();const bool saved=saveAccessibility();
        resetPreviewArtwork();launchAnimation_=launchAwaitingConfirm_=launchReversing_=launchConfirmationOpen_=false;
        openAccessibility();statusMessage_=saved?"Default controls restored":"Controls restored (could not save)";statusUntil_=SDL_GetTicks64()+2200;
        return;
    }
    if(selectKey || selectController || selectJoystick){
        if(buttonDown){
            if(!selectButtonHeld_){
                selectButtonHeld_=true;
                selectButtonChord_=menuButtonHeld_;
                selectOptionsPending_=overlay_==Overlay::None && !launchAnimation_;
                scrollHeld_.fill(false);
                if(menuButtonHeld_) menuButtonChord_=true;
            }
            // Preserve SELECT's existing contextual action in search/menus.
            if(selectOptionsPending_ || launchAnimation_) return;
        }else{
            const bool showOptions=selectButtonHeld_ && selectOptionsPending_ && !selectButtonChord_ &&
                overlay_==Overlay::None && !launchAnimation_;
            selectButtonHeld_=selectButtonChord_=selectOptionsPending_=false;
            if(showOptions){openSelectMenu();noteInput("OPTIONS");}
            return;
        }
    }else if(selectButtonHeld_){
        const bool movedAxis=(e.type==SDL_CONTROLLERAXISMOTION && std::abs(e.caxis.value)>8000) ||
            (e.type==SDL_JOYAXISMOTION && !controller_ && std::abs(e.jaxis.value)>8000);
        const bool movedHat=e.type==SDL_JOYHATMOTION && !controller_ && e.jhat.value!=SDL_HAT_CENTERED;
        if(buttonDown || movedAxis || movedHat){
            selectButtonChord_=true;
            return;
        }
        if(e.type==SDL_TEXTINPUT) return;
    }
    if(!controller_ && (e.type==SDL_JOYBUTTONDOWN || e.type==SDL_JOYBUTTONUP) && e.jbutton.button==14){
        if(e.type==SDL_JOYBUTTONDOWN){if(!menuButtonHeld_)beginMenuPress();}else endMenuPress();
        return;
    }
    const auto isMenuButton=[](Uint8 button){
        return button==SDL_CONTROLLER_BUTTON_GUIDE || button==SDL_CONTROLLER_BUTTON_MISC1;
    };
    if(e.type==SDL_CONTROLLERBUTTONDOWN && isMenuButton(e.cbutton.button)){
        if(!menuButtonHeld_) beginMenuPress();
        return;
    }
    if(e.type==SDL_CONTROLLERBUTTONUP && isMenuButton(e.cbutton.button)){
        endMenuPress();
        return;
    }
    if(e.type==SDL_KEYDOWN && !e.key.repeat && (e.key.keysym.sym==SDLK_MENU || e.key.keysym.sym==SDLK_HOME
#if defined(LAUNCHER_DEVICE)
        || e.key.keysym.sym==SDLK_ESCAPE
#endif
        )){
        if(!menuButtonHeld_) beginMenuPress();
        return;
    }
    if(e.type==SDL_KEYUP && (e.key.keysym.sym==SDLK_MENU || e.key.keysym.sym==SDLK_HOME
#if defined(LAUNCHER_DEVICE)
        || e.key.keysym.sym==SDLK_ESCAPE
#endif
        )){
        endMenuPress();
        return;
    }
    if(menuButtonHeld_){
        const bool movedAxis=(e.type==SDL_CONTROLLERAXISMOTION && std::abs(e.caxis.value)>8000) ||
            (e.type==SDL_JOYAXISMOTION && std::abs(e.jaxis.value)>8000);
        const bool movedHat=e.type==SDL_JOYHATMOTION && e.jhat.value!=SDL_HAT_CENTERED;
        if(buttonDown || movedAxis || movedHat)menuButtonChord_=true;
        return;
    }
    if(startButtonHeld_){
        if(startEvent && !buttonDown){
            const bool launch=!startButtonChord_ && !menuButtonHeld_ && !selectButtonHeld_;
            startButtonHeld_=startButtonChord_=false;
            if(launch)launchSelected(true);
            return;
        }
        const bool axis=(e.type==SDL_CONTROLLERAXISMOTION && std::abs(e.caxis.value)>8000) ||
            (e.type==SDL_JOYAXISMOTION && std::abs(e.jaxis.value)>8000);
        if((buttonDown && !startEvent) || axis || (e.type==SDL_JOYHATMOTION && e.jhat.value)){
            startButtonChord_=true;return;
        }
    }
    if(startEvent && buttonDown && overlay_==Overlay::None && !launchAnimation_ && !menuButtonHeld_){
        startButtonHeld_=true;startButtonChord_=false;scrollHeld_.fill(false);noteInput("START");return;
    }
    if(buttonDown && !selectKey && !selectController && !selectJoystick && !menuButtonHeld_){
        const int action=accessibility::physicalButton(e);
        if(action>=0)noteInput(accessibility::actionName(action));
    }
    if(launchConfirmationOpen_){
        if(e.type==SDL_CONTROLLERBUTTONDOWN){
            const Uint8 b=e.cbutton.button;
            if(b==SDL_CONTROLLER_BUTTON_B)cancelLaunchConfirmation();
            else if(b==SDL_CONTROLLER_BUTTON_DPAD_UP || b==SDL_CONTROLLER_BUTTON_DPAD_DOWN ||
                    b==SDL_CONTROLLER_BUTTON_DPAD_LEFT || b==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)launchConfirmationSelection_=1-launchConfirmationSelection_;
            else if(b==SDL_CONTROLLER_BUTTON_A){
                if(launchConfirmationSelection_==0)commitGameLaunch();
                else cancelLaunchConfirmation();
            }
        }else if(e.type==SDL_KEYDOWN && !e.key.repeat){
            if(e.key.keysym.sym==SDLK_ESCAPE)cancelLaunchConfirmation();
            else if(e.key.keysym.sym==SDLK_RETURN){if(launchConfirmationSelection_==0)commitGameLaunch();else cancelLaunchConfirmation();}
        }
        return;
    }
    if(launchAnimation_){
        const bool confirmPressed=(e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.button==SDL_CONTROLLER_BUTTON_A) ||
            (e.type==SDL_KEYDOWN && !e.key.repeat && (e.key.keysym.sym==SDLK_RETURN || e.key.keysym.sym==SDLK_SPACE));
        const bool previewLeft=(e.type==SDL_KEYDOWN && !e.key.repeat &&
                                (e.key.keysym.sym==SDLK_LEFT || e.key.keysym.sym==SDLK_a)) ||
                               (e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
                               (e.type==SDL_JOYBUTTONDOWN && !controller_ && e.jbutton.button==2) ||
                               (e.type==SDL_JOYHATMOTION && (e.jhat.value&SDL_HAT_LEFT)!=0);
        const bool previewRight=(e.type==SDL_KEYDOWN && !e.key.repeat &&
                                 (e.key.keysym.sym==SDLK_RIGHT || e.key.keysym.sym==SDLK_d)) ||
                                (e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
                                (e.type==SDL_JOYBUTTONDOWN && !controller_ && e.jbutton.button==3) ||
                                (e.type==SDL_JOYHATMOTION && (e.jhat.value&SDL_HAT_RIGHT)!=0);
        if(launchAwaitingConfirm_){
            if(confirmPressed){
                requestGameLaunch();
            }else if(previewLeft){
                browsePreviewPage(-1);
            }else if(previewRight){
                browsePreviewPage(1);
            }else if((e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.button==SDL_CONTROLLER_BUTTON_B) ||
                     (e.type==SDL_KEYDOWN && !e.key.repeat && (e.key.keysym.sym==SDLK_ESCAPE || e.key.keysym.sym==SDLK_BACKSPACE))){
                // Restore the primary artwork before the first reverse frame,
                // so B never briefly shows an alternate image in the browser.
                resetPreviewArtwork();
                launchAwaitingConfirm_=false;
                launchReversing_=!accessibility_.reducedMotion;
                if(accessibility_.reducedMotion)launchAnimation_=false;
                launchReverseElapsed_=0.0f;
                launchStepTwoConfirmed_=false;
                if(overlay_!=Overlay::Search)position_=target_=launchCenteringPosition_;
                launchCenteringOffset_=0.0f;
            }
        }else if(confirmPressed && !launchReversing_ &&
                 SDL_GetTicks64()-launchFirstPressAt_<=kLaunchDoubleTapWindowMs){
            requestGameLaunch();
        }
        return;
    }

    if(handleAccessibilityOverlay(e))return;

    if(overlay_==Overlay::Search){
        if(e.type==SDL_TEXTINPUT){if(!searchResultsFocused_)handleSearchInput(e);return;}
        if(e.type==SDL_KEYDOWN && !e.key.repeat){
            const SDL_Keycode key=e.key.keysym.sym;
            if(!searchResultsFocused_){
                if(key==SDLK_BACKSPACE){if(!searchQuery_.empty()) searchQuery_.pop_back();updateSearchResults();}
                else if(key==SDLK_RETURN || key==SDLK_TAB) searchResultsFocused_=true;
                else if(key==SDLK_ESCAPE){SDL_StopTextInput();overlay_=Overlay::None;}
                else if(key==SDLK_UP || key==SDLK_w) virtualKey_=std::max(0,virtualKey_-10);
                else if(key==SDLK_DOWN || key==SDLK_s) virtualKey_=std::min(39,virtualKey_+10);
                else if(key==SDLK_LEFT || key==SDLK_a) virtualKey_=std::max(0,virtualKey_-1);
                else if(key==SDLK_RIGHT || key==SDLK_d) virtualKey_=std::min(39,virtualKey_+1);
                else if(key==SDLK_x)refreshAll();
            }else{
                if(key==SDLK_LEFT || key==SDLK_a) moveSearchSelection(-1);
                else if(key==SDLK_RIGHT || key==SDLK_d) moveSearchSelection(1);
                else if(key==SDLK_UP || key==SDLK_w) moveSearchSelection(-1);
                else if(key==SDLK_DOWN || key==SDLK_s) moveSearchSelection(1);
                else if(key==SDLK_RETURN) launchSearchSelected();
                else if(key==SDLK_ESCAPE || key==SDLK_BACKSPACE || key==SDLK_TAB) searchResultsFocused_=false;
            }
            return;
        }
        if(e.type==SDL_CONTROLLERBUTTONDOWN){
            const Uint8 button=e.cbutton.button;
            if(button==SDL_CONTROLLER_BUTTON_B){
                if(searchResultsFocused_) searchResultsFocused_=false;
                else if(!searchQuery_.empty()){searchQuery_.pop_back();updateSearchResults();}
                else {SDL_StopTextInput();overlay_=Overlay::None;}
            }else if(button==SDL_CONTROLLER_BUTTON_X)refreshAll();
            else if(button==SDL_CONTROLLER_BUTTON_Y){SDL_StopTextInput();overlay_=Overlay::None;}
            else if(button==SDL_CONTROLLER_BUTTON_START) searchResultsFocused_=true;
            else if(button==SDL_CONTROLLER_BUTTON_A){
                if(searchResultsFocused_) launchSearchSelected();
                else{
                    searchQuery_.push_back(searchKeyboardCharacter(virtualKey_));
                    updateSearchResults();
                }
            }else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT){if(searchResultsFocused_)moveSearchSelection(-1);else virtualKey_=std::max(0,virtualKey_-1);}
            else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT){if(searchResultsFocused_)moveSearchSelection(1);else virtualKey_=std::min(39,virtualKey_+1);}
            else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP){if(searchResultsFocused_)moveSearchSelection(-1);else virtualKey_=std::max(0,virtualKey_-10);}
            else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN){if(searchResultsFocused_)moveSearchSelection(1);else virtualKey_=std::min(39,virtualKey_+10);}
            else if(button==SDL_CONTROLLER_BUTTON_BACK){SDL_StopTextInput();overlay_=Overlay::None;}
            return;
        }
        if(e.type==SDL_JOYBUTTONDOWN && !controller_){
            const int button=e.jbutton.button;
            if(button==5){if(searchResultsFocused_)searchResultsFocused_=false;else if(!searchQuery_.empty()){searchQuery_.pop_back();updateSearchResults();}else{SDL_StopTextInput();overlay_=Overlay::None;}}
            else if(button==6)refreshAll();
            else if(button==7){SDL_StopTextInput();overlay_=Overlay::None;}
            else if(button==4){if(searchResultsFocused_)launchSearchSelected();else{searchQuery_.push_back(searchKeyboardCharacter(virtualKey_));updateSearchResults();}}
            else if(button==10)searchResultsFocused_=true;
            else if(button==8){SDL_StopTextInput();overlay_=Overlay::None;}
            return;
        }
        if(e.type==SDL_JOYHATMOTION && !controller_){
            if(searchResultsFocused_){if(e.jhat.value&SDL_HAT_LEFT)moveSearchSelection(-1);else if(e.jhat.value&SDL_HAT_RIGHT)moveSearchSelection(1);}
            else if(e.jhat.value&SDL_HAT_LEFT)virtualKey_=std::max(0,virtualKey_-1);else if(e.jhat.value&SDL_HAT_RIGHT)virtualKey_=std::min(39,virtualKey_+1);else if(e.jhat.value&SDL_HAT_UP)virtualKey_=std::max(0,virtualKey_-10);else if(e.jhat.value&SDL_HAT_DOWN)virtualKey_=std::min(39,virtualKey_+10);
            return;
        }
        return;
    }

    if(overlay_!=Overlay::None){
        if(overlay_==Overlay::Settings){
            if(e.type==SDL_KEYDOWN && !e.key.repeat){
                const SDL_Keycode key=e.key.keysym.sym;
                if(key==SDLK_ESCAPE || key==SDLK_BACKSPACE){overlay_=Overlay::None;return;}
                if(key==SDLK_UP || key==SDLK_w) setScrollHeld(2,true);
                else if(key==SDLK_DOWN || key==SDLK_s) setScrollHeld(3,true);
                else if(key==SDLK_LEFT || key==SDLK_a) changeSetting(-1);
                else if(key==SDLK_RIGHT || key==SDLK_d) changeSetting(1);
                else if(key==SDLK_x) refreshAll();
                else if(key==SDLK_RETURN || key==SDLK_SPACE) changeSetting(1);
            }else if(e.type==SDL_CONTROLLERBUTTONDOWN){
                const Uint8 button=e.cbutton.button;
                if(button==SDL_CONTROLLER_BUTTON_B){overlay_=Overlay::None;return;}
                if(button==SDL_CONTROLLER_BUTTON_DPAD_UP) setScrollHeld(2,true);
                else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN) setScrollHeld(3,true);
                else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT) changeSetting(-1);
                else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT || button==SDL_CONTROLLER_BUTTON_A) changeSetting(1);
                else if(button==SDL_CONTROLLER_BUTTON_X) refreshAll();
            }
            return;
        }
        if(e.type==SDL_KEYDOWN && !e.key.repeat){
            const SDL_Keycode key=e.key.keysym.sym;
            if(overlay_==Overlay::CreateList){
                if(key==SDLK_ESCAPE){SDL_StopTextInput();overlay_=Overlay::None;}
                else if(key==SDLK_BACKSPACE){if(!newListName_.empty())newListName_.pop_back();}
                else if(key==SDLK_RETURN)createList();
                else if(key==SDLK_UP || key==SDLK_w)virtualKey_=std::max(0,virtualKey_-10);
                else if(key==SDLK_DOWN || key==SDLK_s)virtualKey_=std::min(39,virtualKey_+10);
                else if(key==SDLK_LEFT || key==SDLK_a)virtualKey_=std::max(0,virtualKey_-1);
                else if(key==SDLK_RIGHT || key==SDLK_d)virtualKey_=std::min(39,virtualKey_+1);
                return;
            }
            if(key==SDLK_ESCAPE || key==SDLK_BACKSPACE){overlay_=Overlay::None;return;}
            if(key==SDLK_UP || key==SDLK_w)--overlaySelection_;
            else if(key==SDLK_DOWN || key==SDLK_s)++overlaySelection_;
            else if(key==SDLK_RETURN || key==SDLK_SPACE){
                if(overlay_==Overlay::SelectMenu) activateSelectMenu();
                else if(overlay_==Overlay::AddToList && !listOptions_.empty()) addSelectedToList(listOptions_[static_cast<size_t>(wrapIndex(overlaySelection_,static_cast<int>(listOptions_.size())))]);
                else if(overlay_==Overlay::DeleteConfirm){const bool confirm=overlaySelection_==0;overlay_=Overlay::None;if(confirm)deleteSelectedGame();}
                else if(overlay_==Overlay::DeleteListConfirm){const bool confirm=overlaySelection_==0;overlay_=Overlay::None;if(confirm)deleteSelectedList();}
            }
        }else if(e.type==SDL_TEXTINPUT && overlay_==Overlay::CreateList){newListName_+=e.text.text;}
        else if(e.type==SDL_CONTROLLERBUTTONDOWN){
            const Uint8 button=e.cbutton.button;
            if(overlay_==Overlay::CreateList){
                if(button==SDL_CONTROLLER_BUTTON_B){SDL_StopTextInput();overlay_=Overlay::None;}
                else if(button==SDL_CONTROLLER_BUTTON_X){if(!newListName_.empty())newListName_.pop_back();}
                else if(button==SDL_CONTROLLER_BUTTON_START)createList();
                else if(button==SDL_CONTROLLER_BUTTON_A)newListName_.push_back(searchKeyboardCharacter(virtualKey_));
                else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)virtualKey_=std::max(0,virtualKey_-1);
                else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)virtualKey_=std::min(39,virtualKey_+1);
                else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)virtualKey_=std::max(0,virtualKey_-10);
                else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)virtualKey_=std::min(39,virtualKey_+10);
                return;
            }
            if(button==SDL_CONTROLLER_BUTTON_B){overlay_=Overlay::None;return;}
            if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)--overlaySelection_;
            else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)++overlaySelection_;
            else if(button==SDL_CONTROLLER_BUTTON_A || button==SDL_CONTROLLER_BUTTON_START){
                if(overlay_==Overlay::SelectMenu)activateSelectMenu();
                else if(overlay_==Overlay::AddToList && !listOptions_.empty())addSelectedToList(listOptions_[static_cast<size_t>(wrapIndex(overlaySelection_,static_cast<int>(listOptions_.size())))]);
                else if(overlay_==Overlay::DeleteConfirm){const bool confirm=overlaySelection_==0;overlay_=Overlay::None;if(confirm)deleteSelectedGame();}
                else if(overlay_==Overlay::DeleteListConfirm){const bool confirm=overlaySelection_==0;overlay_=Overlay::None;if(confirm)deleteSelectedList();}
            }
        }
        if(overlay_==Overlay::SelectMenu && !menuOptions_.empty()) overlaySelection_=wrapIndex(overlaySelection_,static_cast<int>(menuOptions_.size()));
        else if(overlay_==Overlay::AddToList && !listOptions_.empty()) overlaySelection_=wrapIndex(overlaySelection_,static_cast<int>(listOptions_.size()));
        else if(overlay_==Overlay::DeleteConfirm) overlaySelection_=wrapIndex(overlaySelection_,2);
        else if(overlay_==Overlay::DeleteListConfirm) overlaySelection_=wrapIndex(overlaySelection_,2);
        return;
    }

    if(e.type==SDL_KEYDOWN&&!e.key.repeat){
        switch(e.key.keysym.sym){
            case SDLK_LEFT: case SDLK_a: setScrollHeld(0,true); break;
            case SDLK_RIGHT: case SDLK_d: setScrollHeld(1,true); break;
            case SDLK_UP: case SDLK_w: setScrollHeld(2,true); break;
            case SDLK_DOWN: case SDLK_s: setScrollHeld(3,true); break;
#if !defined(LAUNCHER_DEVICE)
            case SDLK_PAGEUP: skipLetter(-1); break;
            case SDLK_PAGEDOWN: skipLetter(1); break;
#endif
            case SDLK_RETURN: launchSelected(true); break;
            case SDLK_SPACE: case SDLK_LALT: launchSelected(); break;

            case SDLK_TAB:
#if defined(LAUNCHER_DEVICE)
                skipLetter(-1);
#else
                openSelectMenu();
#endif
                break;
            case SDLK_y: startSearch(); break;
            case SDLK_x: refreshAll(); break;
            case SDLK_ESCAPE: case SDLK_LCTRL: running_=false; break;
            case SDLK_BACKSPACE:
#if defined(LAUNCHER_DEVICE)
                skipLetter(1);
#else
                running_=false;
#endif
                break;
        }
    }
    if(e.type==SDL_CONTROLLERBUTTONDOWN){
        switch(e.cbutton.button){
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: setScrollHeld(0,true); break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: setScrollHeld(1,true); break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP: setScrollHeld(2,true); break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: setScrollHeld(3,true); break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: skipLetter(-1); noteInput("PREVIOUS LETTER"); break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: skipLetter(1); noteInput("NEXT LETTER"); break;
            case SDL_CONTROLLER_BUTTON_A: launchSelected(); break;
            case SDL_CONTROLLER_BUTTON_START: launchSelected(true); break;
            case SDL_CONTROLLER_BUTTON_B: break;

            case SDL_CONTROLLER_BUTTON_Y: startSearch(); break;
            case SDL_CONTROLLER_BUTTON_X: refreshAll(); break;
            default: break;
        }
    }
    if(e.type==SDL_JOYHATMOTION && !controller_){
        setScrollHeld(0,(e.jhat.value&SDL_HAT_LEFT)!=0);
        setScrollHeld(1,(e.jhat.value&SDL_HAT_RIGHT)!=0);
        setScrollHeld(2,(e.jhat.value&SDL_HAT_UP)!=0);
        setScrollHeld(3,(e.jhat.value&SDL_HAT_DOWN)!=0);
    }
    if(e.type==SDL_JOYBUTTONDOWN && !controller_){
#if defined(LAUNCHER_DEVICE)
        switch(e.jbutton.button){
            case 0: setScrollHeld(2,true); break;
            case 1: setScrollHeld(3,true); break;
            case 2: setScrollHeld(0,true); break;
            case 3: setScrollHeld(1,true); break;
            case 4: launchSelected(); break;
            case 13: launchSelected(true); break; // MMIYOO_BUTTON_START
            case 5: break;
            case 7: startSearch(); break;
            case 6: refreshAll(); break;

            default: break;
        }
#else
        if(e.jbutton.button==0) launchSelected();
#endif
    }
}

namespace {
void arrow(SDL_Renderer* renderer,int x,int y,bool pointsUp,SDL_Color color){
    color=draw::foreground(color);
    SDL_SetRenderDrawColor(renderer,color.r,color.g,color.b,color.a);
    if(pointsUp){
        SDL_RenderDrawLine(renderer,x,y+8,x,y+2);
        SDL_RenderDrawLine(renderer,x,y+2,x-4,y+6);
        SDL_RenderDrawLine(renderer,x,y+2,x+4,y+6);
    }else{
        SDL_RenderDrawLine(renderer,x,y,x,y+6);
        SDL_RenderDrawLine(renderer,x,y+6,x-4,y+2);
        SDL_RenderDrawLine(renderer,x,y+6,x+4,y+2);
    }
}

void fillRect(SDL_Renderer* renderer,const SDL_Rect& rect,SDL_Color color){
    const bool selected=(color.r==32 && color.g==50 && color.b==72) ||
        (color.r==39 && color.g==64 && color.b==90) || (color.r==41 && color.g==70 && color.b==98);
    color=draw::panelColor(color);
    SDL_SetRenderDrawColor(renderer,color.r,color.g,color.b,color.a);
    SDL_RenderFillRect(renderer,&rect);
    if(selected)draw::selection(renderer,rect);
}

void outlineRect(SDL_Renderer* renderer,const SDL_Rect& rect,SDL_Color color){
    color=draw::foreground(color);
    SDL_SetRenderDrawColor(renderer,color.r,color.g,color.b,color.a);
    SDL_RenderDrawRect(renderer,&rect);
}

std::string shortLabel(std::string value,size_t limit=30){
    if(value.size()>limit) return value.substr(0,limit-3)+"...";
    return value;
}
}

void Launcher::render(float dt){
    const Uint32 now=SDL_GetTicks();
    if(batteryPollAt_==0 || static_cast<Uint32>(now-batteryPollAt_)>=5000){
        batteryPercent_=readBatteryPercent();
        batteryPollAt_=now;
    }
    applyAccessibility();
    draw::textBackground(textBackground_,invertTextColour_ && overlay_==Overlay::None);
    draw::suppressTextBackground(false);
    if(overlay_==Overlay::Accessibility || overlay_==Overlay::AccessibilityResetConfirm){renderAccessibility();return;}
    if(overlay_==Overlay::Search){renderSearch();return;}
    Category* category=currentCategory();
    if(!category || category->entries.empty()){
        renderBackground();
        if(category){
            draw::text(renderer_,18,38,category->label,26,white);
            const std::string empty="EMPTY LIST";
            draw::text(renderer_,(kDesignW-draw::textWidth(empty,30))/2,kDesignH/2,empty,30,white);
        }else{
            const std::string empty="NO LISTS FOUND";
            draw::text(renderer_,(kDesignW-draw::textWidth(empty,30))/2,kDesignH/2,empty,30,white);
        }
        if(overlay_==Overlay::SelectMenu){
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
            fillRect(renderer_,SDL_Rect{82,154,588,246},{4,7,15,240});
            outlineRect(renderer_,SDL_Rect{82,154,588,246},{70,82,100,255});
            draw::text(renderer_,(kDesignW-draw::textWidth("OPTIONS",26))/2,182,"OPTIONS",26,white);
            for(size_t i=0;i<menuOptions_.size();++i){
                const std::string item=(static_cast<int>(i)==overlaySelection_?"> ":"  ")+menuOptions_[i];
                draw::text(renderer_,(kDesignW-draw::textWidth(item,22))/2,238+static_cast<int>(i)*38,item,22,static_cast<int>(i)==overlaySelection_?SDL_Color{145,205,255,255}:white);
            }
            const std::string footer=controlHints("A SELECT   B BACK");
            draw::text(renderer_,(kDesignW-draw::textWidth(footer,16))/2,347,footer,16,muted);
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
        }else if(overlay_==Overlay::CreateList){
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
            fillRect(renderer_,SDL_Rect{58,85,636,overlay_==Overlay::Settings && accessibility_.textSize==2?418:390},{4,7,15,238});
            outlineRect(renderer_,SDL_Rect{58,85,636,overlay_==Overlay::Settings && accessibility_.textSize==2?418:390},{70,82,100,255});
            draw::text(renderer_,(kDesignW-draw::textWidth("CREATE NEW LIST",27))/2,105,"CREATE NEW LIST",27,white);
            const std::string name=newListName_.empty()?"TYPE A NAME":"NAME: "+newListName_;
            draw::text(renderer_,(kDesignW-draw::textWidth(name,23))/2,153,name,23,{145,205,255,255});
            for(int row=0;row<4;++row) for(int col=0;col<10;++col){
                const int index=row*10+col;
                const SDL_Rect keyRect{116+col*53,204+row*42,45,accessibility_.largeControls?38:32};
                if(index==virtualKey_)fillRect(renderer_,keyRect,{41,70,98,255});
                outlineRect(renderer_,keyRect,index==virtualKey_?SDL_Color{145,205,255,255}:SDL_Color{55,63,76,255});
                const char letter=searchKeyboardCharacter(index);
                const std::string caption=letter==' '?"SPC":std::string(1,letter);
                draw::text(renderer_,keyRect.x+(keyRect.w-draw::textWidth(caption,17))/2,keyRect.y+7,caption,17,index==virtualKey_?SDL_Color{145,205,255,255}:white);
            }
            const std::string footer=controlHints("A ADD   X BACKSPACE   START CREATE   B CANCEL");
            draw::text(renderer_,(kDesignW-draw::textWidth(footer,15))/2,425,footer,15,muted);
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
        }else if(overlay_==Overlay::DeleteListConfirm){
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
            fillRect(renderer_,SDL_Rect{82,154,588,246},{4,7,15,240});
            outlineRect(renderer_,SDL_Rect{82,154,588,246},{70,82,100,255});
            draw::text(renderer_,(kDesignW-draw::textWidth("DELETE LIST?",28))/2,183,"DELETE LIST?",28,white);
            const std::string label=currentCategory()?currentCategory()->label:"";
            draw::text(renderer_,(kDesignW-draw::textWidth(shortLabel(label,36),20))/2,234,shortLabel(label,36),20,{145,205,255,255});
            draw::text(renderer_,(kDesignW-draw::textWidth("Games stay on the SD card.",17))/2,268,"Games stay on the SD card.",17,muted);
            const std::array<std::string,2> choices{{"DELETE LIST","CANCEL"}};
            for(int i=0;i<2;++i){const SDL_Rect row{202,300+i*44,348,36};if(i==overlaySelection_)fillRect(renderer_,row,{32,50,72,255});draw::text(renderer_,(kDesignW-draw::textWidth(choices[static_cast<size_t>(i)],20))/2,row.y+5,choices[static_cast<size_t>(i)],20,i==overlaySelection_?SDL_Color{145,205,255,255}:white);}
            const std::string footer=controlHints("UP/DOWN CHOOSE   A CONFIRM   B CANCEL");
            draw::text(renderer_,(kDesignW-draw::textWidth(footer,15))/2,374,footer,15,muted);
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
        }
        presentFrame();
        return;
    }

    const bool accessibleLayout=accessibility_.textSize==2 || accessibility_.largeArtwork || accessibility_.largeControls;
    std::vector<Game>& entries=category->entries;
    // Settle the carousel quickly enough that held-key repeats do not stack
    // multiple moving cartridge positions on the Miyoo LCD.
    if(accessibility_.reducedMotion && !launchAnimation_)position_=target_;
    if(!launchAnimation_) position_+=(target_-position_)*(1.0f-std::exp(-dt*28.0f));
    const int entryCount=static_cast<int>(entries.size());
    const float count=static_cast<float>(entryCount);
    if(std::fabs(target_-position_)<0.001f){
        position_=target_;
        const float turns=std::floor(position_/count);
        position_-=turns*count; target_-=turns*count;
    }
    rememberedSelections_[categoryIndex_]=wrapIndex(static_cast<int>(std::lround(target_)),entryCount);
    renderBackground();

    // Keep the carousel's geometric center unwrapped while it animates across
    // either end. Only the entry lookup wraps; using the wrapped index for the
    // x-coordinate makes a one-step move appear to jump by the full list size.
    const int centerUnwrapped=static_cast<int>(std::lround(position_));
    const std::array<int,3> drawOrder{{-1,1,0}};
    std::array<int,3> visible{{-1,-1,-1}};
    for(int offset=-1;offset<=1;++offset){
        const int candidate=centerUnwrapped+offset;
        visible[static_cast<size_t>(offset+1)]=wrapIndex(candidate,entryCount);
    }
    for(int offset:drawOrder){
        const int candidate=centerUnwrapped+offset;
        const int idx=wrapIndex(candidate,entryCount);
        Game& entry=entries[static_cast<size_t>(idx)];
        if(!entry.artwork && !entry.artworkAttempted && !entry.artPath.empty()){
            entry.artworkAttempted=true;
            entry.artwork=artwork::loadPng(renderer_,entry.artPath);
        }
        const float delta=static_cast<float>(candidate)-position_;
        const float distance=std::fabs(delta);
        const float spacing=carouselSpacing_==0?215.0f:(carouselSpacing_==2?255.0f:240.0f);
        int cx=kDesignW/2+static_cast<int>(delta*spacing);
        int cy=254;
        float startupSideEase=1.0f;
        float startupCenterEase=1.0f;
        if(startupAnimation_){
            auto smooth=[](float value){
                const float t=std::max(0.0f,std::min(1.0f,value));
                return t*t*(3.0f-2.0f*t);
            };
            startupCenterEase=smooth(startupAnimationElapsed_/0.48f);
            startupSideEase=smooth((startupAnimationElapsed_-0.48f)/0.40f);
            if(offset!=0){
                const float direction=offset<0?-1.0f:1.0f;
                cx+=static_cast<int>(direction*460.0f*(1.0f-startupSideEase));
            }
        }
        if(launchAnimation_){
            const float animationTime=accessibility_.reducedMotion?0.50f:std::max(0.0f,launchAnimationElapsed_-launchCenterDuration_);
            const float reverseProgress=std::min(1.0f,launchReverseElapsed_/kLaunchReverseDuration);
            const float reverseEase=reverseProgress*reverseProgress*(3.0f-2.0f*reverseProgress);
            const float centerProgress=launchCenterDuration_>0.0f
                ?std::min(1.0f,launchAnimationElapsed_/launchCenterDuration_):1.0f;
            const float centerEase=centerProgress*centerProgress*(3.0f-2.0f*centerProgress);
            cx+=static_cast<int>(launchCenteringOffset_*centerEase);
            if(offset!=0){
                const float slide=std::max(0.0f,std::min(1.0f,animationTime/0.48f));
                const float slideEase=launchReversing_?1.0f-reverseEase:1.0f-std::pow(1.0f-slide,3.0f);
                cx+=static_cast<int>((offset<0?-1.0f:1.0f)*slideEase*500.0f);
            }
        }
        const int selectedSystemScale=cartridgeScaleFor(entry.console);
        const float scalePreference=selectedSystemScale==0?0.90f:(selectedSystemScale==2?1.10f:1.0f);
        const float artworkPreference=(accessibility_.largeArtwork?1.18f:1.0f)*
            (entry.kind==EntryKind::App && accessibility_.largeControls?1.08f:1.0f);
        const int cw=static_cast<int>((280-95*std::min(1.0f,distance))*scalePreference*artworkPreference);
        const int ch=static_cast<int>((350-120*std::min(1.0f,distance))*scalePreference*artworkPreference);
        if(startupAnimation_ && offset==0){
            const int startY=kDesignH+ch/2+8;
            cy=startY+static_cast<int>((254-startY)*startupCenterEase);
        }
        const SDL_Rect box{cx-cw/2,cy-ch/2,cw,ch};
        if(!(launchAwaitingConfirm_ && previewShowingDetails_ && offset==0)){
            if(!launchAnimation_ && !startupAnimation_)
                draw::artworkViewport({12,accessibleLayout?118:100,728,accessibleLayout?298:316});
            else draw::artworkViewport({0,0,0,0});
            const Uint8 shade=accessibility_.dimming && offset!=0?static_cast<Uint8>(255-accessibility_.dimming*30):255;
            if(entry.artwork)SDL_SetTextureAlphaMod(entry.artwork,shade);
            if(entry.kind==EntryKind::App)draw::appIcon(renderer_,box,entry.artwork,entry.variant,entry.onionMainUi,distance<0.5f);
            else if(launchAnimation_ && offset==0)drawLaunchingCartridge(entry);
            else draw::cartridge(renderer_,box,entry.system,entry.title,entry.variant,distance<0.5f,entry.artwork);
            if(entry.artwork)SDL_SetTextureAlphaMod(entry.artwork,255);
            draw::artworkViewport({0,0,0,0});
        }
    }
    for(size_t i=0;i<entries.size();++i){
        if(entries[i].artwork && std::find(visible.begin(),visible.end(),static_cast<int>(i))==visible.end()){
            SDL_DestroyTexture(entries[i].artwork);
            entries[i].artwork=nullptr;
            entries[i].artworkAttempted=false;
        }
    }

    const int chosen=wrapIndex(static_cast<int>(std::lround(position_)),entryCount);
    std::string title=entries[static_cast<size_t>(chosen)].title;
    const int titleHeight=titleSize_==0?26:(titleSize_==2?34:30);
    title=draw::fitText(title,titleHeight,kDesignW-40);
    int bottomTextOffset=0;
    int topTextOffset=0;
    if(launchAnimation_){
        const float animationTime=accessibility_.reducedMotion?0.50f:std::max(0.0f,launchAnimationElapsed_-launchCenterDuration_);
        const float topProgress=std::min(1.0f,animationTime/0.40f);
        const float bottomProgress=std::min(1.0f,animationTime/0.40f);
        const float reverseProgress=std::min(1.0f,launchReverseElapsed_/kLaunchReverseDuration);
        const float reverseEase=reverseProgress*reverseProgress*(3.0f-2.0f*reverseProgress);
        const float topEase=launchReversing_?1.0f-reverseEase:topProgress*topProgress*(3.0f-2.0f*topProgress);
        const float bottomEase=launchReversing_?1.0f-reverseEase:bottomProgress*bottomProgress*(3.0f-2.0f*bottomProgress);
        topTextOffset=-190*static_cast<int>(topEase*1000.0f)/1000;
        bottomTextOffset=190*static_cast<int>(bottomEase*1000.0f)/1000;
    }else if(startupAnimation_){
        const float textProgress=std::max(0.0f,std::min(1.0f,(startupAnimationElapsed_-0.82f)/0.36f));
        const float textEase=textProgress*textProgress*(3.0f-2.0f*textProgress);
        topTextOffset=-190*static_cast<int>((1.0f-textEase)*1000.0f)/1000;
        bottomTextOffset=190*static_cast<int>((1.0f-textEase)*1000.0f)/1000;
    }
    const bool invertBrowserText=invertTextColour_ && overlay_==Overlay::None;
    const auto browserTextColor=[invertBrowserText](SDL_Color color){
        if(!invertBrowserText) return color;
        const unsigned luminance=(static_cast<unsigned>(color.r)*30u+
                                  static_cast<unsigned>(color.g)*59u+
                                  static_cast<unsigned>(color.b)*11u)/100u;
        if(luminance>=200u){
            color.r=18; color.g=22; color.b=30;
        }else if(luminance>=140u){
            color.r=42; color.g=48; color.b=58;
        }else{
            color.r=60; color.g=66; color.b=76;
        }
        return color;
    };
    if(!hideGameTitle_)
        draw::text(renderer_,(kDesignW-draw::textWidth(title,titleHeight))/2,428+bottomTextOffset,title,titleHeight,browserTextColor(white));
    if(showGameCount_ && !accessibility_.hideSecondary){
        const std::string gameCount=std::to_string(chosen+1)+" / "+std::to_string(entryCount);
        draw::text(renderer_,8,8+topTextOffset,gameCount,16,browserTextColor(muted));
    }
    if(showBatteryLevel_ && !accessibility_.hideSecondary){
        const std::string batteryLabel=batteryPercent_>=0?std::to_string(batteryPercent_)+"%":"--%";
        draw::text(renderer_,kDesignW-8-draw::textWidth(batteryLabel,16),8+topTextOffset,batteryLabel,16,browserTextColor(muted));
    }

    const Category* selectedCategory=currentCategory();
    const std::string listHeader=selectedCategory?selectedCategory->label:"";
    const int listHeaderHeight=26;
    draw::text(renderer_,(kDesignW-draw::textWidth(listHeader,listHeaderHeight))/2,(accessibleLayout?50:48)+topTextOffset,listHeader,listHeaderHeight,browserTextColor(white));
    const std::string systemHeader=entries[static_cast<size_t>(chosen)].system;
    const int systemHeaderHeight=18;
    // A normal console list already names its system as the main header. Keep
    // the secondary system label for named collections and Favourite, where
    // it adds context instead of repeating the same heading.
    if(!accessibility_.hideSecondary && lower(systemHeader)!=lower(listHeader))
        draw::text(renderer_,(kDesignW-draw::textWidth(systemHeader,systemHeaderHeight))/2,(accessibleLayout?88:75)+topTextOffset,systemHeader,systemHeaderHeight,browserTextColor(muted));
    const size_t upCategoryIndex=adjacentVisibleCategory(-1);
    const size_t downCategoryIndex=adjacentVisibleCategory(1);
    if(upCategoryIndex!=categoryIndex_ || downCategoryIndex!=categoryIndex_){
        const Category& upCategory=categories_[upCategoryIndex];
        const Category& downCategory=categories_[downCategoryIndex];
        const SDL_Color subheader=browserTextColor(SDL_Color{145,148,154,255});
        const std::string upLabel=std::string(accessibility::buttonName(accessibility_.bindings[accessibility::Up]))+": "+shortLabel(upCategory.label,32);
        const int upW=draw::textWidth(upLabel,16);
        arrow(renderer_,kDesignW/2,8+topTextOffset,true,subheader);
        draw::text(renderer_,(kDesignW-upW)/2,22+topTextOffset,upLabel,16,subheader);
        const std::string downLabel=std::string(accessibility::buttonName(accessibility_.bindings[accessibility::Down]))+": "+shortLabel(downCategory.label,31);
        const int downW=draw::textWidth(downLabel,16);
        draw::text(renderer_,(kDesignW-downW)/2,477+bottomTextOffset,downLabel,16,subheader);
        arrow(renderer_,kDesignW/2,501+bottomTextOffset,false,subheader);
    }
    if(showControlHints_){
        bool letterJumpAvailable=selectedCategory && !selectedCategory->systemId.empty() &&
            selectedCategory->entries.size()>1;
        if(letterJumpAvailable){
            const char firstGroup=titleInitialGroup(selectedCategory->entries.front().title);
            letterJumpAvailable=std::any_of(selectedCategory->entries.begin()+1,selectedCategory->entries.end(),
                [firstGroup](const Game& game){return titleInitialGroup(game.title)!=firstGroup;});
        }
        const std::string hints=controlHints((letterJumpAvailable?"L/R LETTER   ":"")+std::string("UP/DOWN LIST   LEFT/RIGHT GAME   A OPEN   X REFRESH   SELECT OPTIONS   Y SEARCH"));
        if(accessibility_.textSize==2){
            const size_t middle=hints.find("   ",hints.size()/2);
            const std::string first=hints.substr(0,middle),second=middle==std::string::npos?"":hints.substr(middle+3);
            draw::text(renderer_,(kDesignW-draw::textWidth(first,15))/2,512+bottomTextOffset,first,15,browserTextColor(SDL_Color{150,154,162,255}));
            draw::text(renderer_,(kDesignW-draw::textWidth(second,15))/2,536+bottomTextOffset,second,15,browserTextColor(SDL_Color{150,154,162,255}));
        }else{
            int hintSize=15;
            while(hintSize>11 && draw::textWidth(hints,hintSize)>kDesignW-12) --hintSize;
            draw::text(renderer_,(kDesignW-draw::textWidth(hints,hintSize))/2,532+bottomTextOffset,hints,hintSize,browserTextColor(SDL_Color{150,154,162,255}));
        }
    }
    if(launchAnimation_ && launchAwaitingConfirm_){
        const Game* preview=launchingGame();
        if(preview){
            std::string info=launchArtworkLoading_?"LOADING IMAGES":"NO ARTWORK VARIANTS";
            if(!launchArtworkLoading_ && previewShowingDetails_){
                info="GAME DETAILS  "+std::to_string(preview->artVariants.size()+1)+" / "+
                     std::to_string(preview->artVariants.size()+1);
            }else if(!launchArtworkLoading_ && !preview->artVariants.empty()){
                const std::string artworkLabel=shortLabel(filenameWithoutExtension(preview->artPath),28);
                info="IMAGE "+std::to_string(preview->artSelection+1)+" / "+
                     std::to_string(preview->artVariants.size())+"   "+artworkLabel;
            }
            if(!launchArtworkLoading_ && previewShowingDetails_) drawPreviewGameDetails(*preview);
            if(!accessibility_.hideSecondary || launchArtworkLoading_)
                draw::text(renderer_,(kDesignW-draw::textWidth(info,15))/2,469,info,15,browserTextColor(muted));
            const std::string hints=controlHints("LEFT/RIGHT BROWSE   A LAUNCH   B BACK");
            draw::text(renderer_,(kDesignW-draw::textWidth(hints,15))/2,528,hints,15,browserTextColor(SDL_Color{150,154,162,255}));
        }
    }

    if(overlay_!=Overlay::None){
        SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
        fillRect(renderer_,SDL_Rect{58,85,636,overlay_==Overlay::Settings && accessibility_.textSize==2?418:390},{4,7,15,238});
        outlineRect(renderer_,SDL_Rect{58,85,636,overlay_==Overlay::Settings && accessibility_.textSize==2?418:390},{70,82,100,255});
        const SDL_Color accent{135,205,255,255};
        if(overlay_==Overlay::SelectMenu || overlay_==Overlay::AddToList){
            const bool listScreen=overlay_==Overlay::AddToList;
            const std::string heading=listScreen?"ADD TO LIST":"GAME OPTIONS";
            draw::text(renderer_,(kDesignW-draw::textWidth(heading,27))/2,105,heading,27,white);
            const std::vector<std::string>& options=listScreen?listOptions_:menuOptions_;
            if(options.empty()){
                const std::string none="NO LISTS FOUND";
                draw::text(renderer_,(kDesignW-draw::textWidth(none,23))/2,245,none,23,muted);
            }else{
                const int rowHeight=accessibility_.largeControls?48:std::max(36,draw::textHeight(21)+12);
                const int visibleRows=std::min(265/rowHeight,static_cast<int>(options.size()));
                int start=std::max(0,overlaySelection_-visibleRows+1);
                for(int row=0;row<visibleRows;++row){
                    const int index=start+row;
                    const int y=158+row*rowHeight;
                    if(index==overlaySelection_) fillRect(renderer_,SDL_Rect{90,y-4,572,rowHeight-4},{32,50,72,255});
                    const std::string item=draw::fitText((index==overlaySelection_?"> ":"  ")+options[static_cast<size_t>(index)],21,528);
                    draw::text(renderer_,112,y,item,21,index==overlaySelection_?accent:white);
                }
            }
            const std::string footer=controlHints("UP/DOWN CHOOSE   A SELECT   B BACK");
            draw::text(renderer_,(kDesignW-draw::textWidth(footer,15))/2,434,footer,15,muted);
        }else if(overlay_==Overlay::Settings){
            std::vector<std::string> names{
                "Animation speed","Game title size","Show game counter","Show battery level",
                "Show control hints","Hide game title","Carousel spacing","Startup animation",
                "Background","Invert text colour","Text background","Restore browser position","Reset settings"
            };
            std::vector<std::string> values{
                animationSpeed_==0?"SLOW":(animationSpeed_==2?"FAST":"NORMAL"),
                titleSize_==0?"SMALL":(titleSize_==2?"LARGE":"NORMAL"),
                showGameCount_?"ON":"OFF",showBatteryLevel_?"ON":"OFF",
                showControlHints_?"ON":"OFF",hideGameTitle_?"ON":"OFF",
                carouselSpacing_==0?"COMPACT":(carouselSpacing_==2?"WIDE":"NORMAL"),
                startupAnimationEnabled_?"ON":"OFF",
                backgroundSelection_>=0 && backgroundSelection_<static_cast<int>(backgroundChoices_.size())
                    ?backgroundChoices_[static_cast<size_t>(backgroundSelection_)].label:"NONE",
                invertTextColour_?"ON":"OFF",textBackground_?"ON":"OFF",
                restoreBrowserPosition_?"ON":"OFF",
                "RESTORE DEFAULTS"
            };
            // Keep per-system sizing together at the end so that systems
            // added by OnionOS do not push the everyday options downscreen.
            for(const SystemInfo& system:systems_){
                names.push_back(system.label+" cartridge size");
                const int setting=cartridgeScaleFor(system.id);
                values.push_back(setting==0?"SMALL":(setting==2?"LARGE":"NORMAL"));
                names.push_back(system.label+" visibility");
                values.push_back(systemHiddenFor(system.id)?"HIDDEN":"SHOWN");
            }
            draw::text(renderer_,(kDesignW-draw::textWidth("CARTOS SETTINGS",24))/2,100,"CARTOS SETTINGS",24,white);
            const int rowHeight=accessibility_.largeControls?46:34;
            const int visibleRows=accessibility_.largeControls?6:8;
            if(settingsSelection_<settingsScrollOffset_)settingsScrollOffset_=settingsSelection_;
            if(settingsSelection_>=settingsScrollOffset_+visibleRows)settingsScrollOffset_=settingsSelection_-visibleRows+1;
            const int first=settingsScrollOffset_;
            const int last=std::min(static_cast<int>(names.size()),first+visibleRows);
            for(int i=first;i<last;++i){
                const int row=i-first;
                const int y=139+row*rowHeight;
                if(i==settingsSelection_) fillRect(renderer_,SDL_Rect{82,y-2,588,rowHeight-4},{32,50,72,255});
                const std::string value=draw::fitText(values[static_cast<size_t>(i)],16,190);
                const int valueWidth=draw::textWidth(value,16);
                const std::string name=draw::fitText((i==settingsSelection_?"> ":"  ")+names[static_cast<size_t>(i)],16,530-valueWidth);
                draw::text(renderer_,94,y+4,name,16,i==settingsSelection_?accent:white);
                draw::text(renderer_,650-valueWidth,y+4,value,16,i==settingsSelection_?accent:muted);
            }
            const std::string footer=controlHints("UP/DOWN SCROLL   LEFT/RIGHT CHANGE   A SELECT   X REFRESH   B CLOSE");
            if(accessibility_.textSize==2){
                const size_t middle=footer.find("   ",footer.size()/2);
                const std::string first=footer.substr(0,middle),second=middle==std::string::npos?"":footer.substr(middle+3);
                draw::text(renderer_,(kDesignW-draw::textWidth(first,14))/2,424,first,14,muted);
                draw::text(renderer_,(kDesignW-draw::textWidth(second,14))/2,447,second,14,muted);
            }else draw::text(renderer_,(kDesignW-draw::textWidth(footer,14))/2,435,footer,14,muted);
            const std::string version="CARTOS v" CARTOS_VERSION;
            draw::text(renderer_,(kDesignW-draw::textWidth(version,13))/2,accessibility_.textSize==2?477:456,version,13,muted);
        }else if(overlay_==Overlay::CreateList){
            const std::string heading="CREATE NEW LIST";
            draw::text(renderer_,(kDesignW-draw::textWidth(heading,27))/2,105,heading,27,white);
            const std::string name=newListName_.empty()?"TYPE A NAME":"NAME: "+newListName_;
            draw::text(renderer_,(kDesignW-draw::textWidth(name,23))/2,153,name,23,accent);
            for(int row=0;row<4;++row){
                for(int col=0;col<10;++col){
                    const int index=row*10+col;
                    const SDL_Rect keyRect{116+col*53,204+row*42,45,accessibility_.largeControls?38:32};
                    if(index==virtualKey_) fillRect(renderer_,keyRect,{41,70,98,255});
                    outlineRect(renderer_,keyRect,index==virtualKey_?accent:SDL_Color{55,63,76,255});
                    const char letter=searchKeyboardCharacter(index);
                    const std::string caption=letter==' '?"SPC":std::string(1,letter);
                    draw::text(renderer_,keyRect.x+(keyRect.w-draw::textWidth(caption,17))/2,keyRect.y+7,caption,17,index==virtualKey_?accent:white);
                }
            }
            const std::string footer=controlHints("A ADD   X BACKSPACE   START CREATE   B CANCEL");
            draw::text(renderer_,(kDesignW-draw::textWidth(footer,15))/2,425,footer,15,muted);
        }else if(overlay_==Overlay::DeleteConfirm){
            const Category* selected=currentCategory();
            const std::string title=selected && !selected->entries.empty()?selected->entries[static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(selected->entries.size())))].title:"this game";
            draw::text(renderer_,(kDesignW-draw::textWidth("DELETE ROM?",28))/2,135,"DELETE ROM?",28,white);
            const std::string prompt=shortLabel(title,44);
            draw::text(renderer_,(kDesignW-draw::textWidth(prompt,19))/2,196,prompt,19,accent);
            draw::text(renderer_,(kDesignW-draw::textWidth("This removes the ROM only. Saves are kept.",17))/2,238,"This removes the ROM only. Saves are kept.",17,muted);
            const std::array<std::string,2> choices{{"DELETE GAME","CANCEL"}};
            for(int i=0;i<2;++i){
                const SDL_Rect row{202,300+i*48,348,38};
                if(i==overlaySelection_) fillRect(renderer_,row,{32,50,72,255});
                draw::text(renderer_,(kDesignW-draw::textWidth(choices[static_cast<size_t>(i)],21))/2,row.y+6,choices[static_cast<size_t>(i)],21,i==overlaySelection_?accent:white);
            }
            draw::text(renderer_,(kDesignW-draw::textWidth(controlHints("UP/DOWN CHOOSE   A CONFIRM   B CANCEL"),15))/2,420,controlHints("UP/DOWN CHOOSE   A CONFIRM   B CANCEL"),15,muted);
        }else if(overlay_==Overlay::DeleteListConfirm){
            const std::string heading="DELETE LIST?";
            draw::text(renderer_,(kDesignW-draw::textWidth(heading,28))/2,135,heading,28,white);
            const std::string label=currentCategory()?currentCategory()->label:"";
            draw::text(renderer_,(kDesignW-draw::textWidth(shortLabel(label,36),20))/2,193,shortLabel(label,36),20,accent);
            const std::string note="Games stay on the SD card.";
            draw::text(renderer_,(kDesignW-draw::textWidth(note,17))/2,231,note,17,muted);
            const std::array<std::string,2> choices{{"DELETE LIST","CANCEL"}};
            for(int i=0;i<2;++i){
                const SDL_Rect row{202,292+i*48,348,38};
                if(i==overlaySelection_) fillRect(renderer_,row,{32,50,72,255});
                draw::text(renderer_,(kDesignW-draw::textWidth(choices[static_cast<size_t>(i)],21))/2,row.y+6,choices[static_cast<size_t>(i)],21,i==overlaySelection_?accent:white);
            }
            const std::string footer=controlHints("UP/DOWN CHOOSE   A CONFIRM   B CANCEL");
            draw::text(renderer_,(kDesignW-draw::textWidth(footer,15))/2,420,footer,15,muted);
        }else if(overlay_==Overlay::GameInfo){
            const Category* selected=currentCategory();
            if(selected && !selected->entries.empty()){
                const Game& game=selected->entries[static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(selected->entries.size())))];
                draw::text(renderer_,(kDesignW-draw::textWidth("GAME DETAILS",27))/2,112,"GAME DETAILS",27,white);
                draw::text(renderer_,92,156,"TITLE",16,muted);
                draw::text(renderer_,92,180,draw::fitText(game.title,20,568),20,white);
                draw::text(renderer_,92,229,"SYSTEM",16,muted);
                draw::text(renderer_,92,253,game.system,20,white);
                draw::text(renderer_,92,302,"PLAY TIME",16,muted);
                const bool activityTrackerInstalled=access((sdRoot_+"/App/PlayActivity/launch.sh").c_str(),R_OK)==0;
                const long long playedSeconds=activityTrackerInstalled?playtimeSecondsFor(game):-1;
                std::string playtime="Install Activity Tracker to see your playtime";
                if(activityTrackerInstalled) playtime="No playtime";
                if(activityTrackerInstalled && playedSeconds>=3600){
                    playtime=std::to_string(playedSeconds/3600)+"h "+std::to_string((playedSeconds%3600)/60)+"m";
                }else if(activityTrackerInstalled && playedSeconds>=60){
                    playtime=std::to_string(playedSeconds/60)+"m";
                }else if(activityTrackerInstalled && playedSeconds>=0){
                    playtime=std::to_string(playedSeconds)+"s";
                }
                const int playtimeSize=activityTrackerInstalled?20:16;
                draw::text(renderer_,92,326,playtime,playtimeSize,activityTrackerInstalled?white:muted);
                draw::text(renderer_,92,376,"ROM FILE",16,muted);
                draw::text(renderer_,92,400,shortLabel(game.romPath,62),16,white);
                const std::string footer=controlHints("B CLOSE");
                draw::text(renderer_,(kDesignW-draw::textWidth(footer,16))/2,448,footer,16,muted);
            }
        }
        SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
    }
    if(!statusMessage_.empty() && SDL_GetTicks64()<statusUntil_){
        const int width=draw::textWidth(statusMessage_,16);
        fillRect(renderer_,SDL_Rect{(kDesignW-width)/2-8,487,width+16,25},{15,24,36,255});
        draw::text(renderer_,(kDesignW-width)/2,490,statusMessage_,16,{160,216,170,255});
    }
    presentFrame();
}

void Launcher::renderSearch(){
    renderBackground();
    if(launchAnimation_){
        const Game* selected=nullptr;
        if(!selectedInSearch(selected)){
            presentFrame();
            return;
        }
        const float animationTime=accessibility_.reducedMotion?0.50f:std::max(0.0f,launchAnimationElapsed_-launchCenterDuration_);
        const float textProgress=std::min(1.0f,animationTime/0.40f);
        const float textEase=textProgress*textProgress*(3.0f-2.0f*textProgress);
        const float reverseProgress=std::min(1.0f,launchReverseElapsed_/kLaunchReverseDuration);
        const float reverseEase=reverseProgress*reverseProgress*(3.0f-2.0f*reverseProgress);
        const float visibleTextEase=launchReversing_?1.0f-reverseEase:textEase;
        const int topOffset=-190*static_cast<int>(visibleTextEase*1000.0f)/1000;
        const int centerX=kDesignW/2;
        const int count=static_cast<int>(searchResults_.size());
        for(int offset=-1;offset<=1;++offset){
            if(count==0) break;
            if(count==1 && offset!=0) continue;
            const int index=wrapIndex(searchResultSelection_+offset,count);
            const auto ref=searchResults_[static_cast<size_t>(index)];
            Game& game=categories_[ref.first].entries[ref.second];
            if(!game.artwork && !game.artPath.empty()) game.artwork=artwork::loadPng(renderer_,game.artPath);
            if(offset==0){
                if(!(launchAwaitingConfirm_ && previewShowingDetails_))drawLaunchingCartridge(game);
            }else{
                const float slide=std::max(0.0f,std::min(1.0f,animationTime/0.48f));
                const float reverseProgress=std::min(1.0f,launchReverseElapsed_/kLaunchReverseDuration);
                const float reverseEase=reverseProgress*reverseProgress*(3.0f-2.0f*reverseProgress);
                const float slideEase=launchReversing_?1.0f-reverseEase:1.0f-std::pow(1.0f-slide,3.0f);
                const int width=112;
                const int height=140;
                const int x=centerX+offset*176+static_cast<int>(offset*slideEase*500.0f)-width/2;
                const SDL_Rect box{x,274-height/2,width,height};
                draw::cartridge(renderer_,box,game.system,game.title,game.variant,false,game.artwork);
            }
        }
        const std::string heading="SEARCH GAMES";
        draw::text(renderer_,(kDesignW-draw::textWidth(heading,27))/2,45+topOffset,heading,27,white);
        const std::string query="\""+shortLabel(searchQuery_,44)+"\"";
        draw::text(renderer_,(kDesignW-draw::textWidth(query,22))/2,86+topOffset,query,22,{145,205,255,255});
        // The enlarged-cartridge view is intentionally artwork-only. The
        // title and system/count labels are omitted once the launch animation
        // starts instead of lingering at the bottom edge.
        if(launchAwaitingConfirm_){
            std::string info=launchArtworkLoading_?"LOADING IMAGES":"NO ARTWORK VARIANTS";
            if(!launchArtworkLoading_ && previewShowingDetails_){
                info="GAME DETAILS  "+std::to_string(selected->artVariants.size()+1)+" / "+
                     std::to_string(selected->artVariants.size()+1);
                drawPreviewGameDetails(*selected);
            }else if(!launchArtworkLoading_ && !selected->artVariants.empty()){
                const std::string artworkLabel=shortLabel(filenameWithoutExtension(selected->artPath),28);
                info="IMAGE "+std::to_string(selected->artSelection+1)+" / "+
                     std::to_string(selected->artVariants.size())+"   "+artworkLabel;
            }
            if(!accessibility_.hideSecondary || launchArtworkLoading_)
                draw::text(renderer_,(kDesignW-draw::textWidth(info,15))/2,469,info,15,muted);
            const std::string hints=controlHints("LEFT/RIGHT BROWSE   A LAUNCH   B BACK");
            draw::text(renderer_,(kDesignW-draw::textWidth(hints,15))/2,528,hints,15,muted);
        }
        presentFrame();
        return;
    }
    const std::string heading="SEARCH GAMES";
    draw::text(renderer_,(kDesignW-draw::textWidth(heading,27))/2,45,heading,27,white);
    const std::string query=searchQuery_.empty()?"TYPE TO SEARCH":"\""+shortLabel(searchQuery_,44)+"\"";
    draw::text(renderer_,(kDesignW-draw::textWidth(query,22))/2,86,query,22,{145,205,255,255});
    const Game* selected=nullptr;
    const bool hasSelected=selectedInSearch(selected);
    if(hasSelected){
        const int count=static_cast<int>(searchResults_.size());
        for(int offset=-1;offset<=1;++offset){
            if(count==0) break;
            if(count==1 && offset!=0) continue;
            const int index=wrapIndex(searchResultSelection_+offset,count);
            const auto neighborRef=searchResults_[static_cast<size_t>(index)];
            Game& game=categories_[neighborRef.first].entries[neighborRef.second];
            if(!game.artwork && !game.artPath.empty()) game.artwork=artwork::loadPng(renderer_,game.artPath);
            const float factor=accessibility_.largeArtwork?1.18f:1.0f;
            const int width=static_cast<int>((offset==0?220:96)*factor);
            const int height=static_cast<int>((offset==0?275:120)*factor);
            const int x=kDesignW/2+offset*(offset==0?0:150)-width/2;
            const int carouselCenterY=searchResultsFocused_?238:226;
            const SDL_Rect box{x,carouselCenterY-height/2,width,height};
            // Search already draws the ROM title beneath the carousel. Keep
            // fallback shells free of a second, overlapping copy of the name.
            draw::artworkViewport(searchResultsFocused_?SDL_Rect{20,126,712,250}:SDL_Rect{20,126,712,180});
            draw::cartridge(renderer_,box,game.system,"",game.variant,offset==0,game.artwork);
            draw::artworkViewport({0,0,0,0});
        }
        const std::string title=shortLabel(selected->title,52);
        const int titleY=searchResultsFocused_?397:316;
        const int subtitleY=searchResultsFocused_?428:360;
        draw::text(renderer_,(kDesignW-draw::textWidth(title,22))/2,titleY,title,22,white);
        const std::string system=selected->system+"   "+std::to_string(searchResultSelection_+1)+" / "+std::to_string(searchResults_.size());
        if(!accessibility_.hideSecondary)draw::text(renderer_,(kDesignW-draw::textWidth(system,16))/2,subtitleY,system,16,muted);
    }else{
        const std::string none=searchQuery_.empty()?"START TYPING TO FIND A GAME":"NO MATCHES";
        draw::text(renderer_,(kDesignW-draw::textWidth(none,20))/2,245,none,20,muted);
    }
    if(!searchResultsFocused_){
        fillRect(renderer_,{101,388,560,130},{0,0,0,255});
        draw::suppressTextBackground(true);
        for(int row=0;row<4;++row){
            for(int col=0;col<10;++col){
                const int index=row*10+col;
                const SDL_Rect keyRect{111+col*55,398+row*(accessibility_.largeControls?29:28),47,accessibility_.largeControls?26:23};
                if(index==virtualKey_)fillRect(renderer_,keyRect,{39,64,90,255});
                outlineRect(renderer_,keyRect,index==virtualKey_?SDL_Color{145,205,255,255}:SDL_Color{54,62,75,255});
                const char value=searchKeyboardCharacter(index);
                const std::string label=draw::fitText(value==' '?(accessibility_.textSize==2?"SPC":"SPACE"):std::string(1,value),value==' '?12:15,43);
                const int labelSize=value==' '?12:15;
                draw::text(renderer_,keyRect.x+(keyRect.w-draw::textWidth(label,labelSize))/2,keyRect.y+4,label,labelSize,index==virtualKey_?SDL_Color{145,205,255,255}:white);
            }
        }
        const std::string footer=controlHints("A TYPE   B BACKSPACE   X REFRESH   START RESULTS   Y EXIT");
        draw::text(renderer_,(kDesignW-draw::textWidth(footer,14))/2,520,footer,14,muted);
        draw::suppressTextBackground(false);
    }else{
        const std::string footer=controlHints("LEFT/RIGHT BROWSE   A OPEN   B KEYBOARD   Y EXIT");
        draw::text(renderer_,(kDesignW-draw::textWidth(footer,14))/2,520,footer,14,muted);
    }
    presentFrame();
}

int Launcher::run(){
    if(!init()){shutdown();return 1;}
    Uint64 previous=SDL_GetPerformanceCounter();
    while(running_){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            bool skipStartup=e.type==SDL_KEYDOWN || e.type==SDL_CONTROLLERBUTTONDOWN ||
                             e.type==SDL_JOYBUTTONDOWN;
            if(e.type==SDL_JOYHATMOTION && e.jhat.value!=SDL_HAT_CENTERED)
                skipStartup=true;
            if(e.type==SDL_JOYAXISMOTION && (e.jaxis.value>8000 || e.jaxis.value< -8000))
                skipStartup=true;
            if(e.type==SDL_CONTROLLERAXISMOTION && (e.caxis.value>8000 || e.caxis.value< -8000))
                skipStartup=true;
            if(skipStartup) startupAnimation_=false;
            handle(e);
        }
        updateHeldScroll(SDL_GetTicks64());
        const Uint64 now=SDL_GetPerformanceCounter();
        float dt=static_cast<float>(now-previous)/static_cast<float>(SDL_GetPerformanceFrequency());
        previous=now; dt=std::min(dt,0.05f);
        if(startupAnimation_){
            startupAnimationElapsed_+=dt;
            if(startupAnimationElapsed_>=1.20f) startupAnimation_=false;
        }
        if(launchAnimation_ && !launchAwaitingConfirm_){
            const float speed=animationSpeed_==0?0.75f:(animationSpeed_==2?1.35f:1.0f);
            if(launchReversing_){
                launchReverseElapsed_+=dt*speed;
                if(launchReverseElapsed_>=kLaunchReverseDuration){
                    launchReverseElapsed_=kLaunchReverseDuration;
                    launchAnimation_=false;
                    launchReversing_=false;
                    launchStepTwoConfirmed_=false;
                    resetPreviewArtwork();
                }
            }else{
                if(accessibility_.reducedMotion && launchStepTwoConfirmed_)launchAnimationElapsed_=kLaunchAnimationDuration;
                else launchAnimationElapsed_+=dt*speed;
            }
            const float stepOneDuration=launchCenterDuration_+0.50f;
            if(launchAnimation_ && !launchReversing_ && !launchStepTwoConfirmed_ && launchAnimationElapsed_>=stepOneDuration){
                launchAnimationElapsed_=stepOneDuration;
                launchAwaitingConfirm_=true;
                // Defer artwork discovery until the enlarged preview is on
                // screen, then keep its loading message visible while scanning.
                launchArtworkLoading_=true;
            }
        }
        render(dt);
        if(launchArtworkLoading_ && launchAwaitingConfirm_){
            if(Game* game=launchingGame()){
                loadArtworkVariants(*game);
                resetPreviewArtwork();
            }
            launchArtworkLoading_=false;
        }
        if(!rendererVsync_) SDL_Delay(16);
        if(launchAnimation_ && !launchAwaitingConfirm_ && !launchReversing_ && launchAnimationElapsed_>=kLaunchAnimationDuration){
            launchAnimation_=false;
            if(launchAnimationCategory_<categories_.size() && launchAnimationEntry_<categories_[launchAnimationCategory_].entries.size())
                launchGame(categories_[launchAnimationCategory_].entries[launchAnimationEntry_]);
        }
    }
    shutdown();
    return 0;
}
