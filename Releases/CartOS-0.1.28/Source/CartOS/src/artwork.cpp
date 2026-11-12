#include "artwork.hpp"
#include <png.h>
#include <algorithm>
#include <cstdio>
#include <limits>
#include <new>
#include <vector>

namespace artwork {
SDL_Texture* loadPng(SDL_Renderer* renderer, const std::string& path, int maxTextureSide,
                     std::string* failureReason) {
    if(failureReason) failureReason->clear();
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, path.c_str())) {
        if(failureReason) *failureReason=image.message[0]?image.message:"PNG header could not be read";
        return nullptr;
    }

    if (image.width == 0 || image.height == 0 || image.width > 2048 || image.height > 2048) {
        if(failureReason) *failureReason="PNG dimensions are zero or exceed 2048 pixels";
        png_image_free(&image);
        return nullptr;
    }

    image.format = PNG_FORMAT_RGBA;
    const png_alloc_size_t bytes = PNG_IMAGE_SIZE(image);
    constexpr png_alloc_size_t kMaxDecodedBytes=6u*1024u*1024u;
    if (bytes == 0 || bytes > std::numeric_limits<size_t>::max() || bytes > kMaxDecodedBytes) {
        if(failureReason) *failureReason="decoded PNG exceeds CartOS's 6 MiB artwork limit";
        std::fprintf(stderr,"Skipping oversized artwork: %s (%ux%u, %lu bytes)\n",
                     path.c_str(),image.width,image.height,static_cast<unsigned long>(bytes));
        png_image_free(&image);
        return nullptr;
    }

    std::vector<png_byte> pixels;
    try {
        pixels.resize(static_cast<size_t>(bytes));
    } catch (const std::bad_alloc&) {
        if(failureReason) *failureReason="not enough memory to decode PNG";
        std::fprintf(stderr,"Artwork too large for available memory: %s (%ux%u)\n",
                     path.c_str(),image.width,image.height);
        png_image_free(&image);
        return nullptr;
    }
    if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr)) {
        if(failureReason) *failureReason=image.message[0]?image.message:"PNG pixel data could not be decoded";
        png_image_free(&image);
        return nullptr;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels.data(), static_cast<int>(image.width), static_cast<int>(image.height),
        32, static_cast<int>(image.width * 4), SDL_PIXELFORMAT_RGBA32);
    SDL_Surface* uploadSurface=surface;
    SDL_Surface* reducedSurface=nullptr;
    if(surface && maxTextureSide>0 &&
       (image.width>static_cast<png_uint_32>(maxTextureSide) || image.height>static_cast<png_uint_32>(maxTextureSide))){
        uploadSurface=nullptr;
        const double scale=std::min(static_cast<double>(maxTextureSide)/image.width,
                                    static_cast<double>(maxTextureSide)/image.height);
        const int width=std::max(1,static_cast<int>(image.width*scale));
        const int height=std::max(1,static_cast<int>(image.height*scale));
        reducedSurface=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_RGBA32);
        if(reducedSurface && SDL_BlitScaled(surface,nullptr,reducedSurface,nullptr)==0)
            uploadSurface=reducedSurface;
        else if(failureReason)
            *failureReason=SDL_GetError()[0]?SDL_GetError():"could not scale PNG surface";
    }
    SDL_Texture* texture = uploadSurface ? SDL_CreateTextureFromSurface(renderer, uploadSurface) : nullptr;
    if(!texture && failureReason && failureReason->empty())
        *failureReason=SDL_GetError()[0]?SDL_GetError():"could not create SDL texture from PNG";
    if (reducedSurface) SDL_FreeSurface(reducedSurface);
    if (surface) SDL_FreeSurface(surface);
    png_image_free(&image);
    if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}
}
