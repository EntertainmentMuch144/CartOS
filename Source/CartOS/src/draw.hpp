#pragma once
#include <SDL.h>
#include <string>

namespace draw {
void accessibilityStyle(int textSize,bool highContrast,bool enhancedSelection);
void textBackground(bool enabled,bool light=false);
void suppressTextBackground(bool suppressed);
int textHeight(int baseHeight);
SDL_Color foreground(SDL_Color color);
SDL_Color panelColor(SDL_Color color);
void artworkViewport(SDL_Rect viewport);
SDL_Rect artworkRect(SDL_Texture* artwork,SDL_Rect box,SDL_Rect viewport);
void selection(SDL_Renderer* renderer,SDL_Rect box);
std::string fitText(const std::string& text,int height,int width);
bool initializeFont(SDL_Renderer* renderer, const std::string& path);
void shutdownFont();
void background(SDL_Renderer* renderer, SDL_Texture* artwork, int width, int height);
void text(SDL_Renderer* r, int x, int y, const std::string& s, int pixelHeight, SDL_Color color);
int textWidth(const std::string& s, int pixelHeight);
void cartridge(SDL_Renderer* r, SDL_Rect box, const std::string& system,
               const std::string& title, int variant, bool selected,
               SDL_Texture* artwork);
void appIcon(SDL_Renderer* r, SDL_Rect box, SDL_Texture* artwork, int variant, bool onionMainUi=false,
             bool selected=false);
}
