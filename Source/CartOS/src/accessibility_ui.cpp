#include "launcher.hpp"
#include "draw.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unistd.h>

namespace {
const SDL_Color light{240,240,240,255},secondary{170,177,190,255},accent{145,205,255,255};
void rectangle(SDL_Renderer* renderer,SDL_Rect box,SDL_Color color){
    color=draw::panelColor(color);SDL_SetRenderDrawColor(renderer,color.r,color.g,color.b,color.a);SDL_RenderFillRect(renderer,&box);
}
void centered(SDL_Renderer* renderer,int y,const std::string& text,int height,SDL_Color color=light){
    const std::string fitted=draw::fitText(text,height,700);
    draw::text(renderer,(752-draw::textWidth(fitted,height))/2,y,fitted,height,color);
}
const char* toggle(bool enabled){return enabled?"ON":"OFF";}
bool pressed(const SDL_Event& e,Uint8 button){return e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.button==button;}
int cycle(int value,int direction,int count){return (value+(direction<0?count-1:1))%count;}
}

void Launcher::openAccessibility(){
    overlay_=Overlay::Accessibility;accessibilityPage_=-1;accessibilitySelection_=0;scrollHeld_.fill(false);
}
std::vector<std::pair<std::string,std::string>> Launcher::accessibilityRows() const{
    const auto& a=accessibility_;
    if(accessibilityPage_<0)return {{"Visual accessibility",">"},{"Controls and navigation",">"},
        {"Display preferences",">"},{"Audio and feedback",">"},{"Reset Accessibility Settings",">"}};
    if(accessibilityPage_==0)return {{"Interface text size",a.textSize==0?"SMALL":a.textSize==2?"LARGE":"DEFAULT"},
        {"High contrast",toggle(a.highContrast)},{"Reduced motion",toggle(a.reducedMotion)},
        {"UI dimming",a.dimming==0?"NONE":a.dimming==1?"LIGHT":a.dimming==2?"MEDIUM":"STRONG"}};
    if(accessibilityPage_==1){
        std::vector<std::pair<std::string,std::string>> rows{{"Confirm game launch",toggle(a.confirmLaunch)}};
        for(int i=0;i<accessibility::ButtonCount;++i)rows.emplace_back(accessibility::actionName(i),accessibility::buttonName(a.bindings[static_cast<size_t>(i)]));
        rows.emplace_back("Reset button mappings",">");return rows;
    }
    if(accessibilityPage_==2)return {{"Artwork size",a.largeArtwork?"LARGE":"DEFAULT"},
        {"UI element size",a.largeControls?"LARGE":"DEFAULT"},{"Hide secondary information",toggle(a.hideSecondary)},
        {"Scrolling speed",a.scrollSpeed==0?"SLOW":a.scrollSpeed==2?"FAST":"DEFAULT"},
        {"Enhanced selection indicator",toggle(a.enhancedSelection)}};
    std::vector<std::pair<std::string,std::string>> rows{{"Navigation sounds",toggle(a.navigationSounds)},
        {"Interface sound volume",std::to_string(a.soundVolume)+"%"},{"Visual button feedback",toggle(a.visualFeedback)}};
    if(vibrationAvailable_)rows.emplace_back("Vibration feedback",toggle(a.vibration));
    return rows;
}
bool Launcher::accessibilityRowEnabled(int row) const{
    return !(accessibilityPage_==3 && row==1 && !accessibility_.navigationSounds);
}
void Launcher::applyAccessibility(){
    draw::accessibilityStyle(accessibility_.textSize,accessibility_.highContrast,accessibility_.enhancedSelection);
    if(accessibility_.reducedMotion){startupAnimation_=false;position_=target_;}
    if(!accessibility_.navigationSounds || accessibility_.soundVolume==0)navigationAudio_.close();
}
bool Launcher::saveAccessibility(){return accessibility::updateConfig(sdRoot_+"/App/CartOS/settings.ini",accessibility_.entries(),{"a11y_simple_navigation"});}
std::string Launcher::controlHints(const std::string& text) const{return accessibility::hints(text,accessibility_);}

void Launcher::changeAccessibility(int direction,bool activate){
    const auto rows=accessibilityRows();if(rows.empty())return;
    accessibilitySelection_=std::max(0,std::min(static_cast<int>(rows.size())-1,accessibilitySelection_));
    if(accessibilityPage_<0){
        if(!activate)return;
        if(accessibilitySelection_<4){accessibilityPage_=accessibilitySelection_;accessibilitySelection_=0;}
        else{accessibilityResetBindingsOnly_=false;overlaySelection_=1;overlay_=Overlay::AccessibilityResetConfirm;}
        return;
    }
    if(!accessibilityRowEnabled(accessibilitySelection_))return;
    if(accessibilityPage_==1 && accessibilitySelection_==9){
        if(activate){accessibilityResetBindingsOnly_=true;overlaySelection_=1;overlay_=Overlay::AccessibilityResetConfirm;}
        return;
    }
    const accessibility::Preferences previous=accessibility_;
    auto& a=accessibility_;
    if(accessibilityPage_==0){
        if(accessibilitySelection_==0)a.textSize=cycle(a.textSize,direction,3);
        else if(accessibilitySelection_==1)a.highContrast=!a.highContrast;
        else if(accessibilitySelection_==2)a.reducedMotion=!a.reducedMotion;
        else a.dimming=cycle(a.dimming,direction,4);
    }else if(accessibilityPage_==1){
        if(accessibilitySelection_==0)a.confirmLaunch=!a.confirmLaunch;
        else a.changeBinding(accessibilitySelection_-1,direction);
    }else if(accessibilityPage_==2){
        if(accessibilitySelection_==0)a.largeArtwork=!a.largeArtwork;
        else if(accessibilitySelection_==1)a.largeControls=!a.largeControls;
        else if(accessibilitySelection_==2)a.hideSecondary=!a.hideSecondary;
        else if(accessibilitySelection_==3)a.scrollSpeed=cycle(a.scrollSpeed,direction,3);
        else a.enhancedSelection=!a.enhancedSelection;
    }else{
        if(accessibilitySelection_==0)a.navigationSounds=!a.navigationSounds;
        else if(accessibilitySelection_==1)a.soundVolume=cycle(a.soundVolume/25,direction,5)*25;
        else if(accessibilitySelection_==2)a.visualFeedback=!a.visualFeedback;
        else if(vibrationAvailable_)a.vibration=!a.vibration;
    }
    if(!saveAccessibility()){
        accessibility_=previous;statusMessage_="Could not save accessibility settings";statusUntil_=SDL_GetTicks64()+2500;
    }
    scrollHeld_.fill(false);applyAccessibility();
    if(!previous.navigationSounds && a.navigationSounds)inputFeedbackAt_=0; // Preview once when enabled.
    noteInput("SETTING CHANGED");
    if(previous.vibration && !a.vibration){
        if(controller_)SDL_GameControllerRumble(controller_,0,0,0);
        else if(joystick_)SDL_JoystickRumble(joystick_,0,0,0);
    }
}
void Launcher::moveAccessibilitySelection(int direction){
    const int count=static_cast<int>(accessibilityRows().size());
    if(count)accessibilitySelection_=(accessibilitySelection_+direction+count)%count;
}

bool Launcher::handleAccessibilityOverlay(const SDL_Event& e){
    if(overlay_!=Overlay::Accessibility && overlay_!=Overlay::AccessibilityResetConfirm)return false;
    const bool back=pressed(e,SDL_CONTROLLER_BUTTON_B) || (e.type==SDL_KEYDOWN && !e.key.repeat && (e.key.keysym.sym==SDLK_ESCAPE || e.key.keysym.sym==SDLK_BACKSPACE));
    const bool confirm=pressed(e,SDL_CONTROLLER_BUTTON_A) || (e.type==SDL_KEYDOWN && !e.key.repeat && e.key.keysym.sym==SDLK_RETURN);
    if(overlay_==Overlay::AccessibilityResetConfirm){
        if(back){overlay_=Overlay::Accessibility;return true;}
        if(pressed(e,SDL_CONTROLLER_BUTTON_DPAD_UP) || pressed(e,SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
           pressed(e,SDL_CONTROLLER_BUTTON_DPAD_LEFT) || pressed(e,SDL_CONTROLLER_BUTTON_DPAD_RIGHT))overlaySelection_=1-overlaySelection_;
        if(confirm){
            if(overlaySelection_==0){
                const accessibility::Preferences previous=accessibility_;
                if(accessibilityResetBindingsOnly_)accessibility_.resetBindings();
                else accessibility_=accessibility::Preferences{};
                if(!saveAccessibility()){
                    accessibility_=previous;statusMessage_="Could not save accessibility reset";
                }else statusMessage_=accessibilityResetBindingsOnly_?"Default controls restored":"Accessibility settings reset";
                statusUntil_=SDL_GetTicks64()+2200;applyAccessibility();
                if(controller_)SDL_GameControllerRumble(controller_,0,0,0);
                else if(joystick_)SDL_JoystickRumble(joystick_,0,0,0);
            }
            overlay_=Overlay::Accessibility;scrollHeld_.fill(false);
        }
        return true;
    }
    if(back){
        if(accessibilityPage_<0)overlay_=Overlay::None;
        else{accessibilitySelection_=accessibilityPage_;accessibilityPage_=-1;}
        scrollHeld_.fill(false);return true;
    }
    if(pressed(e,SDL_CONTROLLER_BUTTON_DPAD_UP))setScrollHeld(2,true);
    else if(pressed(e,SDL_CONTROLLER_BUTTON_DPAD_DOWN))setScrollHeld(3,true);
    else if(pressed(e,SDL_CONTROLLER_BUTTON_DPAD_LEFT))changeAccessibility(-1,false);
    else if(pressed(e,SDL_CONTROLLER_BUTTON_DPAD_RIGHT))changeAccessibility(1,false);
    else if(confirm)changeAccessibility(1,true);
    return true;
}

void Launcher::noteInput(const std::string& action){
    const Uint64 now=SDL_GetTicks64();
    if(inputFeedbackAt_ && now-inputFeedbackAt_<65)return;
    inputFeedbackAt_=now;inputFeedbackText_=action;
    if(accessibility_.navigationSounds && !navigationAudio_.play(accessibility_.soundVolume)){
        std::fprintf(stderr,"CartOS navigation audio: driver=%s error=%s\n",
            SDL_GetCurrentAudioDriver()?SDL_GetCurrentAudioDriver():(SDL_getenv("SDL_AUDIODRIVER")?SDL_getenv("SDL_AUDIODRIVER"):"default"),SDL_GetError());
        accessibility_.navigationSounds=false;navigationAudio_.close();saveAccessibility();
        statusMessage_="Navigation audio unavailable";statusUntil_=now+2500;
    }
    if(accessibility_.vibration && vibrationAvailable_){
        const int result=controller_?SDL_GameControllerRumble(controller_,0x1800,0x1800,25):
            joystick_?SDL_JoystickRumble(joystick_,0x1800,0x1800,25):-1;
        if(result<0){vibrationAvailable_=false;accessibility_.vibration=false;saveAccessibility();}
    }
}

void Launcher::requestGameLaunch(){
    if(accessibility_.confirmLaunch){
        launchConfirmationWasPreview_=launchAwaitingConfirm_;
        launchConfirmationOpen_=true;launchConfirmationSelection_=1;
        launchAwaitingConfirm_=true;launchStepTwoConfirmed_=false;
        scrollHeld_.fill(false);
    }else commitGameLaunch();
}
void Launcher::commitGameLaunch(){
    launchConfirmationOpen_=false;resetPreviewArtwork();
    launchAwaitingConfirm_=false;launchStepTwoConfirmed_=true;launchArtworkLoading_=false;
}
void Launcher::cancelLaunchConfirmation(){
    launchConfirmationOpen_=false;
    if(launchConfirmationWasPreview_)launchAwaitingConfirm_=true;
    else{launchAnimation_=false;launchAwaitingConfirm_=false;resetPreviewArtwork();}
}

void Launcher::presentFrame(){
    draw::artworkViewport({0,0,0,0});
    if(launchConfirmationOpen_){
        SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
        rectangle(renderer_,{0,0,752,560},{0,0,0,180});
        const SDL_Rect panel{64,126,624,300};rectangle(renderer_,panel,{4,7,15,255});
        draw::selection(renderer_,panel);
        centered(renderer_,148,"LAUNCH GAME?",27);
        const Game* game=launchingGame();centered(renderer_,205,draw::fitText(game?game->title:"Selected game",20,560),20);
        for(int i=0;i<2;++i){
            const SDL_Rect row{110,274+i*58,532,42};
            if(i==launchConfirmationSelection_){rectangle(renderer_,row,{32,50,72,255});draw::selection(renderer_,row);}
            centered(renderer_,row.y+7,(i==launchConfirmationSelection_?"> ":"  ")+std::string(i==0?"LAUNCH":"CANCEL"),21,i==launchConfirmationSelection_?accent:light);
        }
        centered(renderer_,451,controlHints("A SELECT   B CANCEL"),16,secondary);
    }
    if(accessibility_.visualFeedback && !inputFeedbackText_.empty() && SDL_GetTicks64()-inputFeedbackAt_<220){
        // A static edge flash occupies no text/artwork space. It has no moving
        // or fading components, including when reduced motion is enabled.
        const SDL_Color color=draw::foreground(accent);
        SDL_SetRenderDrawColor(renderer_,color.r,color.g,color.b,255);
        // Filled, inset strips avoid the Miyoo line renderer dropping the
        // closing/left edge at the framebuffer boundary.
        const SDL_Rect edges[]={{3,3,746,2},{3,555,746,2},{3,3,2,554},{747,3,2,554}};
        SDL_RenderFillRects(renderer_,edges,4);
    }
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
    SDL_RenderPresent(renderer_);
}

void Launcher::renderAccessibility(){
    renderBackground();SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    rectangle(renderer_,{24,12,704,540},{4,7,15,255});
    centered(renderer_,24,"CARTOS ACCESSIBILITY",26);
    if(overlay_==Overlay::AccessibilityResetConfirm){
        centered(renderer_,118,accessibilityResetBindingsOnly_?"RESET BUTTON MAPPINGS?":"RESET ACCESSIBILITY SETTINGS?",24);
        centered(renderer_,177,"Other CartOS settings and game lists are kept.",17,secondary);
        for(int i=0;i<2;++i){
            SDL_Rect row{80,244+i*70,592,48};
            if(overlaySelection_==i){rectangle(renderer_,row,{32,50,72,255});draw::selection(renderer_,row);}
            centered(renderer_,row.y+9,(overlaySelection_==i?"> ":"  ")+std::string(i==0?"RESET":"CANCEL"),22,overlaySelection_==i?accent:light);
        }
        centered(renderer_,446,controlHints("A SELECT   B CANCEL"),17,secondary);
    }else{
        static const char* headings[]={"VISUAL ACCESSIBILITY","CONTROLS AND NAVIGATION","DISPLAY PREFERENCES","AUDIO AND FEEDBACK"};
        centered(renderer_,67,accessibilityPage_<0?"CHOOSE A SECTION":headings[accessibilityPage_],18,secondary);
        const auto rows=accessibilityRows();
        accessibilitySelection_=std::max(0,std::min(static_cast<int>(rows.size())-1,accessibilitySelection_));
        const int rowHeight=accessibility_.largeControls?48:42;
        const int visible=std::min(static_cast<int>(rows.size()),310/rowHeight);
        const int first=std::max(0,accessibilitySelection_-visible+1);
        for(int i=first;i<std::min(static_cast<int>(rows.size()),first+visible);++i){
            const auto& row=rows[static_cast<size_t>(i)];const int y=111+(i-first)*rowHeight;
            SDL_Rect box{44,y-3,664,rowHeight-4};
            if(i==accessibilitySelection_){rectangle(renderer_,box,{32,50,72,255});draw::selection(renderer_,box);}
            const bool enabled=accessibilityRowEnabled(i);
            const SDL_Color nameColor=enabled?(i==accessibilitySelection_?accent:light):SDL_Color{180,180,180,150};
            const SDL_Color valueColor=enabled?(i==accessibilitySelection_?accent:secondary):SDL_Color{180,180,180,150};
            const int valueWidth=draw::textWidth(row.second,18);
            const std::string name=draw::fitText((i==accessibilitySelection_?"> ":"  ")+row.first,18,614-valueWidth);
            draw::text(renderer_,59,y+6,name,18,nameColor);
            draw::text(renderer_,686-valueWidth,y+6,row.second,18,valueColor);
        }
        std::string help;
        if(accessibilityPage_==1){
            if(accessibilitySelection_>=1 && accessibilitySelection_<9)help="ASSIGNMENTS SWAP AUTOMATICALLY TO PREVENT CONFLICTS";
            else if(accessibilitySelection_==0)help="CONFIRMATION ALSO APPLIES TO START AND SEARCH";
            else help="RESTORE ONLY BUTTON MAPPINGS";
        }else if(accessibilityPage_==3){
            if(accessibilitySelection_==0)help="ENABLE A QUIET TICK WHEN YOU NAVIGATE";
            else if(accessibilitySelection_==1)help=accessibility_.navigationSounds?
                "CARTOS TONES ONLY; SYSTEM VOLUME IS UNCHANGED":"TURN ON NAVIGATION SOUNDS TO ADJUST VOLUME";
            else if(accessibilitySelection_==3)help="SHORT VIBRATION PULSE; MOTOR HAS FIXED STRENGTH";
            else help=vibrationAvailable_?"BRIEF FEEDBACK ON ALL FOUR SCREEN EDGES":"SCREEN-EDGE FLASH; VIBRATION IS UNAVAILABLE";
        }else help=controlHints(accessibilityPage_<0?"UP/DOWN CHOOSE   A OPEN":
            accessibilityPage_==3 && !vibrationAvailable_?"VIBRATION IS UNAVAILABLE ON THIS DEVICE":
            accessibilityPage_==0?"CHANGES APPLY IMMEDIATELY INSIDE CARTOS":"UP/DOWN SCROLL   LEFT/RIGHT CHANGE");
        centered(renderer_,440,help,16,secondary);
        centered(renderer_,480,"HOLD SELECT + START   RESET CONTROLS",15,secondary);
        centered(renderer_,523,controlHints(accessibilityPage_<0?"A OPEN   B CLOSE":"A CHANGE   B BACK"),15,secondary);
    }
    if(!statusMessage_.empty() && SDL_GetTicks64()<statusUntil_)centered(renderer_,461,statusMessage_,14,accent);
    presentFrame();
}
