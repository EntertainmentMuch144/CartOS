#pragma once
#include <SDL.h>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace accessibility {
enum Button { Confirm, Back, Refresh, Search, Left, Right, Up, Down, ButtonCount };
struct Preferences {
    int textSize=1; // Small, Default, Large
    bool highContrast=false;
    bool reducedMotion=false;
    int dimming=0; // None, Light, Medium, Strong; never system brightness
    bool confirmLaunch=false;
    bool largeArtwork=false;
    bool largeControls=false;
    bool hideSecondary=false;
    int scrollSpeed=1;
    bool enhancedSelection=false;
    bool navigationSounds=false;
    int soundVolume=50;
    bool visualFeedback=false;
    bool vibration=false;
    std::array<int,ButtonCount> bindings{{0,1,2,3,4,5,6,7}};
    bool read(const std::string& key,const std::string& value);
    void validate();
    void changeBinding(int logical,int direction);
    void resetBindings();
    std::vector<std::pair<std::string,std::string>> entries() const;
    Uint64 repeatDelay() const;
    Uint64 repeatInterval() const;
};
// Updates only the supplied keys, retaining unrelated preferences and comments.
bool updateConfig(const std::string& path,const std::vector<std::pair<std::string,std::string>>& values,
                  const std::vector<std::string>& replacePrefixes={});
int physicalButton(const SDL_Event& event);
SDL_Event translate(const SDL_Event& event,const Preferences& preferences);
const char* buttonName(int button);
const char* actionName(int button);
std::string hints(const std::string& text,const Preferences& preferences);

std::array<Sint16,3840> navigationTone(int volume);

class NavigationAudio {
public:
    bool play(int volume);
    void close();
    bool opened() const { return device_!=0; }
private:
    SDL_AudioDeviceID device_=0;
    bool initialized_=false;
};
}
