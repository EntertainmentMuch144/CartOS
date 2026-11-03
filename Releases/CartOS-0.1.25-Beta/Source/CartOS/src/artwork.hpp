#pragma once
#include <SDL.h>
#include <string>

namespace artwork {
SDL_Texture* loadPng(SDL_Renderer* renderer, const std::string& path, int maxTextureSide = 512,
                     std::string* failureReason = nullptr);
}
