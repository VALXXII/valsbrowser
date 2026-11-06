// Native validation through the peer's production ThorVG path (src/native-lottie.cpp).
// Dumps every sampled frame's alpha (side*side bytes) for tests/lottie-assets/compare_thorvg.py.
// No window, no WebView2. Usage: thorvg_dump.exe <repo-root> <out-dir>
#include <cstdio>
#include <fstream>
#include <string>
#include "../../src/native-lottie.h"
using namespace NativeChrome::Lottie;
int wmain(int argc,wchar_t** argv){
    if(argc!=3){std::fputs("usage: thorvg_dump <root> <out>\n",stderr);return 2;}
    const std::wstring root=argv[1],out=argv[2];int failures=0;
    for(const wchar_t* name:{L"copy-link",L"refresh"}){
        // One Asset per side: re-targeting a loaded Asset to a second side
        // currently fails in Sample() (reported to the integration peer).
        for(unsigned side:{16u,32u,64u}){
            Asset asset;
            if(!asset.Load(root+L"/assets/lottie/"+name+L".json")){std::fwprintf(stderr,L"FAIL: ThorVG load %ls\n",name);++failures;break;}
            const unsigned count=asset.FrameCount();
            std::wprintf(L"PASS ThorVG loaded %ls @%upx: %u frames, %.3f s\n",name,side,count,asset.Duration());
            for(unsigned i=0;i<count;++i){
                auto frame=asset.Sample(count>1?double(i)/(count-1):0,side);
                if(!frame||frame->bgra.size()!=side*side){std::fwprintf(stderr,L"FAIL: sample %ls %u f%u\n",name,side,i);++failures;continue;}
                std::string bytes(side*side,'\0');
                for(size_t k=0;k<bytes.size();++k)bytes[k]=char(frame->bgra[k]>>24);
                wchar_t file[64];std::swprintf(file,64,L"/%ls-%u-f%02u.a8",name,side,i);
                std::ofstream((out+file).c_str(),std::ios::binary).write(bytes.data(),bytes.size());
            }
        }
    }
    return failures?1:0;
}
