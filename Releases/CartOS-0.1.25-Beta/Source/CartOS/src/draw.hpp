#pragma once
#include <SDL.h>
#include <string>

namespace draw {
bool initializeFont(SDL_Renderer* renderer, const std::string& path);
void shutdownFont();
void background(SDL_Renderer* renderer, SDL_Texture* artwork, int width, int height);
void text(SDL_Renderer* r, int x, int y, const std::string& s, int pixelHeight, SDL_Color color);
int textWidth(const std::string& s, int pixelHeight);
void cartridge(SDL_Renderer* r, SDL_Rect box, const std::string& system,
               const std::string& title, int variant, bool selected,
               SDL_Texture* artwork);
void appIcon(SDL_Renderer* r, SDL_Rect box, SDL_Texture* artwork, int variant, bool onionMainUi=false);
}
