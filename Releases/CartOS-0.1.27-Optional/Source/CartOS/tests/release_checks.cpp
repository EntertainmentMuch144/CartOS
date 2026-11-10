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
static SDL_Event released(Uint8 value) { SDL_Event e=button(value); e.type=SDL_CONTROLLERBUTTONUP; return e; }
static SDL_Event key(SDL_Keycode value,bool down=true,bool repeat=false) {
    SDL_Event e{}; e.type=down?SDL_KEYDOWN:SDL_KEYUP; e.key.keysym.sym=value; e.key.repeat=repeat; return e;
}
static SDL_Event rawButton(Uint8 value,bool down=true) {
    SDL_Event e{}; e.type=down?SDL_JOYBUTTONDOWN:SDL_JOYBUTTONUP; e.jbutton.button=value; return e;
}
static void checkSelectShortcuts(const fs::path& base) {
    Launcher input; input.sdRoot_=base.string();
    Category category; Game game; game.title="Options test"; category.entries.push_back(game);
    input.categories_.push_back(category);
    const auto press=button(SDL_CONTROLLER_BUTTON_BACK), release=released(SDL_CONTROLLER_BUTTON_BACK);
    input.handle(press); assert(input.overlay_==Launcher::Overlay::None);
    input.handle(release); assert(input.overlay_==Launcher::Overlay::SelectMenu);
    input.overlay_=Launcher::Overlay::None;
    input.handle(release); assert(input.overlay_==Launcher::Overlay::None);
    // Both trigger directions must cancel options, including neutral events
    // before a trigger press and releasing SELECT before the trigger.
    for(Uint8 trigger:{SDL_CONTROLLER_AXIS_TRIGGERLEFT,SDL_CONTROLLER_AXIS_TRIGGERRIGHT}) {
        input.handle(press);
        SDL_Event axis{}; axis.type=SDL_CONTROLLERAXISMOTION; axis.caxis.axis=trigger;
        input.handle(axis); assert(!input.selectButtonChord_);
        axis.caxis.value=32767; input.handle(axis); assert(input.selectButtonChord_);
        axis.caxis.value=0; input.handle(axis); input.handle(release);
        assert(input.overlay_==Launcher::Overlay::None && !input.launchAnimation_);
    }
    input.handle(press); input.handle(button(SDL_CONTROLLER_BUTTON_A)); input.handle(release);
    assert(input.overlay_==Launcher::Overlay::None && !input.launchAnimation_);
    input.handle(press); input.handle(button(SDL_CONTROLLER_BUTTON_START)); input.handle(release);
    assert(input.overlay_==Launcher::Overlay::Accessibility && !input.launchAnimation_);
    input.overlay_=Launcher::Overlay::None;
    input.handle(press); input.handle(button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)); input.handle(release);
    assert(input.position_==0.0f && input.target_==0.0f && !input.scrollHeld_[1]);
    assert(input.overlay_==Launcher::Overlay::None);
    // Normal controller navigation still resumes after a chord.
    input.handle(button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)); assert(input.scrollHeld_[1]);
    input.handle(released(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)); assert(!input.scrollHeld_[1]);
    for(SDL_Keycode select:{SDLK_RCTRL,SDLK_RSHIFT}) {
        input.handle(key(select)); input.handle(key(select,true,true));
        assert(input.overlay_==Launcher::Overlay::None);
        input.handle(key(select,false)); assert(input.overlay_==Launcher::Overlay::SelectMenu);
        input.overlay_=Launcher::Overlay::None;
        for(SDL_Keycode trigger:{SDLK_TAB,SDLK_BACKSPACE}) {
            input.handle(key(select)); input.handle(key(trigger)); input.handle(key(trigger,false));
            input.handle(key(select,false)); assert(input.overlay_==Launcher::Overlay::None);
            assert(input.running_ && !input.launchAnimation_);
        }
    }
    input.handle(rawButton(12)); assert(input.overlay_==Launcher::Overlay::None);
    input.handle(rawButton(12,false)); assert(input.overlay_==Launcher::Overlay::SelectMenu);
    input.overlay_=Launcher::Overlay::None;
    for(Uint8 trigger:{10,11}) {
        input.handle(rawButton(12)); input.handle(rawButton(trigger)); input.handle(rawButton(trigger,false));
        input.handle(rawButton(12,false)); assert(input.overlay_==Launcher::Overlay::None);
    }
    input.overlay_=Launcher::Overlay::Search;
    input.handle(press); assert(input.overlay_==Launcher::Overlay::None); // contextual back retained
    input.handle(release); assert(input.overlay_==Launcher::Overlay::None);
    input.overlay_=Launcher::Overlay::Settings;
    input.handle(press); input.handle(button(SDL_CONTROLLER_BUTTON_DPAD_DOWN)); input.handle(release);
    assert(input.overlay_==Launcher::Overlay::Settings && input.settingsSelection_==0);
    input.overlay_=Launcher::Overlay::None;
    input.launchAnimation_=true; input.launchAwaitingConfirm_=true;
    input.handle(press); input.handle(button(SDL_CONTROLLER_BUTTON_A)); input.handle(release);
    assert(input.launchAwaitingConfirm_ && !input.launchStepTwoConfirmed_);
    input.launchAnimation_=false; input.launchAwaitingConfirm_=false;
    input.handle(press);
    SDL_Event focus{}; focus.type=SDL_WINDOWEVENT; focus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    input.handle(focus); input.handle(release);
    assert(!input.selectButtonHeld_ && input.overlay_==Launcher::Overlay::None);
    input.handle(press); input.handle(release); assert(input.overlay_==Launcher::Overlay::SelectMenu);
    std::cout<<"PASS SELECT release, L2/R2 chords, keyboard/raw/controller paths, context and focus recovery\n";
}
static void checkBrowserResume(const fs::path& base) {
    Game first,second; first.title="First"; first.romPath=(base/"GB/First.gb").string();
    second.title="Second"; second.romPath=(base/"GB/Second \"Special\".gb").string();
    auto fixture=[&]() {
        Launcher app; app.sdRoot_=base.string(); app.restoreBrowserPosition_=true;
        for(const std::string& key:{"system:GB","recent","favourite","collection:Main \"Test\""}) {
            Category category; category.orderKey=key;
            if(key=="system:GB") category.systemId="GB";
            category.entries={first,second}; app.categories_.push_back(category);
        }
        app.rememberedSelections_.assign(app.categories_.size(),0);
        return app;
    };
    for(size_t origin:{0u,1u,2u,3u}) {
        Launcher before=fixture(); const std::string key=before.categories_[origin].orderKey;
        assert(before.saveBrowserState(origin,second.romPath));
        assert(fs::exists(".browser-state") && !fs::exists(".browser-state.tmp"));
        Launcher after=fixture();
        std::reverse(after.categories_.begin(),after.categories_.end());
        size_t expected=0;
        for(size_t c=0;c<after.categories_.size();++c) {
            if(after.categories_[c].orderKey==key) {
                expected=c; after.categories_[c].entries.insert(after.categories_[c].entries.begin(),Game{});
            }
        }
        after.restoreBrowserState(true);
        assert(after.categoryIndex_==expected && after.position_==2.0f && after.target_==2.0f);
        assert(after.rememberedSelections_[expected]==2);
        assert(after.overlay_==Launcher::Overlay::None && !after.launchAnimation_);
        assert(!fs::exists(".browser-state"));
    }
    Launcher app=fixture();
    assert(app.saveBrowserState(0,second.romPath));
    app.restoreBrowserState(false);
    assert(app.categories_[app.categoryIndex_].orderKey=="recent" && app.position_==0.0f);
    assert(!fs::exists(".browser-state"));
    app.restoreBrowserState(true); assert(app.categories_[app.categoryIndex_].orderKey=="recent");
    // Missing list, missing game, and hidden system all fall back to Recent.
    for(int missing=0;missing<3;++missing) {
        Launcher after=fixture(); assert(after.saveBrowserState(0,second.romPath));
        if(missing==0) after.categories_[0].orderKey="system:NEW";
        if(missing==1) after.categories_[0].entries.pop_back();
        if(missing==2) after.hiddenSystemById_["GB"]=true;
        after.restoreBrowserState(true);
        assert(after.categories_[after.categoryIndex_].orderKey=="recent" && after.position_==0.0f);
    }
    // History aliases normalize to the same game after a resumed session.
    app=fixture(); assert(app.saveBrowserState(0,"/mnt/SDCARD/GB/../GB/First.gb"));
    app.restoreBrowserState(true); assert(app.categoryIndex_==0 && app.position_==0.0f);
    const std::vector<std::string> invalid={"", "Not a CartOS snapshot", "CartOSBrowser1\n\"system:GB\"\n\"unfinished", std::string(16385,'x')};
    for(const std::string& data:invalid) {
        std::ofstream(".browser-state",std::ios::binary)<<data;
        app=fixture(); app.restoreBrowserState(true);
        assert(app.categories_[app.categoryIndex_].orderKey=="recent" && !fs::exists(".browser-state"));
    }
    app=fixture(); assert(app.saveBrowserState(0,first.romPath));
    std::ofstream(".browser-state",std::ios::app)<<"unexpected trailing data";
    app.restoreBrowserState(true); assert(app.categories_[app.categoryIndex_].orderKey=="recent");
    assert(!app.saveBrowserState(999,first.romPath));
    assert(!app.saveBrowserState(0,""));
    fs::create_directory(".browser-state.tmp");
    assert(!app.saveBrowserState(0,first.romPath)); fs::remove(".browser-state.tmp");
    std::cout<<"PASS browser resume across system/Recent/Favourite/custom lists, reordering, cold starts, missing/hidden entries, corrupt state, write failure\n";
}
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
static void checkMixedRecent(const fs::path& base) {
    const fs::path root=base/"mixed-recent";
    const fs::path history=root/"Saves/CurrentProfile/lists/content_history.lpl";
    const fs::path native=root/"Roms/recentlist.json";
    const fs::path fileExplorer=root/"App/File Explorer";
    const fs::path terminal=root/"App/Terminal";
    for(const auto& path:{history,native,root/"Roms/GB/First.gb",root/"Roms/GB/Second.gb",
                         root/"Roms/GB/Standalone.gb",root/"Roms/GB/Third.gb",root/"Emu/GB/launch.sh",
                         fileExplorer/"launch.sh",fileExplorer/"icon.png",terminal/"launch.sh"})
        touch(path);
    fs::create_directories(root/"App/CartOS");
    write(fileExplorer/"config.json",R"({"label":"File Explorer","icon":"icon.png"})");
    write(terminal/"config.json",R"({"label":"Terminal","launch":"launch.sh"})");
    const std::string first=R"({"path":"/mnt/SDCARD/Roms/GB/First.gb"})" "\n";
    const std::string second=R"({"path":"/mnt/SDCARD/Roms/GB/Second.gb"})" "\n";
    const std::string alias=R"({"path":"/mnt/SDCARD/Emu/GB/../../Roms/GB/First.gb"})" "\n";
    const std::string third=R"({"path":"/mnt/SDCARD/Roms/GB/Third.gb"})" "\n";
    const std::string nativeData=R"({"rompath":"/mnt/SDCARD/Roms/GB/First.gb","label":"First"})" "\n";
    write(native,nativeData);
    write(history,first+alias);
    auto open=[&]() {
        Launcher app;
        app.sdRoot_=root.string();
        SystemInfo system;
        system.id=system.label="GB";
        system.romPaths={(root/"Roms/GB").string()};
        system.artworkPath=(root/"Roms/GB/Imgs").string();
        system.launcherPath=(root/"Emu/GB/launch.sh").string();
        system.extensions={".gb"};
        app.systems_.push_back(system);
        app.discoverRecent(app.sdRoot_);
        app.discoverApps(app.sdRoot_);
        app.mergeRecentHistory(app.categories_.front(),app.categories_.back());
        return app;
    };
    auto titles=[](const Launcher& app) {
        std::vector<std::string> result;
        for(const auto& item:app.categories_.front().entries) result.push_back(item.title);
        return result;
    };
    auto launchApp=[](Launcher& app,const std::string& title) {
        for(const auto& item:app.categories_.back().entries){
            if(item.title!=title) continue;
            assert(app.rememberRecentLaunch(item));
            return;
        }
        assert(false);
    };
    auto launchGame=[&](Launcher& app,const std::string& name) {
        Game game;
        game.romPath=(root/"Roms/GB"/(name+".gb")).string();
        assert(app.rememberRecentLaunch(game));
    };

    Launcher app=open();
    assert(titles(app)==std::vector<std::string>{"First"});
    launchApp(app,"File Explorer");
    app=open();
    assert((titles(app)==std::vector<std::string>{"File Explorer","First"}));
    const Game& recentApp=app.categories_.front().entries.front();
    assert(recentApp.kind==EntryKind::App && recentApp.artPath==(fileExplorer/"icon.png").string());
    assert(recentApp.workingDirectory==fileExplorer.string());
    assert(recentApp.launcherPath==(fileExplorer/"launch.sh").string());
    launchGame(app,"Second");
    write(history,second+first+alias); // RetroArch records a newly played ROM on exit.
    app=open();
    assert((titles(app)==std::vector<std::string>{"Second","File Explorer","First"}));
    launchApp(app,"Terminal");
    app=open();
    assert((titles(app)==std::vector<std::string>{"Terminal","Second","File Explorer","First"}));
    app.updateSearchResults();
    assert((app.searchResults_==std::vector<std::pair<size_t,size_t>>{{0,1},{0,3}}));
    assert(app.randomGameCandidates().empty());

    launchApp(app,"File Explorer");
    app=open();
    assert((titles(app)==std::vector<std::string>{"File Explorer","Terminal","Second","First"}));
    launchGame(app,"Standalone");
    app=open();
    assert((titles(app)==std::vector<std::string>{"Standalone","File Explorer","Terminal","Second","First"}));
    launchApp(app,"Terminal");
    write(history,first+third+second); // An external replay and a new game.
    app=open();
    assert((titles(app)==std::vector<std::string>{"First","Third","Terminal","Standalone","File Explorer","Second"}));

    launchApp(app,"Main UI");
    launchApp(app,"CartOS settings");
    assert(titles(open())==titles(app));
    touch(fileExplorer/"new.sh");
    write(fileExplorer/"config.json",R"({"label":"File Manager","launch":"new.sh"})");
    fs::remove(terminal/"launch.sh");
    fs::remove(root/"Roms/GB/Standalone.gb");
    app=open();
    assert((titles(app)==std::vector<std::string>{"First","Third","File Manager","Second"}));
    assert(app.categories_.front().entries[2].launcherPath==(fileExplorer/"new.sh").string());
    assert(contents(native)==nativeData && contents(history)==first+third+second);
    std::cout<<"PASS mixed Recent order, deduplication, native history changes, standalone games, current app targets, removed items, and game-only search\n";
}

static void checkRestoreSetting(const fs::path& base) {
    const fs::path prior=fs::current_path(), root=base/"restore-setting";
    fs::create_directories(root/"App/CartOS"); fs::current_path(root);
    const fs::path config=root/"App/CartOS/settings.ini";
    auto fixture=[&]() {
        Launcher app; app.sdRoot_=root.string();
        Game game; game.romPath=(root/"Game.gb").string(); game.title="Game";
        Category system; system.orderKey="system:GB"; system.systemId="GB"; system.entries.push_back(game);
        Category recent; recent.orderKey="recent";
        app.categories_={system,recent}; app.rememberedSelections_={0,0};
        SystemInfo info; info.id="GB"; app.systems_.push_back(info);
        return app;
    };
    Launcher app=fixture(); assert(!app.restoreBrowserPosition_);
    assert(app.settingsRowCount()==14); // 12 general + 2 system controls.
    assert(app.saveBrowserState(0,app.categories_[0].entries[0].romPath));
    assert(!fs::exists(".browser-state"));
    // Existing installations have no key: it must stay off, even on refresh.
    write(config,"show_game_count=1\n"); app.restoreBrowserPosition_=true; app.loadSettings();
    assert(!app.restoreBrowserPosition_);
    app.overlay_=Launcher::Overlay::Settings; app.settingsSelection_=10;
    app.handle(button(SDL_CONTROLLER_BUTTON_A));
    assert(app.restoreBrowserPosition_ && contents(config).find("restore_browser_position=1\n")!=std::string::npos);
    Launcher restarted=fixture(); restarted.loadSettings(); assert(restarted.restoreBrowserPosition_);
    assert(restarted.saveBrowserState(0,restarted.categories_[0].entries[0].romPath));
    restarted.restoreBrowserState(true); assert(restarted.categoryIndex_==0);
    // Turning it off persists and removes an old pending position.
    assert(app.saveBrowserState(0,app.categories_[0].entries[0].romPath));
    assert(fs::exists(".browser-state"));
    app.handle(button(SDL_CONTROLLER_BUTTON_A));
    assert(!app.restoreBrowserPosition_ && !fs::exists(".browser-state"));
    assert(contents(config).find("restore_browser_position=0\n")!=std::string::npos);
    restarted=fixture(); restarted.loadSettings(); assert(!restarted.restoreBrowserPosition_);
    // A leftover snapshot from the earlier always-on beta must not restore.
    Launcher enabled=fixture(); enabled.restoreBrowserPosition_=true;
    assert(enabled.saveBrowserState(0,enabled.categories_[0].entries[0].romPath));
    restarted.restoreBrowserState(true);
    assert(restarted.categoryIndex_==1 && !fs::exists(".browser-state"));
    // Reset returns the new option to OFF and keeps per-system rows aligned.
    app.restoreBrowserPosition_=true; app.settingsSelection_=11; app.changeSetting(1);
    assert(!app.restoreBrowserPosition_);
    app.settingsSelection_=12; app.changeSetting(1); assert(app.cartridgeScaleFor("GB")==2);
    app.settingsSelection_=13; app.changeSetting(1); assert(app.systemHiddenFor("GB"));
    app.moveSettingsSelection(1); assert(app.settingsSelection_==0);
    write(config,"restore_browser_position=invalid\n");
    app.restoreBrowserPosition_=true; app.loadSettings(); assert(!app.restoreBrowserPosition_);
    fs::current_path(prior);
    std::cout<<"PASS restore setting defaults OFF, toggles/persists, resets, ignores stale state, and preserves system rows\n";
}
static void checkWholeDeviceRandom(const fs::path& base) {
    Launcher app; app.sdRoot_=base.string();
    Game a,b,c; a.title="Alpha"; a.romPath=(base/"GB/Alpha.gb").string(); a.launcherPath="/Emu/GB/launch.sh";
    b=a; b.title="Beta"; b.romPath=(base/"GB/Beta.gb").string();
    c=a; c.title="Gamma"; c.romPath=(base/"GBA/Gamma.gba").string(); c.launcherPath="/Emu/GBA/launch.sh";
    Game utility=a; utility.title="~Filter";
    Game application=a; application.kind=EntryKind::App;
    Game noRom=a; noRom.romPath.clear();
    Game noLauncher=a; noLauncher.launcherPath.clear();
    Category gb; gb.systemId="GB"; gb.orderKey="system:GB"; gb.entries={a,b,utility,application,noRom,noLauncher};
    Category gba; gba.systemId="GBA"; gba.orderKey="system:GBA";
    Game alias=a; alias.romPath="/mnt/SDCARD/GB/../GB/Alpha.gb";
    gba.entries={c,alias};
    Category recent; recent.orderKey="recent"; recent.entries.assign(10,a);
    Category favourite; favourite.orderKey="favourite"; favourite.entries={b,c};
    Category custom; custom.orderKey="collection:Main"; custom.entries={a,b,c};
    Category apps; apps.orderKey="apps"; apps.entries={application};
    app.categories_={recent,gb,gba,favourite,custom,apps};
    app.hiddenSystemById_["GBA"]=true;
    const auto pool=app.randomGameCandidates();
    const std::vector<std::pair<size_t,size_t>> expected{{1,0},{1,1},{2,0}};
    assert(pool==expected); // Both systems, including hidden; one entry per ROM.
    app.categoryIndex_=4; assert(app.randomGameCandidates()==expected);
    // Exercise the real menu action. Its only candidate lives outside the
    // current saved list, so this test cannot pass with the former list-local code.
    Launcher action; action.sdRoot_=base.string();
    Category only; only.systemId="GBA"; only.orderKey="system:GBA"; only.entries={c};
    action.categories_={custom,only}; action.categoryIndex_=0;
    action.position_=action.target_=1.0f; action.rememberedSelections_={0,0};
    action.menuOptions_={"Play random game"}; action.overlaySelection_=0;
    action.overlay_=Launcher::Overlay::SelectMenu;
    action.activateSelectMenu();
    assert(action.categoryIndex_==1 && action.position_==0.0f && action.target_==0.0f);
    assert(action.rememberedSelections_[0]==1 && action.rememberedSelections_[1]==0);
    assert(action.launchAnimation_ && !action.launchStepTwoConfirmed_);
    assert(action.launchAnimationCategory_==1 && action.launchAnimationEntry_==0);
    assert(action.launchingGame()->romPath==c.romPath && action.overlay_==Launcher::Overlay::None);
    assert(!action.restoreBrowserPosition_); // This option remains OFF by default.
    Launcher empty; empty.categories_={apps,custom}; empty.categoryIndex_=1;
    empty.overlay_=Launcher::Overlay::SelectMenu; empty.playRandomGame();
    assert(!empty.launchAnimation_ && empty.statusMessage_=="No games available");
    assert(empty.categoryIndex_==1 && empty.overlay_==Launcher::Overlay::None);
    Launcher none; none.playRandomGame(); assert(!none.launchAnimation_ && none.statusMessage_=="No games available");
    std::cout<<"PASS whole-device random pool, unique-ROM weighting, hidden systems, app/utility exclusion, menu animation, empty library\n";
}
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
    checkSelectShortcuts(base);
    checkBrowserResume(base);
    checkMixedRecent(base);
    checkRestoreSetting(base);
    checkWholeDeviceRandom(base);
    launcher.handle(button(SDL_CONTROLLER_BUTTON_A));
    assert(launcher.launchAnimation_ && !launcher.launchStepTwoConfirmed_);
    assert(launcher.launchCenterDuration_==0.0f); // Already centred: no pre-growth pause.
    launcher.launchAwaitingConfirm_=true;
    launcher.handle(button(SDL_CONTROLLER_BUTTON_A));
    assert(launcher.launchStepTwoConfirmed_ && !launcher.launchAwaitingConfirm_);
    launcher.launchAnimation_=false; launcher.position_=0.25f;
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START));
    assert(!launcher.launchAnimation_);
    launcher.handle(released(SDL_CONTROLLER_BUTTON_START));
    assert(launcher.launchAnimation_ && launcher.launchStepTwoConfirmed_);
    assert(launcher.launchCenterDuration_==0.16f && launcher.launchAnimationElapsed_==0.0f);
    assert(launcher.launchingGame()->romPath==game.romPath && launcher.artworkIndexes_.empty());
    launcher.launchAnimation_=false; launcher.overlay_=Launcher::Overlay::Search;
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START));
    assert(launcher.searchResultsFocused_ && !launcher.launchAnimation_);
    launcher.overlay_=Launcher::Overlay::Settings;
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START)); launcher.handle(released(SDL_CONTROLLER_BUTTON_START)); assert(!launcher.launchAnimation_);
    launcher.overlay_=Launcher::Overlay::SelectMenu;
    launcher.menuOptions_={"Game details"};
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START)); launcher.handle(released(SDL_CONTROLLER_BUTTON_START)); assert(!launcher.launchAnimation_);
    launcher.overlay_=Launcher::Overlay::None;
    launcher.categories_[0].entries[0].kind=EntryKind::App;
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START)); launcher.handle(released(SDL_CONTROLLER_BUTTON_START)); assert(!launcher.launchAnimation_);
    launcher.categories_[0].entries[0].kind=EntryKind::Game;
    launcher.categories_[0].entries[0].romPath="/missing/rom.zip";
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START)); launcher.handle(released(SDL_CONTROLLER_BUTTON_START)); assert(!launcher.launchAnimation_);
    launcher.categories_[0].entries.clear();
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START)); launcher.handle(released(SDL_CONTROLLER_BUTTON_START)); assert(!launcher.launchAnimation_);
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
    launcher.handle(button(SDL_CONTROLLER_BUTTON_START)); launcher.handle(released(SDL_CONTROLLER_BUTTON_START)); assert(!launcher.launchAnimation_);
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
