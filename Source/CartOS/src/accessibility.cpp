#include "accessibility.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace accessibility {
namespace {
const char* bindingKeys[]={"confirm","back","refresh","search","left","right","up","down"};
const Uint8 controllerButtons[]={SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_X,SDL_CONTROLLER_BUTTON_Y,SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,SDL_CONTROLLER_BUTTON_DPAD_UP,SDL_CONTROLLER_BUTTON_DPAD_DOWN};
int integer(const std::string& value,int minimum,int maximum,int fallback){
    if(value.empty() || value.find_first_not_of("0123456789")!=std::string::npos) return fallback;
    if(value.size()>3) return fallback;
    const int result=std::stoi(value);
    return result>=minimum && result<=maximum?result:fallback;
}
int keyButton(SDL_Keycode key){
    switch(key){
        case SDLK_SPACE:return Confirm;
        case SDLK_LCTRL:return Back;
        case SDLK_LSHIFT:case SDLK_x:return Refresh;
        case SDLK_LALT:case SDLK_y:return Search;
        case SDLK_LEFT:case SDLK_a:return Left;
        case SDLK_RIGHT:case SDLK_d:return Right;
        case SDLK_UP:case SDLK_w:return Up;
        case SDLK_DOWN:case SDLK_s:return Down;
        default:return -1;
    }
}
}

bool Preferences::read(const std::string& key,const std::string& value){
    if(key=="a11y_text_size")textSize=integer(value,0,2,1);
    else if(key=="a11y_high_contrast")highContrast=value=="1";
    else if(key=="a11y_reduced_motion")reducedMotion=value=="1";
    else if(key=="a11y_dimming")dimming=integer(value,0,3,0);
    else if(key=="a11y_confirm_launch")confirmLaunch=value=="1";
    else if(key=="a11y_simple_navigation")return true; // Retired: A always previews; START launches.
    else if(key=="a11y_large_artwork")largeArtwork=value=="1";
    else if(key=="a11y_large_controls")largeControls=value=="1";
    else if(key=="a11y_hide_secondary")hideSecondary=value=="1";
    else if(key=="a11y_scroll_speed")scrollSpeed=integer(value,0,2,1);
    else if(key=="a11y_selection_indicator")enhancedSelection=value=="1";
    else if(key=="a11y_navigation_sounds")navigationSounds=value=="1";
    else if(key=="a11y_sound_volume")soundVolume=integer(value,0,100,50);
    else if(key=="a11y_visual_feedback")visualFeedback=value=="1";
    else if(key=="a11y_vibration")vibration=value=="1";
    else{
        for(int i=0;i<ButtonCount;++i)if(key==std::string("a11y_bind_")+bindingKeys[i]){
            bindings[static_cast<size_t>(i)]=integer(value,i<4?0:4,i<4?3:7,-1);
            return true;
        }
        return false;
    }
    return true;
}

void Preferences::validate(){
    textSize=std::max(0,std::min(2,textSize));
    dimming=std::max(0,std::min(3,dimming));
    scrollSpeed=std::max(0,std::min(2,scrollSpeed));
    soundVolume=std::max(0,std::min(100,soundVolume));
    for(int first:{0,4}){
        std::array<bool,4> used{{false,false,false,false}};
        bool valid=true;
        for(int i=first;i<first+4;++i){
            const int binding=bindings[static_cast<size_t>(i)]-first;
            if(binding<0 || binding>=4 || used[static_cast<size_t>(binding)]){valid=false;break;}
            used[static_cast<size_t>(binding)]=true;
        }
        if(!valid)for(int i=first;i<first+4;++i)bindings[static_cast<size_t>(i)]=i;
    }
}

void Preferences::changeBinding(int logical,int direction){
    if(logical<0 || logical>=ButtonCount || direction==0)return;
    const int first=logical<4?0:4;
    const int current=bindings[static_cast<size_t>(logical)];
    const int target=first+(current-first+(direction<0?3:1))%4;
    for(int i=first;i<first+4;++i)if(bindings[static_cast<size_t>(i)]==target){
        std::swap(bindings[static_cast<size_t>(i)],bindings[static_cast<size_t>(logical)]);
        break;
    }
}
void Preferences::resetBindings(){for(int i=0;i<ButtonCount;++i)bindings[static_cast<size_t>(i)]=i;}

std::vector<std::pair<std::string,std::string>> Preferences::entries() const{
    std::vector<std::pair<std::string,std::string>> result{
        {"a11y_text_size",std::to_string(textSize)},{"a11y_high_contrast",highContrast?"1":"0"},
        {"a11y_reduced_motion",reducedMotion?"1":"0"},{"a11y_dimming",std::to_string(dimming)},
        {"a11y_confirm_launch",confirmLaunch?"1":"0"},
        {"a11y_large_artwork",largeArtwork?"1":"0"},{"a11y_large_controls",largeControls?"1":"0"},
        {"a11y_hide_secondary",hideSecondary?"1":"0"},{"a11y_scroll_speed",std::to_string(scrollSpeed)},
        {"a11y_selection_indicator",enhancedSelection?"1":"0"},{"a11y_navigation_sounds",navigationSounds?"1":"0"},
        {"a11y_sound_volume",std::to_string(soundVolume)},{"a11y_visual_feedback",visualFeedback?"1":"0"},
        {"a11y_vibration",vibration?"1":"0"}};
    for(int i=0;i<ButtonCount;++i)result.emplace_back(std::string("a11y_bind_")+bindingKeys[i],std::to_string(bindings[static_cast<size_t>(i)]));
    return result;
}
Uint64 Preferences::repeatDelay() const{return scrollSpeed==0?450:(scrollSpeed==2?300:360);}
Uint64 Preferences::repeatInterval() const{return scrollSpeed==0?280:(scrollSpeed==2?110:180);}

bool updateConfig(const std::string& path,const std::vector<std::pair<std::string,std::string>>& values,const std::vector<std::string>& replacePrefixes){
    struct stat info{};
    const bool exists=stat(path.c_str(),&info)==0;
    if(exists && (!S_ISREG(info.st_mode) || info.st_size>1024*1024))return false;
    std::map<std::string,std::string> pending;
    for(const auto& item:values)pending[item.first]=item.second;
    std::ifstream source(path,std::ios::binary);
    if(exists && !source)return false;
    const std::string original{std::istreambuf_iterator<char>(source),std::istreambuf_iterator<char>()};
    std::istringstream input(original);
    std::ostringstream output;
    std::string line;
    std::map<std::string,bool> written;
    while(std::getline(input,line)){
        if(!line.empty() && line.back()=='\r')line.pop_back();
        const size_t equal=line.find('=');
        const std::string key=equal==std::string::npos?"":line.substr(0,equal);
        const auto replacement=pending.find(key);
        const bool obsolete=std::any_of(replacePrefixes.begin(),replacePrefixes.end(),[&](const std::string& prefix){return key.compare(0,prefix.size(),prefix)==0;});
        if(replacement==pending.end()){if(!obsolete)output<<line<<'\n';}
        else if(!written[key]){output<<key<<'='<<replacement->second<<'\n';written[key]=true;}
    }
    for(const auto& item:values)if(!written[item.first]){
        output<<item.first<<'='<<item.second<<'\n';written[item.first]=true;
    }
    const std::string text=output.str();
    if(text==original)return true; // Avoid an SD-card sync on an unchanged boot.
    const std::string temporary=path+".tmp";
    FILE* file=std::fopen(temporary.c_str(),"wb");
    if(!file)return false;
    bool success=std::fwrite(text.data(),1,text.size(),file)==text.size();
    if(std::fflush(file)!=0 || fsync(fileno(file))!=0)success=false;
    if(std::fclose(file)!=0)success=false;
    if(success && std::rename(temporary.c_str(),path.c_str())==0)return true;
    unlink(temporary.c_str());return false;
}

int physicalButton(const SDL_Event& event){
    if(event.type==SDL_CONTROLLERBUTTONDOWN || event.type==SDL_CONTROLLERBUTTONUP){
        for(int i=0;i<ButtonCount;++i)if(event.cbutton.button==controllerButtons[i])return i;
    }else if(event.type==SDL_KEYDOWN || event.type==SDL_KEYUP)return keyButton(event.key.keysym.sym);
    else if(event.type==SDL_JOYBUTTONDOWN || event.type==SDL_JOYBUTTONUP){
        static const int raw[]={Up,Down,Left,Right,Confirm,Back,Refresh,Search};
        if(event.jbutton.button<8)return raw[event.jbutton.button];
    }
    return -1;
}

SDL_Event translate(const SDL_Event& event,const Preferences& preferences){
    SDL_Event result=event;
    const int physical=physicalButton(event);
    if(physical>=0){
        if(event.type==SDL_KEYDOWN && event.key.repeat){result.type=SDL_USEREVENT;return result;}
        int logical=physical;
        for(int i=0;i<ButtonCount;++i)if(preferences.bindings[static_cast<size_t>(i)]==physical){logical=i;break;}
        const bool pressed=event.type==SDL_KEYDOWN || event.type==SDL_CONTROLLERBUTTONDOWN || event.type==SDL_JOYBUTTONDOWN;
        result.type=pressed?SDL_CONTROLLERBUTTONDOWN:SDL_CONTROLLERBUTTONUP;
        result.cbutton.button=controllerButtons[logical];
    }else if((event.type==SDL_JOYBUTTONDOWN || event.type==SDL_JOYBUTTONUP) &&
             (event.jbutton.button==8 || event.jbutton.button==9)){
        result.type=event.type==SDL_JOYBUTTONDOWN?SDL_CONTROLLERBUTTONDOWN:SDL_CONTROLLERBUTTONUP;
        result.cbutton.button=event.jbutton.button==8?SDL_CONTROLLER_BUTTON_LEFTSHOULDER:SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    }else if(event.type==SDL_JOYHATMOTION){
        static const Uint8 hats[]={SDL_HAT_LEFT,SDL_HAT_RIGHT,SDL_HAT_UP,SDL_HAT_DOWN};
        result.jhat.value=SDL_HAT_CENTERED;
        for(int logical=Left;logical<ButtonCount;++logical)
            if(event.jhat.value&hats[preferences.bindings[static_cast<size_t>(logical)]-Left])
                result.jhat.value|=hats[logical-Left];
    }
    return result;
}
const char* buttonName(int button){
    static const char* names[]={"A","B","X","Y","LEFT","RIGHT","UP","DOWN"};
    return button>=0 && button<ButtonCount?names[button]:"?";
}
const char* actionName(int button){
    static const char* names[]={"Confirm / Open","Back","Refresh","Search","Move left","Move right","Move up","Move down"};
    return button>=0 && button<ButtonCount?names[button]:"Button";
}
std::string hints(const std::string& text,const Preferences& preferences){
    std::string result;
    for(size_t i=0;i<text.size();){
        bool replaced=false;
        for(int action=0;action<ButtonCount;++action){
            const std::string token=buttonName(action);
            const size_t end=i+token.size();
            const bool left=i==0 || text[i-1]==' ' || text[i-1]=='/';
            const bool right=end<=text.size() && (end==text.size() || text[end]==' ' || text[end]=='/' || text[end]==':');
            if(left && right && text.compare(i,token.size(),token)==0){
                result+=buttonName(preferences.bindings[static_cast<size_t>(action)]);
                i=end;replaced=true;break;
            }
        }
        if(!replaced)result.push_back(text[i++]);
    }
    return result;
}

std::array<Sint16,3840> navigationTone(int volume){
    std::array<Sint16,3840> samples{};
    const double amplitude=4800.0*std::max(0,std::min(100,volume))/100.0;
    for(size_t i=0;i<1920;++i){
        // 40 ms soft tick, with smooth attack/release to prevent clicks.
        const double envelope=0.5-0.5*std::cos(6.283185307179586*i/1919.0);
        const double phase=6.283185307179586*660.0*i/48000.0;
        const Sint16 value=static_cast<Sint16>(amplitude*envelope*(0.85*std::sin(phase)+0.15*std::sin(phase*2.0)));
        samples[i*2]=samples[i*2+1]=value;
    }
    return samples;
}

bool NavigationAudio::play(int volume){
    if(volume<=0)return true;
    if(!device_){
        if(!initialized_){
            if(SDL_InitSubSystem(SDL_INIT_AUDIO)!=0)return false;
            initialized_=true;
        }
        SDL_AudioSpec desired{};desired.freq=48000;desired.format=AUDIO_S16SYS;desired.channels=2;desired.samples=512;
        device_=SDL_OpenAudioDevice(nullptr,0,&desired,nullptr,0);
        if(!device_)return false;
        SDL_PauseAudioDevice(device_,0);
    }
    if(SDL_GetQueuedAudioSize(device_)>7680)return true;
    const auto samples=navigationTone(volume);
    return SDL_QueueAudio(device_,samples.data(),static_cast<Uint32>(samples.size()*sizeof(Sint16)))==0;
}
void NavigationAudio::close(){
    if(device_){SDL_ClearQueuedAudio(device_);SDL_CloseAudioDevice(device_);device_=0;}
    if(initialized_){SDL_QuitSubSystem(SDL_INIT_AUDIO);initialized_=false;}
}
}
