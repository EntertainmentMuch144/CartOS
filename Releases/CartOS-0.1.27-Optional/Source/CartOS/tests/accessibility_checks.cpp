// Disposable host-side checks: never starts OnionOS, an app, or an emulator.
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
#include "draw.hpp"
#include "artwork.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <unistd.h>
namespace fs=std::filesystem;
using Overlay=Launcher::Overlay;
static SDL_Event button(Uint8 b,bool down=true){SDL_Event e{};e.type=down?SDL_CONTROLLERBUTTONDOWN:SDL_CONTROLLERBUTTONUP;e.cbutton.button=b;return e;}
static SDL_Event key(SDL_Keycode k,bool down=true){SDL_Event e{};e.type=down?SDL_KEYDOWN:SDL_KEYUP;e.key.keysym.sym=k;return e;}
static SDL_Event raw(Uint8 b,bool down=true){SDL_Event e{};e.type=down?SDL_JOYBUTTONDOWN:SDL_JOYBUTTONUP;e.jbutton.button=b;return e;}
static void press(Launcher& app,Uint8 b){app.handle(button(b));app.handle(button(b,false));}
static std::string read(const fs::path& path){std::ifstream f(path);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static void write(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream(path)<<text;}
static Launcher fixture(const fs::path& root){
    Launcher app;app.sdRoot_=root.string();
    fs::create_directories(root/"App/CartOS");
    for(int n=0;n<3;++n){
        Game g;g.title=n==0?"Accessible Adventure - a very long ROM title (USA, Europe)":n==1?"Balloon":"Zoo";
        g.console="GB";g.system="GAME BOY";g.romPath=(root/"Roms/GB"/(std::to_string(n)+".gb")).string();
        g.launcherPath=(root/"Emu/GB/launch.sh").string();write(g.romPath,"dummy");
        if(n==0){Category cat;cat.label="GAME BOY";cat.systemId="GB";cat.orderKey="system:GB";app.categories_.push_back(cat);}
        app.categories_[0].entries.push_back(g);
    }
    Category recent=app.categories_[0];recent.label="recent";recent.systemId="";recent.orderKey="recent";
    app.categories_.push_back(recent);app.rememberedSelections_={0,0};
    SystemInfo info;info.id="GB";info.label="Game Boy";info.romPaths={(root/"Roms/GB").string()};info.artworkPath=(root/"Roms/GB/Imgs").string();app.systems_.push_back(info);
    return app;
}
static void preferences(const fs::path& root){
    using namespace accessibility;
    Preferences a;assert(!a.confirmLaunch && !a.navigationSounds && !a.vibration && !a.reducedMotion && a.textSize==1);
    assert(a.repeatDelay()==360 && a.repeatInterval()==180);
    a.read("a11y_text_size","bogus");a.read("a11y_scroll_speed","99999");a.read("a11y_sound_volume","-1");
    a.read("a11y_dimming","4");assert(a.textSize==1 && a.scrollSpeed==1 && a.soundVolume==50 && a.dimming==0);
    for(int n=0;n<2000;++n){a.changeBinding(n%8,n%2?1:-1);std::set<int> used(a.bindings.begin(),a.bindings.end());assert(used.size()==8);}
    a.bindings[0]=a.bindings[1];a.validate();for(int i=0;i<4;++i)assert(a.bindings[i]==i);
    a.read("a11y_bind_left","0");a.validate();for(int i=4;i<8;++i)assert(a.bindings[i]==i);
    a.resetBindings();a.changeBinding(Confirm,1);a.changeBinding(Left,1);
    assert(translate(button(SDL_CONTROLLER_BUTTON_B),a).cbutton.button==SDL_CONTROLLER_BUTTON_A);
    assert(translate(raw(5),a).cbutton.button==SDL_CONTROLLER_BUTTON_A);
    assert(translate(key(SDLK_LCTRL),a).cbutton.button==SDL_CONTROLLER_BUTTON_A);
    assert(translate(button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT),a).cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    assert(translate(key(SDLK_RIGHT,false),a).type==SDL_CONTROLLERBUTTONUP);
    assert(hints("A OPEN   B BACK   LEFT/RIGHT GAME   UP/DOWN LIST   START",a)=="B OPEN   A BACK   RIGHT/LEFT GAME   UP/DOWN LIST   START");
    for(std::string shortText:{"", "U", "LE", "S", "A", "UP/"})hints(shortText,a); // Short token boundaries are safe.
    const fs::path config=root/"App/CartOS/settings.ini";
    write(config,"# user comment\r\nbackground_file=BG_(White).png\r\nunknown_future=keep me\r\na11y_text_size=0\r\na11y_text_size=2\r\nlist_order_99=obsolete\r\n");
    assert(updateConfig(config.string(),a.entries()));
    const auto modified=fs::last_write_time(config);assert(updateConfig(config.string(),a.entries()));assert(fs::last_write_time(config)==modified);
    assert(read(config).find("unknown_future=keep me")!=std::string::npos && read(config).find("# user comment")!=std::string::npos);
    Launcher app=fixture(root);app.loadSettings();assert(app.accessibility_.bindings==a.bindings && !app.restoreBrowserPosition_);
    app.listOrder_={"recent","system:GB"};app.saveSettings();
    const std::string saved=read(config);assert(saved.find("list_order_99")==std::string::npos && saved.find("unknown_future=keep me")!=std::string::npos);
    assert(saved.find("background_file=BG_(White).png")!=std::string::npos);
    app.accessibility_.highContrast=true;app.saveAccessibility();
    app.settingsSelection_=11;app.changeSetting(1);assert(app.accessibility_.highContrast);
    app.openAccessibility();app.accessibilitySelection_=4;app.changeAccessibility(1,true);
    assert(app.overlay_==Overlay::AccessibilityResetConfirm && app.overlaySelection_==1);
    press(app,SDL_CONTROLLER_BUTTON_A);assert(app.accessibility_.highContrast); // Safe initial Cancel.
    app.accessibilitySelection_=4;app.changeAccessibility(1,true);app.overlaySelection_=0;
    press(app,SDL_CONTROLLER_BUTTON_B);assert(!app.accessibility_.highContrast && app.accessibility_.bindings==Preferences{}.bindings);
    assert(read(config).find("unknown_future=keep me")!=std::string::npos && read(config).find("carousel_spacing=1")!=std::string::npos);
    write(config,read(config)+"a11y_simple_navigation=1\n");app.loadSettings();app.saveAccessibility();
    assert(read(config).find("a11y_simple_navigation")==std::string::npos);
    app.accessibilityPage_=0;app.accessibilitySelection_=0;
    fs::create_directory(config.string()+".tmp");app.changeAccessibility(1,true);assert(app.accessibility_.textSize==1);fs::remove(config.string()+".tmp");
    std::cout<<"PASS defaults, malformed values, conflict-free mappings, atomic persistence, unrelated keys, isolated confirmed reset, write failure\n";
}
static void inputAndLaunch(const fs::path& root){
    using namespace accessibility;
    Launcher app=fixture(root);
    app.accessibility_.changeBinding(Confirm,1);
    press(app,SDL_CONTROLLER_BUTTON_B);assert(app.launchAnimation_ && !app.launchStepTwoConfirmed_);
    app.launchAwaitingConfirm_=true;app.accessibility_.confirmLaunch=true;
    press(app,SDL_CONTROLLER_BUTTON_B);assert(app.launchConfirmationOpen_ && !app.launchStepTwoConfirmed_);
    press(app,SDL_CONTROLLER_BUTTON_B);assert(!app.launchConfirmationOpen_ && app.launchAwaitingConfirm_); // mapped confirm chooses Cancel.
    press(app,SDL_CONTROLLER_BUTTON_B);press(app,SDL_CONTROLLER_BUTTON_DPAD_UP);press(app,SDL_CONTROLLER_BUTTON_B);
    assert(app.launchStepTwoConfirmed_ && !app.launchConfirmationOpen_ && !app.launchAwaitingConfirm_);
    app.launchAnimation_=false;app.accessibility_.resetBindings();
    press(app,SDL_CONTROLLER_BUTTON_START);assert(app.launchConfirmationOpen_ && !app.launchStepTwoConfirmed_);
    press(app,SDL_CONTROLLER_BUTTON_B);assert(!app.launchAnimation_ && !app.launchConfirmationOpen_);
    // Ordinary A opens preview; its second A asks for confirmation. A double tap does too.
    press(app,SDL_CONTROLLER_BUTTON_A);press(app,SDL_CONTROLLER_BUTTON_A);assert(app.launchConfirmationOpen_);
    press(app,SDL_CONTROLLER_BUTTON_B);assert(!app.launchAnimation_);
    app.startSearch();app.searchResults_={{0,1}};app.searchResultsFocused_=true;
    press(app,SDL_CONTROLLER_BUTTON_A);app.launchAwaitingConfirm_=true;press(app,SDL_CONTROLLER_BUTTON_A);
    assert(app.launchConfirmationOpen_ && app.launchAnimationEntry_==1);
    press(app,SDL_CONTROLLER_BUTTON_B);app.launchAnimation_=false;app.overlay_=Overlay::None;
    app.playRandomGame();app.launchAwaitingConfirm_=true;press(app,SDL_CONTROLLER_BUTTON_A);assert(app.launchConfirmationOpen_);
    press(app,SDL_CONTROLLER_BUTTON_B);app.launchAnimation_=false;app.overlay_=Overlay::None;
    app.accessibility_.confirmLaunch=false;press(app,SDL_CONTROLLER_BUTTON_START);assert(app.launchStepTwoConfirmed_ && app.artworkIndexes_.empty());
    app.launchAnimation_=false;
    // Reserved START chords leave the browser intact and never quick launch.
    app.handle(button(SDL_CONTROLLER_BUTTON_START));SDL_Event axis{};axis.type=SDL_CONTROLLERAXISMOTION;axis.caxis.value=32767;
    app.handle(axis);app.handle(button(SDL_CONTROLLER_BUTTON_START,false));assert(!app.launchAnimation_);
    app.handle(button(SDL_CONTROLLER_BUTTON_GUIDE));press(app,SDL_CONTROLLER_BUTTON_A);app.handle(button(SDL_CONTROLLER_BUTTON_GUIDE,false));
    assert(!app.launchAnimation_ && !app.menuButtonHeld_);
    app.handle(raw(14));axis.caxis.value=0;app.handle(axis);assert(!app.menuButtonChord_);
    app.handle(raw(10));assert(app.menuButtonChord_);app.handle(raw(14,false));assert(!app.menuButtonHeld_);
    axis.caxis.value=32767;
    app.handle(button(SDL_CONTROLLER_BUTTON_BACK));app.handle(axis);app.handle(button(SDL_CONTROLLER_BUTTON_BACK,false));
    assert(app.overlay_==Overlay::None);
    // Safe recovery works even if both face and direction groups were remapped.
    app.accessibility_.changeBinding(Confirm,1);app.accessibility_.changeBinding(Up,1);
    app.handle(raw(12));app.handle(raw(13));app.handle(raw(12,false));
    assert(app.overlay_==Overlay::Accessibility && app.accessibility_.bindings==Preferences{}.bindings);
    app.accessibility_.changeBinding(Up,1);app.accessibilityPage_=1;
    app.handle(button(SDL_CONTROLLER_BUTTON_DPAD_DOWN));assert(app.scrollHeld_[2]);
    app.handle(button(SDL_CONTROLLER_BUTTON_DPAD_DOWN,false));assert(!app.scrollHeld_[2]);
    app.accessibility_.resetBindings();app.accessibilityPage_=1;app.accessibilitySelection_=0;
    app.accessibility_.scrollSpeed=0;app.handle(button(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
    app.updateHeldScroll(app.scrollRepeatAt_[3]-1);assert(app.accessibilitySelection_==1);
    const auto now=app.scrollRepeatAt_[3];app.updateHeldScroll(now);assert(app.accessibilitySelection_==2 && app.scrollRepeatAt_[3]==now+280);
    app.handle(button(SDL_CONTROLLER_BUTTON_DPAD_DOWN,false));
    app.accessibility_.scrollSpeed=2;app.handle(button(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
    const auto fast=app.scrollRepeatAt_[3];app.updateHeldScroll(fast+5000);assert(app.accessibilitySelection_==4 && app.scrollRepeatAt_[3]==fast+5000+110);
    app.scrollHeld_.fill(false);app.overlay_=Overlay::None;app.accessibility_.reducedMotion=true;app.applyAccessibility();
    app.move(1);app.applyAccessibility();assert(app.position_==app.target_);
    press(app,SDL_CONTROLLER_BUTTON_A);assert(app.launchAwaitingConfirm_ && !app.launchReversing_);
    press(app,SDL_CONTROLLER_BUTTON_B);assert(!app.launchAnimation_ && !app.launchReversing_);
    press(app,SDL_CONTROLLER_BUTTON_A);assert(app.launchAwaitingConfirm_ && !app.launchStepTwoConfirmed_);
    app.launchAnimation_=app.launchAwaitingConfirm_=false;
    // The internal application has no stand-alone OnionOS folder.
    app.launchAnimation_=false;app.categories_.clear();app.discoverApps(root.string());
    auto accessible=std::find_if(app.categories_[0].entries.begin(),app.categories_[0].entries.end(),[](const Game& g){return g.cartAccessibility;});
    assert(accessible!=app.categories_[0].entries.end());
    assert(fs::path(accessible->artPath).filename()=="accessibility.png");
    app.position_=static_cast<float>(accessible-app.categories_[0].entries.begin());app.categoryIndex_=0;
    app.launchSelected();assert(app.overlay_==Overlay::Accessibility && !fs::exists(root/"App/CartOS Accessibility"));
    app.vibrationAvailable_=true;app.accessibility_.vibration=true;app.inputFeedbackAt_=0;app.noteInput("RUMBLE FAILURE");
    assert(!app.vibrationAvailable_ && !app.accessibility_.vibration); // No actuator/controller is attached.
    app.accessibilityPage_=1;assert(app.accessibilityRows().size()==10 && app.accessibilityRowEnabled(1));
    for(const auto& row:app.accessibilityRows())assert(row.first.find("Simplified")==std::string::npos);
    app.accessibility_.read("a11y_simple_navigation","1");
    assert(app.accessibility_.entries().size()==22);
    app.accessibilityPage_=3;app.accessibility_.navigationSounds=false;app.accessibilitySelection_=1;
    const int previousVolume=app.accessibility_.soundVolume;assert(!app.accessibilityRowEnabled(1));
    app.changeAccessibility(1,true);assert(app.accessibility_.soundVolume==previousVolume);
    app.accessibilitySelection_=0;app.changeAccessibility(1,true);
    assert(app.accessibility_.navigationSounds && app.navigationAudio_.opened() && app.accessibilityRowEnabled(1));
    app.changeAccessibility(1,true);assert(!app.accessibility_.navigationSounds && !app.navigationAudio_.opened());
    app.accessibilityPage_=3;assert(app.accessibilityRows().size()==3);
    app.vibrationAvailable_=true;assert(app.accessibilityRows().size()==4);
    std::cout<<"PASS remapped input/release, modifier guards, recovery, all confirmation paths, START, held scroll rates, reduced motion, retired direct-launch preference, internal app, rumble capability gating\n";
}
static void renderScreens(const fs::path& root,const fs::path& output,const fs::path& source){
    fs::create_directories(output);
    SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormat(0,752,560,32,SDL_PIXELFORMAT_ARGB8888);assert(surface);
    SDL_Renderer* renderer=SDL_CreateSoftwareRenderer(surface);assert(renderer);
    assert(draw::initializeFont(renderer,(source/"../../App/CartOS/res/font_atlas.png").lexically_normal().string()));
    Launcher app=fixture(root);app.renderer_=renderer;app.batteryPollAt_=SDL_GetTicks();app.batteryPercent_=85;
    // Synthetic portrait and landscape artwork are deliberately extreme.
    auto texture=[&](int w,int h,SDL_Color color){SDL_Surface* image=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
        SDL_FillRect(image,nullptr,SDL_MapRGB(image->format,color.r,color.g,color.b));
        SDL_Rect border{4,4,w-8,h-8};SDL_FillRect(image,&border,SDL_MapRGB(image->format,60,70,80));
        SDL_Texture* t=SDL_CreateTextureFromSurface(renderer,image);SDL_FreeSurface(image);return t;};
    std::vector<SDL_Texture*> textures{texture(130,300,{230,170,60,255}),texture(340,130,{70,190,100,255}),texture(500,220,{90,140,240,255})};
    for(int i=0;i<3;++i){app.categories_[0].entries[i].artwork=textures[i];app.categories_[0].entries[i].artworkAttempted=true;}
    app.accessibility_.textSize=2;app.accessibility_.largeArtwork=true;app.accessibility_.largeControls=true;
    app.accessibility_.enhancedSelection=true;app.accessibility_.highContrast=true;app.accessibility_.reducedMotion=true;
    auto capture=[&](const std::string& name){app.render(0.016f);assert(SDL_SaveBMP(surface,(output/(name+".bmp")).c_str())==0);};
    for(int i=0;i<3;++i){app.position_=app.target_=static_cast<float>(i);capture("large-browser-"+std::to_string(i));}
    app.openSelectMenu();app.overlaySelection_=6;capture("large-options");
    app.overlay_=Overlay::Settings;app.settingsSelection_=8;capture("large-settings");
    app.overlay_=Overlay::CreateList;app.newListName_="Accessible custom list";capture("large-create-list");
    app.overlay_=Overlay::DeleteConfirm;app.overlaySelection_=1;capture("large-delete");
    app.overlay_=Overlay::GameInfo;capture("large-details");
    app.openAccessibility();capture("large-accessibility");
    for(int page=0;page<4;++page){app.accessibilityPage_=page;app.accessibilitySelection_=0;capture("large-section-"+std::to_string(page));}
    app.accessibilityPage_=1;app.accessibilitySelection_=9;capture("large-controls-end");
    app.overlay_=Overlay::AccessibilityResetConfirm;capture("large-reset");
    app.overlay_=Overlay::Search;app.searchQuery_="Adventure";app.searchResults_={{0,0},{0,1},{0,2}};app.searchResultSelection_=0;
    app.searchResultsFocused_=false;capture("large-search-keyboard");app.searchResultsFocused_=true;capture("large-search-results");
    app.overlay_=Overlay::None;app.beginLaunchAnimation(0,0);app.launchArtworkLoading_=false;capture("large-preview");
    app.previewShowingDetails_=true;capture("large-preview-details");app.previewShowingDetails_=false;
    app.accessibility_.confirmLaunch=true;app.requestGameLaunch();capture("large-confirm");
    app.launchConfirmationOpen_=false;app.launchAnimation_=false;app.accessibility_.hideSecondary=true;
    app.overlay_=Overlay::None;capture("large-secondary-hidden");
    app.accessibility_=accessibility::Preferences{};app.overlay_=Overlay::None;capture("default-browser");
    app.openAccessibility();capture("default-accessibility");
    app.accessibility_.textSize=0;capture("small-accessibility");
    // High contrast is independent of both a bright background and invert text.
    app.backgroundArtwork_=texture(752,560,{255,255,255,255});app.accessibility_.highContrast=true;app.invertTextColour_=true;
    app.overlay_=Overlay::None;capture("contrast-bright-background");
    Uint8 r,g,b;Uint32 pixel=static_cast<Uint32*>(surface->pixels)[550*surface->w+735];SDL_GetRGB(pixel,surface->format,&r,&g,&b);assert(r==0 && g==0 && b==0);
    app.accessibility_.highContrast=false;app.accessibility_.dimming=0;app.renderBackground();
    Uint32 before=static_cast<Uint32*>(surface->pixels)[300*surface->w+350];
    app.accessibility_.dimming=3;app.renderBackground();Uint32 after=static_cast<Uint32*>(surface->pixels)[300*surface->w+350];
    Uint8 br,bg,bb,ar,ag,ab;SDL_GetRGB(before,surface->format,&br,&bg,&bb);SDL_GetRGB(after,surface->format,&ar,&ag,&ab);assert(ar<br && ag<bg && ab<bb);
    SDL_DestroyTexture(app.backgroundArtwork_);app.backgroundArtwork_=nullptr;
    // Adding preview text must not change the selected texture's bounds.
    app.accessibility_=accessibility::Preferences{};app.invertTextColour_=false;app.categoryIndex_=0;app.position_=app.target_=0;
    auto imageBounds=[&](){
        SDL_Rect box{752,560,0,0};int right=-1,bottom=-1;
        for(int y=0;y<560;++y)for(int x=0;x<752;++x){
            Uint8 pr,pg,pb;SDL_GetRGB(static_cast<Uint32*>(surface->pixels)[y*surface->w+x],surface->format,&pr,&pg,&pb);
            if(pr==230 && pg==170 && pb==60){box.x=std::min(box.x,x);box.y=std::min(box.y,y);right=std::max(right,x);bottom=std::max(bottom,y);}
        }
        box.w=right-box.x+1;box.h=bottom-box.y+1;assert(box.w>0 && box.h>0);return box;
    };
    auto sameBounds=[](SDL_Rect a,SDL_Rect b){return a.x==b.x && a.y==b.y && a.w==b.w && a.h==b.h;};
    SDL_Texture* widePrimary=texture(340,130,{230,170,60,255});textures.push_back(widePrimary);
    for(SDL_Texture* primary:{textures[0],widePrimary})for(bool search:{false,true})for(float position:{0.0f,.25f}){
        app.categories_[0].entries[0].artwork=primary;
        app.launchAnimation_=app.launchAwaitingConfirm_=app.launchReversing_=false;
        app.overlay_=search?Overlay::Search:Overlay::None;app.searchResultsFocused_=true;app.searchResultSelection_=0;
        app.searchResults_={{0,0},{0,1},{0,2}};app.position_=app.target_=position;app.render(0.0f);
        const auto browserImage=imageBounds();
        app.beginLaunchAnimation(0,0);app.render(0.0f);
        if(!sameBounds(browserImage,imageBounds())){const auto now=imageBounds();std::cerr<<"Entry mismatch search="<<search<<" position="<<position<<" before "<<browserImage.x<<","<<browserImage.y<<","<<browserImage.w<<","<<browserImage.h<<" after "<<now.x<<","<<now.y<<","<<now.w<<","<<now.h<<"\n";}
        assert(sameBounds(browserImage,imageBounds()));
        SDL_Rect previous=imageBounds();
        for(int frame=1;frame<=40;++frame){
            app.launchAnimationElapsed_=frame/60.0f;
            app.render(0.0f);const auto next=imageBounds();
            if(app.launchAnimationElapsed_<=app.launchCenterDuration_){assert(next.w==previous.w && next.h==previous.h);}
            else if(app.launchAnimationElapsed_<=app.launchCenterDuration_+.50f){
                assert(next.h>=previous.h && next.w>=previous.w);
                assert(next.h-previous.h<=20 && next.w-previous.w<=20);
            }
            previous=next;
            if(app.launchAnimationElapsed_>=app.launchCenterDuration_+.50f)break;
        }
        app.launchAnimationElapsed_=app.launchCenterDuration_+.50f;app.launchAwaitingConfirm_=true;
        app.render(0.0f);const auto full=imageBounds();
        press(app,SDL_CONTROLLER_BUTTON_B);app.render(0.0f);assert(sameBounds(full,imageBounds()));
        app.launchReverseElapsed_=.32f;app.render(0.0f);const auto returned=imageBounds();
        app.launchAnimation_=app.launchReversing_=false;app.render(0.0f);assert(sameBounds(returned,imageBounds()));
    }

    app.categories_[0].entries[0].artwork=textures[0];
    for(bool search:{false,true}){
        app.overlay_=search?Overlay::Search:Overlay::None;app.searchResultSelection_=0;app.searchResults_={{0,0},{0,1},{0,2}};
        app.beginLaunchAnimation(0,0);app.launchAnimationElapsed_=app.launchCenterDuration_+.50f;
        capture(search?"search-before-preview-text":"browser-before-preview-text");const auto beforeText=imageBounds();
        app.launchAwaitingConfirm_=true;app.launchArtworkLoading_=false;
        capture(search?"search-after-preview-text":"browser-after-preview-text");assert(sameBounds(beforeText,imageBounds()));
        app.commitGameLaunch();app.render(.016f);assert(sameBounds(beforeText,imageBounds()));
        app.launchAnimation_=false;app.launchAwaitingConfirm_=false;
    }
    app.overlay_=Overlay::None;
    app.categories_[0].entries[0].title="Internal accessibility application";
    app.categories_[0].entries[0].kind=EntryKind::App;app.categories_[0].entries[0].cartAccessibility=true;
    app.categories_[0].entries[0].artPath=(source/"assets/accessibility.png").string();
    app.categories_[0].entries[0].artwork=artwork::loadPng(renderer,app.categories_[0].entries[0].artPath);
    assert(app.categories_[0].entries[0].artwork);textures.push_back(app.categories_[0].entries[0].artwork);
    app.categories_[0].entries[0].artworkAttempted=true;
    app.accessibility_=accessibility::Preferences{};app.invertTextColour_=false;app.position_=app.target_=0;capture("accessibility-app-icon");
    app.accessibility_.textSize=2;app.accessibility_.highContrast=true;app.accessibility_.largeControls=true;capture("large-accessibility-app-icon");
    app.accessibility_.visualFeedback=true;app.noteInput("CONFIRM");capture("visual-edge-feedback");
    for(const auto point:std::array<SDL_Point,4>{{{3,280},{748,280},{376,3},{376,556}}}){
        Uint32 feedback=static_cast<Uint32*>(surface->pixels)[point.y*surface->w+point.x];SDL_GetRGB(feedback,surface->format,&r,&g,&b);assert(r>200 && g>200);
    }
    // Text sizes x high contrast x large UI x large artwork x secondary info.
    for(int text=0;text<3;++text)for(int flags=0;flags<16;++flags){
        app.accessibility_.textSize=text;app.accessibility_.highContrast=flags&1;app.accessibility_.largeControls=flags&2;
        app.accessibility_.largeArtwork=flags&4;app.accessibility_.hideSecondary=flags&8;app.accessibility_.visualFeedback=false;
        app.overlay_=Overlay::None;app.render(.016f);
        app.overlay_=Overlay::Search;app.searchResultsFocused_=false;app.render(.016f);
        app.overlay_=Overlay::Accessibility;app.accessibilityPage_=1;app.accessibilitySelection_=9;app.render(.016f);
    }
    // Logical sizing also produces valid output on the 640x480 Onion display.
    SDL_Surface* smaller=SDL_CreateRGBSurfaceWithFormat(0,640,480,32,SDL_PIXELFORMAT_ARGB8888);assert(smaller);
    SDL_Renderer* smallRenderer=SDL_CreateSoftwareRenderer(smaller);assert(smallRenderer);
    assert(SDL_RenderSetLogicalSize(smallRenderer,752,560)==0);
    draw::shutdownFont();assert(draw::initializeFont(smallRenderer,(source/"../../App/CartOS/res/font_atlas.png").lexically_normal().string()));
    app.renderer_=smallRenderer;app.openAccessibility();app.accessibility_.textSize=2;app.accessibility_.highContrast=true;app.render(.016f);
    assert(SDL_SaveBMP(smaller,(output/"640x480-large-accessibility.bmp").c_str())==0);
    draw::shutdownFont();SDL_DestroyRenderer(smallRenderer);SDL_FreeSurface(smaller);
    app.renderer_=renderer;
    for(auto& cat:app.categories_)for(auto& game:cat.entries)game.artwork=nullptr;
    for(auto* t:textures)SDL_DestroyTexture(t);
    app.renderer_=nullptr;draw::shutdownFont();SDL_DestroyRenderer(renderer);SDL_FreeSurface(surface);
    std::cout<<"PASS 33 captures, 48 combined-setting permutations across browser/search/controls, dimming pixels, edge feedback, 640x480 logical sizing\n";
}
int main(int argc,char** argv){
    assert(argc==3);SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_GAMECONTROLLER)==0);
    char path[]="/tmp/cartos-accessibility-check-XXXXXX";assert(mkdtemp(path));const fs::path root=path;
    preferences(root/"prefs");inputAndLaunch(root/"inputs");
    SDL_setenv("SDL_AUDIODRIVER","cartos-unavailable-test-driver",1);
    Launcher failedAudio=fixture(root/"audio-failure");failedAudio.accessibility_.navigationSounds=true;
    failedAudio.noteInput("AUDIO FAILURE");assert(!failedAudio.accessibility_.navigationSounds && !failedAudio.navigationAudio_.opened());
    assert(failedAudio.statusMessage_=="Navigation audio unavailable");
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    const auto silent=accessibility::navigationTone(0),soft=accessibility::navigationTone(25),loud=accessibility::navigationTone(100);
    int softPeak=0,loudPeak=0;
    for(size_t i=0;i<loud.size();++i){assert(silent[i]==0);softPeak=std::max(softPeak,std::abs(static_cast<int>(soft[i])));loudPeak=std::max(loudPeak,std::abs(static_cast<int>(loud[i])));}
    assert(softPeak>500 && loudPeak>softPeak*3 && loudPeak<5000 && loud.front()==0 && loud.back()==0);
    accessibility::NavigationAudio audio;assert(audio.play(0) && !audio.opened());
    assert(audio.play(25) && audio.opened());assert(audio.play(100));audio.close();assert(!audio.opened() && !(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO));
    for(int i=0;i<5;++i){assert(audio.play(50));audio.close();assert(!(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO));}
    renderScreens(root/"render",argv[1],argv[2]);
    fs::remove_all(root);SDL_Quit();std::cout<<"PASS dummy audio queue, mute, volume synthesis and close; physical audio/rumble require handheld verification\n";
}
