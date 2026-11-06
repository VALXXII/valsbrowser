#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace NativeChrome {
struct FaviconImage {unsigned width=0,height=0,stride=0;std::vector<unsigned char> pixels;};
// Separate from COM: every document/image change invalidates outstanding work.
struct FaviconTicket {std::uint64_t epoch;std::wstring document,uri;};
struct FaviconState {
    std::uint64_t epoch=0;bool closed=false;std::wstring document,uri;
    std::shared_ptr<const FaviconImage> image;
    void Navigation(const std::wstring& next){++epoch;document=next;uri.clear();image.reset();}
    FaviconTicket Request(const std::wstring& next){++epoch;uri=next;image.reset();return {epoch,document,uri};}
    bool Current(const FaviconTicket& ticket)const{return !closed&&ticket.epoch==epoch&&ticket.document==document&&ticket.uri==uri;}
    bool Complete(const FaviconTicket& ticket,std::shared_ptr<const FaviconImage> next){if(!Current(ticket))return false;image=std::move(next);return true;}
    void Close(){closed=true;++epoch;image.reset();}
};
}
