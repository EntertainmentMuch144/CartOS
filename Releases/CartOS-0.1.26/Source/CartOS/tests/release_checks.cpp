// Host-side regression checks; no ROM or OnionOS process is executed.
#include <SDL.h>
#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <map>
#define private public
#include "launcher.hpp"
#undef private
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
static void touch(const fs::path& path) { fs::create_directories(path.parent_path()); std::ofstream(path).put('x'); }
static SDL_Event button(Uint8 value) { SDL_Event e{}; e.type=SDL_CONTROLLERBUTTONDOWN; e.cbutton.button=value; return e; }
static std::vector<std::string> names(const std::vector<std::string>& paths) {
    std::vector<std::string> result;
    for(const auto& p:paths) result.push_back(fs::path(p).filename().string());
    return result;
}
static std::string contents(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
static void write(const fs::path& path,const std::string& data) { std::ofstream(path,std::ios::binary)<<data; }
int main() {
    char pattern[]="/tmp/cartos-release-check-XXXXXX";
    const char* temp=mkdtemp(pattern); assert(temp);
    const fs::path base=temp;
    const fs::path root=base/"GB/Imgs/CartOS", cache=base/"cache/gb.idx";
    touch(root/"Pokemon.png"); touch(root/"Pokemon - Boxart.png"); touch(root/"Pokemon - Gameplay.PNG");
    touch(root/"Pokemon - .png"); // Empty label was never a view.
    touch(root/"Pokemon Red.png"); touch(root/".hidden.png"); touch(root/"Pokemon.jpg");
    touch(root/"Pokemon.gb/Back.png"); touch(root/"Pokemon.gb/.ignore.png");
    touch(root/"A/Folder game - Backcover.png"); touch(root/"A/Folder game.zip/Gameplay.png");
    touch(root/"GB/Multi - Boxart.png");
    for(int i=0;i<5000;++i) touch(root/("Unrelated "+std::to_string(i)+" - Boxart.png"));
    artwork::Index cold;
    auto at=Clock::now(); assert(cold.open(root.string(),cache.string()));
    const double coldMs=std::chrono::duration<double,std::milli>(Clock::now()-at).count();
    assert(!cold.loadedFromDisk()); assert(fs::exists(cache));
    const auto expected=cold.images("Pokemon.gb");
    assert((names(expected)==std::vector<std::string>{"Pokemon.png","Back.png","Pokemon - Boxart.png","Pokemon - Gameplay.PNG"}));
    assert(cold.images("No match.gba").empty());
    assert(cold.images("../Pokemon.gb").empty());
    assert(cold.images("/Pokemon.gb").empty());
    assert(names(cold.images("A/Folder game.zip")).size()==2);
    assert(names(cold.images("GB/Multi.gb"))==std::vector<std::string>{"Multi - Boxart.png"});
    // No directory scanning or per-PNG stat on a cache hit: the artwork folder
    // is temporarily unavailable, but the persisted lookup must still load.
    fs::rename(root,root.string()+".offline");
    artwork::Index warm;
    at=Clock::now(); assert(warm.open(root.string(),cache.string()));
    const double warmMs=std::chrono::duration<double,std::milli>(Clock::now()-at).count();
    assert(warm.loadedFromDisk()); assert(warm.images("Pokemon.gb")==expected);
    fs::rename(root.string()+".offline",root);
    // A new session uses the saved list until X's forced rebuild.
    touch(root/"Pokemon - New.png"); fs::remove(root/"Pokemon - Boxart.png");
    artwork::Index stale; assert(stale.open(root.string(),cache.string()));
    assert(stale.loadedFromDisk()); assert(stale.images("Pokemon.gb")==expected);
    assert(stale.open(root.string(),cache.string(),true)); assert(!stale.loadedFromDisk());
    const auto refreshed=names(stale.images("Pokemon.gb"));
    assert(std::find(refreshed.begin(),refreshed.end(),"Pokemon - New.png")!=refreshed.end());
    assert(std::find(refreshed.begin(),refreshed.end(),"Pokemon - Boxart.png")==refreshed.end());
    const std::string good=contents(cache);
    for(int kind=0;kind<4;++kind) {
        std::string corrupt=good;
        if(kind==0) corrupt.clear();
        if(kind==1) corrupt.resize(corrupt.size()/2);
        if(kind==2) corrupt[corrupt.size()/2]^=1;
        if(kind==3) corrupt[0]='!';
        write(cache,corrupt);
        artwork::Index recovered; assert(recovered.open(root.string(),cache.string()));
        assert(!recovered.loadedFromDisk()); assert(names(recovered.images("Pokemon.gb"))==refreshed);
    }
    artwork::Index anotherSystem;
    touch(base/"GBA/Imgs/CartOS/Pokemon - Other.png");
    assert(anotherSystem.open((base/"GBA/Imgs/CartOS").string(),(base/"cache/gba.idx").string()));
    assert(names(anotherSystem.images("Pokemon.gba"))==std::vector<std::string>{"Pokemon - Other.png"});
    artwork::Index changedRoot;
    assert(changedRoot.open((base/"GBA/Imgs/CartOS").string(),cache.string()));
    assert(!changedRoot.loadedFromDisk());
    artwork::Index missing; assert(missing.open((base/"Missing").string(),(base/"cache/missing.idx").string()));
    assert(missing.images("Game.zip").empty());
    touch(base/"not-a-directory");
    artwork::Index noWrite; assert(noWrite.open(root.string(),(base/"not-a-directory/index").string()));
    // Check the real launcher input handler, including contextual START.
    Launcher launcher;
    Game game; game.title="Pokemon"; game.romPath=(base/"GB/Pokemon.gb").string();
    game.defaultArtPath=(base/"GB/Imgs/Pokemon.png").string(); game.artPath=game.defaultArtPath;
    touch(game.romPath); touch(game.artPath);
    Category category; category.entries.push_back(game); launcher.categories_.push_back(category);
    launcher.sdRoot_=base.string();
    SystemInfo system; system.id="GB"; system.romPaths={(base/"GB").string()}; system.artworkPath=(base/"GB/Imgs").string();
    launcher.systems_.push_back(system);
    const fs::path prior=fs::current_path(); fs::current_path(base);
    launcher.handle(button(SDL_CONTROLLER_BUTTON_A));
    assert(launcher.launchAnimation_ && !launcher.launchStepTwoConfirmed_);
    const auto normalDuration=launcher.launchCenterDuration_;
    launcher.launchAwaitingConfirm_=true;
    launcher.handle(button(SDL_CONTROLLER_BUTTON_A));
    assert(launcher.launchStepTwoConfirmed_ && !launcher.launchAwaitingConfirm_);
    launcher.launchAnimation_=false; launcher.position_=0.25f;
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START));
    assert(launcher.launchAnimation_ && launcher.launchStepTwoConfirmed_);
    assert(launcher.launchCenterDuration_==normalDuration && launcher.launchAnimationElapsed_==0.0f);
    assert(launcher.launchingGame()->romPath==game.romPath && launcher.artworkIndexes_.empty());
    launcher.launchAnimation_=false; launcher.overlay_=Launcher::Overlay::Search;
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START));
    assert(launcher.searchResultsFocused_ && !launcher.launchAnimation_);
    launcher.overlay_=Launcher::Overlay::Settings;
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START)); assert(!launcher.launchAnimation_);
    launcher.overlay_=Launcher::Overlay::SelectMenu;
    launcher.menuOptions_={"Game details"};
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START)); assert(!launcher.launchAnimation_);
    launcher.overlay_=Launcher::Overlay::None;
    launcher.categories_[0].entries[0].kind=EntryKind::App;
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START)); assert(!launcher.launchAnimation_);
    launcher.categories_[0].entries[0].kind=EntryKind::Game;
    launcher.categories_[0].entries[0].romPath="/missing/rom.zip";
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START)); assert(!launcher.launchAnimation_);
    launcher.categories_[0].entries.clear();
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START)); assert(!launcher.launchAnimation_);
    launcher.categories_[0].entries.push_back(game);
    // Each saved-list copy and a normalized OnionOS history path gets the same gallery.
    for(int list=0;list<4;++list) {
        Game copy=game;
        if(list==1) copy.romPath="/mnt/SDCARD/GB/../GB/Pokemon.gb";
        launcher.loadArtworkVariants(copy);
        assert(copy.artVariants.size()==refreshed.size()+1);
        assert(copy.artVariants.front()==game.defaultArtPath);
    }
    // Real launcher path derivation for a system with more than one ROM root.
    Launcher multi; multi.sdRoot_=base.string();
    SystemInfo multiSystem; multiSystem.id="MULTI";
    multiSystem.romPaths={(base/"GB").string(),(base/"GBA").string()};
    multiSystem.artworkPath=(base/"MultiImgs").string(); multi.systems_.push_back(multiSystem);
    touch(base/"MultiImgs/CartOS/GB/Pokemon - Front.png");
    Game multiGame=game; multi.loadArtworkVariants(multiGame);
    assert(multiGame.artVariants.size()==2);
    assert(fs::path(multiGame.artVariants.back()).filename()=="Pokemon - Front.png");
    // A held Menu shortcut must not become a quick launch.
    launcher.menuButtonHeld_=true;
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START)); assert(!launcher.launchAnimation_);
    launcher.menuButtonHeld_=false;
    Launcher restarted; restarted.sdRoot_=base.string(); restarted.systems_.push_back(system);
    Game copy=game; restarted.loadArtworkVariants(copy);
    assert(restarted.artworkIndexes_.at("GB").loadedFromDisk());
    // X's integration must discard textures/variants and force all active indexes
    // to rebuild; verify the real refresh path with a disposable OnionOS root
    // on the handheld, as discoverGames intentionally uses /mnt/SDCARD.
    fs::current_path(prior);
    std::cout<<"PASS artwork matching, 5,000-image library, disk reuse without scans, refresh, corruption recovery, systems, saved lists, A/START/context guards\n";
    std::cout<<"Host timing (not handheld): build "<<coldMs<<" ms, cached load "<<warmMs<<" ms, index "<<good.size()<<" bytes\n";
    fs::remove_all(base);
}
