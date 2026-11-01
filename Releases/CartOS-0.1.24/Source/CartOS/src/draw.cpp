#include "draw.hpp"
#include "artwork.hpp"
#include "generated_font.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>

namespace {
SDL_Texture* fontAtlas=nullptr;

uint32_t nextCodepoint(const std::string& text,size_t& pos){
    const unsigned char first=static_cast<unsigned char>(text[pos++]);
    if(first<0x80) return first;
    int continuation=0;
    uint32_t value=0;
    if((first&0xE0)==0xC0){continuation=1;value=first&0x1F;}
    else if((first&0xF0)==0xE0){continuation=2;value=first&0x0F;}
    else if((first&0xF8)==0xF0){continuation=3;value=first&0x07;}
    else return '?';
    if(pos+static_cast<size_t>(continuation)>text.size()) return '?';
    for(int i=0;i<continuation;++i){
        const unsigned char next=static_cast<unsigned char>(text[pos]);
        if((next&0xC0)!=0x80) return '?';
        value=(value<<6)|(next&0x3F);
        ++pos;
    }
    if(value>0x10FFFF || (value>=0xD800 && value<=0xDFFF)) return '?';
    return value;
}

const fontdata::Glyph& glyphFor(uint32_t codepoint){
    const auto begin=std::begin(fontdata::kGlyphs);
    const auto end=std::end(fontdata::kGlyphs);
    const auto found=std::lower_bound(begin,end,codepoint,[](const fontdata::Glyph& glyph,uint32_t value){return glyph.codepoint<value;});
    if(found!=end && found->codepoint==codepoint) return *found;
    const auto fallback=std::lower_bound(begin,end,static_cast<uint32_t>('?'),[](const fontdata::Glyph& glyph,uint32_t value){return glyph.codepoint<value;});
    return *fallback;
}

void setColor(SDL_Renderer* renderer,SDL_Color color){
    SDL_SetRenderDrawColor(renderer,color.r,color.g,color.b,color.a);
}

std::string fitLabel(const std::string& value,int pixelHeight,int maxWidth){
    if(draw::textWidth(value,pixelHeight)<=maxWidth) return value;
    const std::string ellipsis="...";
    std::string result;
    for(size_t pos=0;pos<value.size();){
        const size_t start=pos;
        const unsigned char lead=static_cast<unsigned char>(value[pos]);
        size_t length=lead<0x80?1:(lead&0xE0)==0xC0?2:(lead&0xF0)==0xE0?3:(lead&0xF8)==0xF0?4:1;
        if(pos+length>value.size()) length=1;
        result.append(value,start,length);
        pos+=length;
        if(draw::textWidth(result+ellipsis,pixelHeight)>maxWidth){
            result.resize(start);
            break;
        }
    }
    return result.empty()?ellipsis:result+ellipsis;
}

void roundedRect(SDL_Renderer* renderer,SDL_Rect box,int radius,SDL_Color color){
    if(box.w<=0 || box.h<=0) return;
    radius=std::max(0,std::min(radius,std::min(box.w,box.h)/2));
    setColor(renderer,color);
    if(radius==0){SDL_RenderFillRect(renderer,&box);return;}
    for(int y=0;y<box.h;++y){
        const int fromTop=y;
        const int fromBottom=box.h-1-y;
        int inset=0;
        const int edge=std::min(fromTop,fromBottom);
        if(edge<radius){
            const double dy=static_cast<double>(radius-edge)-0.5;
            inset=radius-static_cast<int>(std::floor(std::sqrt(std::max(0.0,static_cast<double>(radius*radius)-dy*dy))));
        }
        SDL_Rect line{box.x+inset,box.y+y,std::max(0,box.w-inset*2),1};
        if(line.w>0) SDL_RenderFillRect(renderer,&line);
    }
}

SDL_Rect fitTexture(SDL_Texture* texture,SDL_Rect bounds){
    int imageW=0,imageH=0;
    SDL_QueryTexture(texture,nullptr,nullptr,&imageW,&imageH);
    if(imageW<=0 || imageH<=0) return bounds;
    const double scale=std::min(static_cast<double>(bounds.w)/imageW,static_cast<double>(bounds.h)/imageH);
    SDL_Rect result{0,0,std::max(1,static_cast<int>(imageW*scale)),std::max(1,static_cast<int>(imageH*scale))};
    result.x=bounds.x+(bounds.w-result.w)/2;
    result.y=bounds.y+(bounds.h-result.h)/2;
    return result;
}
}

namespace draw {
bool initializeFont(SDL_Renderer* renderer,const std::string& path){
    shutdownFont();
    // Glyph coordinates refer to the full atlas dimensions in generated_font.hpp.
    // Keep the 1024x576 font atlas unscaled; artwork images remain bounded at 512px.
    fontAtlas=artwork::loadPng(renderer,path,0);
    return fontAtlas!=nullptr;
}

void shutdownFont(){
    if(fontAtlas){SDL_DestroyTexture(fontAtlas);fontAtlas=nullptr;}
}

void background(SDL_Renderer* renderer,SDL_Texture* artwork,int width,int height){
    if(!renderer || !artwork || width<=0 || height<=0) return;
    int imageW=0,imageH=0;
    const int queryResult=SDL_QueryTexture(artwork,nullptr,nullptr,&imageW,&imageH);
    if(queryResult!=0 || imageW<=0 || imageH<=0) return;

    // Cover the logical screen without stretching: crop the center of the
    // source image to match the device aspect ratio.
    const double sourceAspect=static_cast<double>(imageW)/imageH;
    const double targetAspect=static_cast<double>(width)/height;
    SDL_Rect source{0,0,imageW,imageH};
    if(sourceAspect>targetAspect){
        source.w=std::max(1,static_cast<int>(std::lround(imageH*targetAspect)));
        source.x=(imageW-source.w)/2;
    }else if(sourceAspect<targetAspect){
        source.h=std::max(1,static_cast<int>(std::lround(imageW/targetAspect)));
        source.y=(imageH-source.h)/2;
    }
    const SDL_Rect destination{0,0,width,height};
    // Background PNGs in CartOS are opaque. Avoid the mmiyoo renderer's alpha
    // blend path here so a valid texture is copied directly to the screen.
    const int blendResult=SDL_SetTextureBlendMode(artwork,SDL_BLENDMODE_NONE);
    const char* blendError=blendResult==0?"":SDL_GetError();
    SDL_ClearError();
    const int copyResult=SDL_RenderCopy(renderer,artwork,&source,&destination);
    const char* copyError=copyResult==0?"":SDL_GetError();

    // This diagnostic is written once per launch. Texture creation alone does
    // not prove the device renderer accepted the draw call.
    static bool reportedDraw=false;
    if(!reportedDraw){
        SDL_RendererInfo info{};
        const int infoResult=SDL_GetRendererInfo(renderer,&info);
        int outputW=0,outputH=0;
        const int outputResult=SDL_GetRendererOutputSize(renderer,&outputW,&outputH);
        std::ofstream log("/mnt/SDCARD/App/CartOS/background-debug.log",std::ios::out|std::ios::app);
        if(log){
            log<<"DRAW: query="<<queryResult
               <<" texture="<<imageW<<'x'<<imageH
               <<" blend_mode_none="<<blendResult;
            if(blendError[0]) log<<" blend_error="<<blendError;
            log<<" copy="<<copyResult;
            if(copyError[0]) log<<" copy_error="<<copyError;
            if(infoResult==0)
                log<<" renderer="<<(info.name?info.name:"unknown")
                   <<" max_texture="<<info.max_texture_width<<'x'<<info.max_texture_height;
            if(outputResult==0) log<<" output="<<outputW<<'x'<<outputH;
            log<<'\n';
        }
        reportedDraw=true;
    }
}

int textWidth(const std::string& value,int pixelHeight){
    if(pixelHeight<=0) return 0;
    const double scale=static_cast<double>(pixelHeight)/fontdata::kBaseSize;
    int units=0;
    for(size_t pos=0;pos<value.size();){
        const uint32_t codepoint=nextCodepoint(value,pos);
        units+=glyphFor(codepoint).advance;
    }
    return static_cast<int>(std::ceil(units*scale));
}

void text(SDL_Renderer* renderer,int x,int y,const std::string& value,int pixelHeight,SDL_Color color){
    if(!fontAtlas || pixelHeight<=0) return;
    const double scale=static_cast<double>(pixelHeight)/fontdata::kBaseSize;
    SDL_SetTextureColorMod(fontAtlas,color.r,color.g,color.b);
    SDL_SetTextureAlphaMod(fontAtlas,color.a);
    int pen=x;
    const int baseline=y+static_cast<int>(std::lround(21.0*scale));
    for(size_t pos=0;pos<value.size();){
        const fontdata::Glyph& glyph=glyphFor(nextCodepoint(value,pos));
        if(glyph.w>0 && glyph.h>0){
            SDL_Rect source{glyph.x,glyph.y,glyph.w,glyph.h};
            SDL_Rect destination{
                pen+static_cast<int>(std::lround(glyph.bearingX*scale)),
                baseline+static_cast<int>(std::lround(glyph.bearingY*scale)),
                std::max(1,static_cast<int>(std::lround(glyph.w*scale))),
                std::max(1,static_cast<int>(std::lround(glyph.h*scale)))
            };
            SDL_RenderCopy(renderer,fontAtlas,&source,&destination);
        }
        pen+=static_cast<int>(std::lround(glyph.advance*scale));
    }
    SDL_SetTextureColorMod(fontAtlas,255,255,255);
    SDL_SetTextureAlphaMod(fontAtlas,255);
}

void cartridge(SDL_Renderer* renderer,SDL_Rect box,const std::string& system,const std::string& title,int variant,bool selected,SDL_Texture* artwork){
    if(artwork){
        SDL_Rect image=fitTexture(artwork,box);
        SDL_RenderCopy(renderer,artwork,nullptr,&image);
        return;
    }
    const SDL_Color shells[]={{53,57,65,255},{45,54,58,255},{61,51,56,255},{52,54,47,255}};
    const SDL_Color accents[]={{232,179,49,255},{72,160,193,255},{196,85,87,255},{120,177,111,255}};
    roundedRect(renderer,box,std::max(8,box.w/12),{17,18,20,255});
    SDL_Rect body{box.x+3,box.y+3,box.w-6,box.h-6};
    roundedRect(renderer,body,std::max(6,box.w/14),shells[variant%4]);
    SDL_Rect label{box.x+box.w/6,box.y+box.h/6,box.w*2/3,box.h/2};
    roundedRect(renderer,label,std::max(4,box.w/18),accents[variant%4]);
    const int titleSize=std::max(12,std::min(20,box.w/11));
    const int maxLabelWidth=std::max(1,box.w-18);
    const std::string faceTitle=fitLabel(title,titleSize,maxLabelWidth);
    text(renderer,box.x+(box.w-draw::textWidth(faceTitle,titleSize))/2,box.y+box.h*2/3,faceTitle,titleSize,{245,245,245,255});
    (void)system;
    (void)selected;
}

void appIcon(SDL_Renderer* renderer,SDL_Rect box,SDL_Texture* artwork,int variant,bool onionMainUi){
    const int size=std::min(box.w,box.h);
    SDL_Rect panel{box.x+(box.w-size)/2,box.y+(box.h-size)/2,size,size};
    roundedRect(renderer,panel,std::max(14,size/9),{56,61,70,255});
    SDL_Rect inset{panel.x+4,panel.y+4,panel.w-8,panel.h-8};
    roundedRect(renderer,inset,std::max(10,size/11),{36,39,48,255});
    const int iconSize=onionMainUi?size*3/4:size*5/9;
    SDL_Rect iconArea{panel.x+(size-iconSize)/2,panel.y+(size-iconSize)/2,iconSize,iconSize};
    if(artwork){
        const SDL_Rect image=fitTexture(artwork,iconArea);
        SDL_RenderCopy(renderer,artwork,nullptr,&image);
    }else{
        const SDL_Color accents[]={{232,179,49,255},{72,160,193,255},{196,85,87,255},{120,177,111,255}};
        roundedRect(renderer,iconArea,std::max(8,size/12),accents[variant%4]);
    }
}
}
