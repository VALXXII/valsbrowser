// Our print window (#121) wired to the engine. Part of main.cpp (included once, after
// SavePageAsPdf): it uses the browser's globals.
//
// Every way to print opens src/print-dialog.h instead of the engine's preview: Ctrl+P (in the page
// too: Key() returns PrintActive and the accelerator is marked handled), the tab menu, the page
// menu (the engine's « Imprimer » item is replaced by ours), and window.print() from the top
// document of a page (a document-created script replaces window.print by a message, PrintScript).
//
// Engine calls (ICoreWebView2_16, SDK 1.0.4314.35), one at a time per webview as the SDK requires
// (a queue, PrintOps): PrintToPdfStream for the preview (all pages; the page ranges only pick which
// sheets are shown) and for « Enregistrer » (with the ranges: the file is what the sheet showed,
// written off the UI thread into the download folder, then listed like a finished download, as
// SavePageAsPdf); Print for a printer (its own paper: MediaSize default). The preview's PDF is
// rasterized page by page off the UI thread (src/print-preview.h).
//
// Printers: EnumPrintersW (level 4: names only, no driver call) and, off the UI thread, each
// printer's default paper (DocumentPropertiesW / DeviceCapabilitiesW); winspool is loaded at run
// time (test hosts link the UI libraries only). « Enregistrer en PDF » is first and is the first
// choice (as drawn); the last destination is kept for the session. The PDF's paper is the
// Windows region's (LOCALE_IPAPERSIZE: A4 in France).
//
// A document the engine cannot turn into a PDF stays in our window with « Aperçu indisponible »
// (Paper « Impression · états », state 6): it still prints. Only an engine without ICoreWebView2_16 /
// CreatePrintSettings, or Windows without Windows.Data.Pdf, keeps the engine's preview (ShowPrintUI).
//
// Printing (states 7 and 8): our window closes and the C8 status toast takes over (src/context-toast.h):
// « Impression en cours… » with the printer and « Annuler », then nothing on success, or « Impression
// impossible » with the reason and « Réessayer », which stays until acted on. The engine's Print() cannot be
// stopped: « Annuler » deletes the job from the printer's Windows queue once it is there (same printer,
// same document name, submitted since « Imprimer », by this user).
//
// QA only (--workspace-test or --smoke-test): BROWSER_PRINT_QA_PRINTERS="A;B" replaces the system
// printers by fake ones and BROWSER_PRINT_QA_SINK=<folder> receives, instead of any real printer,
// the job's PDF and a print-job.txt of its settings; BROWSER_PRINT_QA_DELAY=<ms> holds the job that
// long (the progress toast stays on screen), BROWSER_PRINT_QA_FAIL=offline|other makes it fail,
// BROWSER_PRINT_QA_NO_PREVIEW=1 refuses the preview (state 6), BROWSER_PRINT_QA_PREVIEW_DELAY=<ms> holds it (state
// 5). No test ever reaches a real printer.
#pragma once
#include "print-dialog.h"
#include "print-preview.h"
#include <winspool.h>

GUID_TEXT(ICoreWebView2PrintToPdfStreamCompletedHandler,"{4c9f8229-8f93-444f-a711-2c0dfd6359d5}")
GUID_TEXT(ICoreWebView2PrintCompletedHandler,"{8fd80075-ed08-42db-8570-f5d14977461e}")

namespace PD=NativeChrome::PrintDialog;
PD::Dialog printDialog;
PrintPreview::Renderer printRenderer;

// ---- printers (winspool at run time) ----------------------------------------------------------
namespace PrintSpool {
struct Api {
    BOOL(WINAPI* enumPrinters)(DWORD,LPWSTR,DWORD,LPBYTE,DWORD,LPDWORD,LPDWORD)=nullptr;
    BOOL(WINAPI* openPrinter)(LPWSTR,LPHANDLE,LPPRINTER_DEFAULTSW)=nullptr;
    BOOL(WINAPI* closePrinter)(HANDLE)=nullptr;
    LONG(WINAPI* documentProperties)(HWND,HANDLE,LPWSTR,PDEVMODEW,PDEVMODEW,DWORD)=nullptr;
    int(WINAPI* deviceCapabilities)(LPCWSTR,LPCWSTR,WORD,LPWSTR,const DEVMODEW*)=nullptr;
    BOOL(WINAPI* enumJobs)(HANDLE,DWORD,DWORD,DWORD,LPBYTE,DWORD,LPDWORD,LPDWORD)=nullptr;
    BOOL(WINAPI* setJob)(HANDLE,DWORD,DWORD,LPBYTE,DWORD)=nullptr;
    static const Api& Get(){
        static const Api api=[]{Api a;HMODULE m=LoadLibraryExW(L"winspool.drv",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!m)return a;
            a.enumPrinters=reinterpret_cast<decltype(a.enumPrinters)>(reinterpret_cast<void*>(GetProcAddress(m,"EnumPrintersW")));
            a.openPrinter=reinterpret_cast<decltype(a.openPrinter)>(reinterpret_cast<void*>(GetProcAddress(m,"OpenPrinterW")));
            a.closePrinter=reinterpret_cast<decltype(a.closePrinter)>(reinterpret_cast<void*>(GetProcAddress(m,"ClosePrinter")));
            a.documentProperties=reinterpret_cast<decltype(a.documentProperties)>(reinterpret_cast<void*>(GetProcAddress(m,"DocumentPropertiesW")));
            a.deviceCapabilities=reinterpret_cast<decltype(a.deviceCapabilities)>(reinterpret_cast<void*>(GetProcAddress(m,"DeviceCapabilitiesW")));
            a.enumJobs=reinterpret_cast<decltype(a.enumJobs)>(reinterpret_cast<void*>(GetProcAddress(m,"EnumJobsW")));
            a.setJob=reinterpret_cast<decltype(a.setJob)>(reinterpret_cast<void*>(GetProcAddress(m,"SetJobW")));
            return a;}();
        return api;
    }
};
bool QaMode(){return workspaceTest||smoke;}
std::wstring QaValue(const wchar_t* name){if(!QaMode())return L"";wchar_t b[1024]{};const DWORD n=GetEnvironmentVariableW(name,b,1024);return n&&n<1024?std::wstring(b,n):std::wstring();}
// Installed printers and connections, by name (level 4 reads the spooler's list only).
std::vector<std::wstring> Names(){
    std::vector<std::wstring> out;
    if(const auto fake=QaValue(L"BROWSER_PRINT_QA_PRINTERS");!fake.empty()||!QaValue(L"BROWSER_PRINT_QA_SINK").empty()){   // QA: fake printers only, never the real ones
        size_t at=0;while(at<=fake.size()){const auto end=fake.find(L';',at);const auto name=fake.substr(at,end==std::wstring::npos?std::wstring::npos:end-at);if(!name.empty())out.push_back(name);if(end==std::wstring::npos)break;at=end+1;}
        return out;
    }
    const auto& api=Api::Get();if(!api.enumPrinters)return out;
    DWORD need=0,count=0;api.enumPrinters(PRINTER_ENUM_LOCAL|PRINTER_ENUM_CONNECTIONS,nullptr,4,nullptr,0,&need,&count);
    if(!need)return out;std::vector<BYTE> buffer(need);
    if(!api.enumPrinters(PRINTER_ENUM_LOCAL|PRINTER_ENUM_CONNECTIONS,nullptr,4,buffer.data(),need,&need,&count))return out;
    auto* info=reinterpret_cast<PRINTER_INFO_4W*>(buffer.data());
    for(DWORD i=0;i<count;++i)if(info[i].pPrinterName&&*info[i].pPrinterName)out.push_back(info[i].pPrinterName);
    return out;
}
// Paper sizes (mm, portrait) of the DEVMODE paper ids that need no driver call.
bool KnownPaper(short id,float& w,float& h){
    switch(id){
    case DMPAPER_A4:case DMPAPER_A4SMALL:w=210;h=297;return true;
    case DMPAPER_LETTER:case DMPAPER_LETTERSMALL:w=215.9f;h=279.4f;return true;
    case DMPAPER_LEGAL:w=215.9f;h=355.6f;return true;
    case DMPAPER_A3:w=297;h=420;return true;
    case DMPAPER_A5:w=148;h=210;return true;
    case DMPAPER_B5:w=182;h=257;return true;
    case DMPAPER_EXECUTIVE:w=184.15f;h=266.7f;return true;
    case DMPAPER_TABLOID:w=279.4f;h=431.8f;return true;
    }
    return false;
}
// The Windows region's paper (Paramètres › Région): A4 unless the region says otherwise.
void RegionPaper(float& w,float& h){
    DWORD size=0;w=210;h=297;
    if(GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT,LOCALE_IPAPERSIZE|LOCALE_RETURN_NUMBER,reinterpret_cast<LPWSTR>(&size),sizeof(size)/sizeof(wchar_t))&&size)KnownPaper(static_cast<short>(size),w,h);
}
// A printer's default paper (driver calls: run it off the UI thread). False when unknown.
bool PrinterPaper(const std::wstring& name,float& w,float& h){
    const auto& api=Api::Get();if(!api.openPrinter||!api.closePrinter||!api.documentProperties)return false;
    std::wstring n=name;HANDLE printer=nullptr;if(!api.openPrinter(n.data(),&printer,nullptr)||!printer)return false;
    bool ok=false;const LONG need=api.documentProperties(nullptr,printer,n.data(),nullptr,nullptr,0);
    if(need>=static_cast<LONG>(sizeof(DEVMODEW))){
        std::vector<BYTE> buffer(static_cast<size_t>(need));auto* mode=reinterpret_cast<DEVMODEW*>(buffer.data());
        if(api.documentProperties(nullptr,printer,n.data(),mode,nullptr,DM_OUT_BUFFER)==IDOK){
            if((mode->dmFields&DM_PAPERWIDTH)&&(mode->dmFields&DM_PAPERLENGTH)&&mode->dmPaperWidth>0&&mode->dmPaperLength>0){w=mode->dmPaperWidth/10.f;h=mode->dmPaperLength/10.f;ok=true;}
            else if(mode->dmFields&DM_PAPERSIZE){
                ok=KnownPaper(mode->dmPaperSize,w,h);
                if(!ok&&api.deviceCapabilities){   // driver-specific paper: its size from the driver's own table
                    const int k=api.deviceCapabilities(n.c_str(),nullptr,DC_PAPERS,nullptr,nullptr);
                    if(k>0&&k<4096){std::vector<WORD> ids(static_cast<size_t>(k));std::vector<POINT> sizes(static_cast<size_t>(k));
                        if(api.deviceCapabilities(n.c_str(),nullptr,DC_PAPERS,reinterpret_cast<LPWSTR>(ids.data()),nullptr)==k&&api.deviceCapabilities(n.c_str(),nullptr,DC_PAPERSIZE,reinterpret_cast<LPWSTR>(sizes.data()),nullptr)==k)
                            for(int i=0;i<k;++i)if(ids[static_cast<size_t>(i)]==static_cast<WORD>(mode->dmPaperSize)&&sizes[static_cast<size_t>(i)].x>0&&sizes[static_cast<size_t>(i)].y>0){w=sizes[static_cast<size_t>(i)].x/10.f;h=sizes[static_cast<size_t>(i)].y/10.f;ok=true;break;}}
                }
            }
            if(ok&&w>h)std::swap(w,h);   // portrait, whatever the driver's default orientation
        }
    }
    api.closePrinter(printer);return ok;
}

// « Annuler » while printing: deletes this user's jobs named `document` that reached `printer`'s queue since
// `since` (UTC). Returns how many were deleted. The engine gives no handle on its job: this is the only way.
int CancelJobs(const std::wstring& printer,const std::wstring& document,const SYSTEMTIME& since){
    const auto& api=Api::Get();if(!api.openPrinter||!api.closePrinter||!api.enumJobs||!api.setJob||printer.empty())return 0;
    std::wstring n=printer;HANDLE h=nullptr;PRINTER_DEFAULTSW access{nullptr,nullptr,PRINTER_ACCESS_USE};if(!api.openPrinter(n.data(),&h,&access)||!h)return 0;
    wchar_t user[256]{};DWORD userLen=256;GetUserNameW(user,&userLen);
    FILETIME sinceFt{};SystemTimeToFileTime(&since,&sinceFt);ULARGE_INTEGER from{};from.LowPart=sinceFt.dwLowDateTime;from.HighPart=sinceFt.dwHighDateTime;from.QuadPart-=20000000ull;   // 2 s of clock slack
    DWORD need=0,count=0;api.enumJobs(h,0,256,1,nullptr,0,&need,&count);int deleted=0;
    if(need){std::vector<BYTE> buffer(need);if(api.enumJobs(h,0,256,1,buffer.data(),need,&need,&count)){auto* jobs=reinterpret_cast<JOB_INFO_1W*>(buffer.data());
        for(DWORD i=0;i<count;++i){FILETIME ft{};SystemTimeToFileTime(&jobs[i].Submitted,&ft);ULARGE_INTEGER at{};at.LowPart=ft.dwLowDateTime;at.HighPart=ft.dwHighDateTime;
            const bool mine=jobs[i].pUserName&&_wcsicmp(jobs[i].pUserName,user)==0,named=jobs[i].pDocument&&!document.empty()&&document==jobs[i].pDocument;
            if(mine&&named&&at.QuadPart>=from.QuadPart&&api.setJob(h,jobs[i].JobId,0,nullptr,JOB_CONTROL_DELETE))++deleted;}}}
    api.closePrinter(h);return deleted;
}
}
// ---- session ---------------------------------------------------------------------------------------
struct PrintSession {
    std::weak_ptr<Tab> tab; unsigned serial=0; Workspace* owner=nullptr;
    std::vector<std::wstring> names;   // engine printer names, aligned with the dialog's list (the PDF entry: empty)
    unsigned generation=0;             // the document the dialog waits for
    bool previewed=false;              // a first preview arrived (else a failure falls back to the engine)
    bool fromPage=false;               // opened by the page's window.print()
    int paperFor=-1;                   // the destination whose paper the dialog has
};
std::shared_ptr<PrintSession> printSession;
std::wstring printLastDestination;          // the last chosen destination (« » = PDF), for the session
ULONGLONG printPageQuietUntil=0;            // a page's window.print() right after its window was cancelled is ignored (loops)
HWND printPump=nullptr;                     // message-only window: worker results reach the UI thread here
constexpr UINT PrintPreviewMessage=WM_APP+1,PrintPaperMessage=WM_APP+2,PrintSavedMessage=WM_APP+3,PrintRetryMessage=WM_APP+4;
struct PrintPaperResult{unsigned generation=0;int printer=-1;float width=0,height=0;bool ok=false;};
struct PrintSavedResult{std::wstring path,uri;std::int64_t started=0,size=0;bool ok=false;Workspace* owner=nullptr;};

// One engine print operation at a time (SDK rule). Each op ends with PrintOpDone(its token); a late or repeated end
// (the tab closed first, then the engine answered) is ignored.
struct PrintOp{bool preview=false;unsigned serial=0;std::function<void(unsigned)> start;};
std::deque<PrintOp> printOps;bool printOpRunning=false;unsigned printOpSerial=0,printOpToken=0;
void PrintRun(){
    if(printOpRunning||printOps.empty()||halted)return;
    auto op=std::move(printOps.front());printOps.pop_front();printOpRunning=true;printOpSerial=op.serial;op.start(++printOpToken);
}
void PrintOpDone(unsigned token){if(!printOpRunning||token!=printOpToken)return;printOpRunning=false;printOpSerial=0;PrintRun();}
void PrintQueue(PrintOp op){
    if(op.preview)printOps.erase(std::remove_if(printOps.begin(),printOps.end(),[](const PrintOp& o){return o.preview;}),printOps.end());   // only the newest preview matters
    printOps.push_back(std::move(op));PrintRun();
}

std::shared_ptr<Tab> PrintTab(){auto s=printSession;if(!s)return nullptr;auto t=s->tab.lock();return t&&!t->closed&&t->view.p?t:nullptr;}
Com<ICoreWebView2PrintSettings> PrintSettingsFor(const PD::State& st,bool ranges,const std::wstring& printer){
    Com<ICoreWebView2PrintSettings> s;Com<ICoreWebView2Environment6> env6;
    if(!environment.p||FAILED(environment->QueryInterface(IID_ICoreWebView2Environment6,(void**)&env6.p))||!env6.p||FAILED(env6->CreatePrintSettings(&s.p))||!s.p)return s;
    s->put_Orientation(st.landscape?COREWEBVIEW2_PRINT_ORIENTATION_LANDSCAPE:COREWEBVIEW2_PRINT_ORIENTATION_PORTRAIT);
    s->put_PageWidth(st.paperWidth/25.4);s->put_PageHeight(st.paperHeight/25.4);
    s->put_ShouldPrintBackgrounds(st.backgrounds?TRUE:FALSE);
    Com<ICoreWebView2PrintSettings2> s2;
    if(SUCCEEDED(s->QueryInterface(IID_ICoreWebView2PrintSettings2,(void**)&s2.p))&&s2.p){
        if(ranges&&st.chooseRange)s2->put_PageRanges(PD::EngineRanges(st.ranges).c_str());
        if(!printer.empty()){s2->put_PrinterName(printer.c_str());s2->put_MediaSize(COREWEBVIEW2_PRINT_MEDIA_SIZE_DEFAULT);}   // the printer's own paper (the preview used it)
    }
    return s;
}
// The stream's bytes (the engine's PDF); empty on failure.
std::shared_ptr<std::vector<BYTE>> PrintBytes(IStream* stream){
    auto out=std::make_shared<std::vector<BYTE>>();if(!stream)return out;
    STATSTG st{};if(SUCCEEDED(stream->Stat(&st,STATFLAG_NONAME))&&st.cbSize.QuadPart>0&&st.cbSize.QuadPart<(1ull<<31))out->reserve((size_t)st.cbSize.QuadPart);
    LARGE_INTEGER zero{};stream->Seek(zero,STREAM_SEEK_SET,nullptr);
    BYTE chunk[1<<16];ULONG got=0;
    while(SUCCEEDED(stream->Read(chunk,sizeof(chunk),&got))&&got>0){out->insert(out->end(),chunk,chunk+got);if(out->size()>=(1ull<<31)){out->clear();break;}}
    return out;
}
void PrintFallback(const std::shared_ptr<Tab>& tab){
    Log(L"PRINT engine preview kept for this request");
    if(!tab||tab->closed||!tab->view.p)return;
    Com<ICoreWebView2_16> v16;if(SUCCEEDED(tab->view->QueryInterface(IID_ICoreWebView2_16,(void**)&v16.p))&&v16.p&&SUCCEEDED(v16->ShowPrintUI(COREWEBVIEW2_PRINT_DIALOG_KIND_BROWSER)))return;
    if(FAILED(tab->view->ExecuteScript(L"window.print()",nullptr)))Log(L"PRINT window.print failed");
}

// ---- preview ---------------------------------------------------------------------------------------
void PrintAfter(UINT ms,std::function<void()> f);
void PrintPreviewNow(){
    auto session=printSession;auto tab=PrintTab();if(!session||!tab||!printDialog.Visible())return;
    const unsigned gen=++session->generation;printRenderer.Drop(gen);
    PrintOp op;op.preview=true;op.serial=tab->serial;
    op.start=[session,gen,weak=std::weak_ptr<Tab>(tab)](unsigned token){
        auto t=weak.lock();Com<ICoreWebView2_16> v16;
        if(gen!=session->generation||session!=printSession||!printDialog.Visible()||!t||t->closed||!t->view.p||FAILED(t->view->QueryInterface(IID_ICoreWebView2_16,(void**)&v16.p))||!v16.p){PrintOpDone(token);return;}
        auto settings=PrintSettingsFor(printDialog.Current(),false,L"");
        const auto started=GetTickCount64();
        auto done=Callback<ICoreWebView2PrintToPdfStreamCompletedHandler,HRESULT,IStream*>([session,gen,started,token](HRESULT hr,IStream* stream)->HRESULT{
            PrintOpDone(token);
            if(halted||gen!=session->generation||session!=printSession||!printDialog.Visible())return S_OK;
            if(FAILED(hr)||!stream){
                Log(L"PRINT preview failed hr="+HrText(hr));
                if(hr==E_ABORT){if(printPump)PostMessageW(printPump,PrintRetryMessage,gen,0);return S_OK;}   // another print of this page was running: try again
                Log(L"PRINT the engine made no PDF of this document: preview unavailable, printing still possible");printDialog.SetUnavailable();
                return S_OK;
            }
            auto bytes=PrintBytes(stream);
            Log(L"PRINT preview "+std::to_wstring(bytes->size()/1024)+L" KB in "+std::to_wstring(GetTickCount64()-started)+L" ms");
            if(bytes->empty()||!PrintSpool::QaValue(L"BROWSER_PRINT_QA_NO_PREVIEW").empty()){Log(L"PRINT empty PDF: preview unavailable");printDialog.SetUnavailable();return S_OK;}
            session->previewed=true;
            const int hold=_wtoi(PrintSpool::QaValue(L"BROWSER_PRINT_QA_PREVIEW_DELAY").c_str());   // QA: state 5 held on screen
            if(hold>0)PrintAfter((UINT)std::min(hold,60000),[session,gen,bytes]{if(gen==session->generation)printRenderer.Load(gen,bytes);});else printRenderer.Load(gen,bytes);
            return S_OK;});
        const HRESULT hr=settings.p?v16->PrintToPdfStream(settings.p,done):E_NOINTERFACE;done->Release();
        if(FAILED(hr)){Log(L"PRINT PrintToPdfStream refused hr="+HrText(hr));PrintOpDone(token);}
    };
    PrintQueue(std::move(op));
}
// A new destination: its paper first (off the UI thread for a printer), then a new document.
void PrintDestinationChanged(){
    auto session=printSession;if(!session||!printDialog.Visible())return;
    const auto& st=printDialog.Current();const unsigned gen=++session->generation;printRenderer.Drop(gen);
    printLastDestination=st.printer>=0&&st.printer<(int)session->names.size()?session->names[(size_t)st.printer]:L"";
    if(st.printer==session->paperFor){PrintPreviewNow();return;}   // orientation or backgrounds: same paper
    session->paperFor=st.printer;
    if(st.SavesPdf()||printLastDestination.empty()||!PrintSpool::QaValue(L"BROWSER_PRINT_QA_PRINTERS").empty()){float w,h;PrintSpool::RegionPaper(w,h);printDialog.SetPaper(w,h);PrintPreviewNow();return;}
    const std::wstring name=printLastDestination;const int printer=st.printer;HWND pump=printPump;
    try{std::thread([name,printer,gen,pump]{auto* r=new PrintPaperResult;r->generation=gen;r->printer=printer;r->ok=PrintSpool::PrinterPaper(name,r->width,r->height);if(!pump||!PostMessageW(pump,PrintPaperMessage,0,(LPARAM)r))delete r;}).detach();}
    catch(...){float w,h;PrintSpool::RegionPaper(w,h);printDialog.SetPaper(w,h);PrintPreviewNow();}
}

// ---- accept ----------------------------------------------------------------------------------------
void PrintSaveBytes(const std::shared_ptr<std::vector<BYTE>>& bytes,const std::wstring& path,const std::wstring& uri,std::int64_t started){
    Workspace* owner=WorkspaceNow();HWND pump=printPump;
    auto job=[bytes,path,uri,started,owner,pump]{
        auto* r=new PrintSavedResult;r->path=path;r->uri=uri;r->started=started;r->owner=owner;
        HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f!=INVALID_HANDLE_VALUE){size_t at=0;r->ok=true;while(at<bytes->size()){DWORD wrote=0;const DWORD n=(DWORD)std::min<size_t>(bytes->size()-at,1u<<20);if(!WriteFile(f,bytes->data()+at,n,&wrote,nullptr)||!wrote){r->ok=false;break;}at+=wrote;}
            CloseHandle(f);if(!r->ok)DeleteFileW(path.c_str());r->size=(std::int64_t)bytes->size();}
        if(!pump||!PostMessageW(pump,PrintSavedMessage,0,(LPARAM)r))delete r;
    };
    try{std::thread(job).detach();}catch(...){job();}
}
void PrintSaved(PrintSavedResult* r){
    std::unique_ptr<PrintSavedResult> keep(r);if(halted||!r||WorkspaceDead(r->owner))return;
    WorkspaceScope scope(r->owner);
    if(!r->ok){Log(L"PRINT PDF not written "+r->path);return;}
    DL::Item item;item.id=nextDownloadId++;item.path=r->path;item.uri=r->uri;item.started=r->started;item.state=DL::State::Completed;item.total=item.received=r->size;
    downloads.insert(downloads.begin(),item);SaveDownloads();Log(L"PRINT PDF saved "+DL::FileName(r->path));
    if(downloadPanel.Visible()||(downloadDrawer.Visible()&&!downloadDrawer.Closing()))DownloadsChanged();else ShowDownloads();
}
// QA sink: the job a printer would have received, as a PDF and its settings, never a real printer.
void PrintQaSink(const std::shared_ptr<std::vector<BYTE>>& bytes,const PD::State& st,const std::wstring& printer){
    const auto folder=PrintSpool::QaValue(L"BROWSER_PRINT_QA_SINK");if(folder.empty())return;
    std::error_code ignored;std::filesystem::create_directories(folder,ignored);
    {std::ofstream out(std::filesystem::path(folder)/L"print-job.pdf",std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes->data()),(std::streamsize)bytes->size());}
    auto utf8=[](const std::wstring& w){std::string r;const int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);if(n>0){r.resize((size_t)n);WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),r.data(),n,nullptr,nullptr);}return r;};
    std::ofstream info(std::filesystem::path(folder)/L"print-job.txt",std::ios::trunc);
    info<<"printer="<<utf8(printer)<<"\norientation="<<(st.landscape?"landscape":"portrait")<<"\nbackgrounds="<<(st.backgrounds?1:0)<<"\nranges="<<utf8(st.chooseRange?PD::EngineRanges(st.ranges):L"")<<"\npaper="<<st.paperWidth<<"x"<<st.paperHeight<<"\nbytes="<<bytes->size()<<"\n";
    Log(L"PRINT QA job written to the sink");
}
// ---- printing: states 7 and 8, the C8 toasts -----------------------------------------------------------
struct PrintJob{PD::State st;std::wstring printer,title;std::weak_ptr<Tab> tab;bool cancelled=false,done=false;SYSTEMTIME startedUtc{};};
constexpr wchar_t PrintProgressToastId[]=L"PrintProgressToast",PrintErrorToastId[]=L"PrintErrorToast";
// Board « Toast — en cours » (1NT4-0): ring #FFFFFF26 1.5 and a white arc from the top to (13.6, 10.8) of 16,
// i.e. 116.6° of the turn; the engine reports no progress, so the arc is the board's, fixed.
constexpr float PrintToastArc=116.57f/360.f;
NativeChrome::ContextToast::Spec PrintProgressSpec(const std::wstring& printer){
    namespace CT=NativeChrome::ContextToast;CT::Spec s;s.automationId=PrintProgressToastId;s.icon=CT::Icon::Progress;s.progress=PrintToastArc;
    s.message=TR(print_printing);s.detail=printer;s.action=TR(common_cancel);s.durationMs=0;s.closable=false;return s;
}
NativeChrome::ContextToast::Spec PrintErrorSpec(const std::wstring& reason){
    namespace CT=NativeChrome::ContextToast;CT::Spec s;s.automationId=PrintErrorToastId;s.icon=CT::Icon::Error;s.error=true;
    s.message=TR(print_failed);s.detail=reason;s.action=TR(print_retry);s.durationMs=0;s.closable=true;return s;
}
std::map<UINT_PTR,std::function<void()>> printLater;UINT_PTR printLaterId=100;
void PrintAfter(UINT ms,std::function<void()> f){if(!printPump){f();return;}const UINT_PTR id=++printLaterId;printLater[id]=std::move(f);SetTimer(printPump,id,ms,nullptr);}
void PrintJobStart(const std::shared_ptr<PrintJob>& job);
void PrintJobCancel(const std::shared_ptr<PrintJob>& job){
    if(!job||job->cancelled||job->done)return;job->cancelled=true;Log(L"PRINT job cancelled by the user");
    const int n=PrintSpool::CancelJobs(job->printer,job->title,job->startedUtc);if(n)Log(L"PRINT "+std::to_wstring(n)+L" job(s) deleted from the printer queue");
}
void PrintJobEnded(const std::shared_ptr<PrintJob>& job,HRESULT hr,COREWEBVIEW2_PRINT_STATUS status){
    job->done=true;
    if(job->cancelled){const int n=PrintSpool::CancelJobs(job->printer,job->title,job->startedUtc);Log(L"PRINT cancelled job ended; "+std::to_wstring(n)+L" job(s) deleted from the printer queue");return;}
    const bool ours=contextToast.Visible()&&contextToast.Current().automationId==PrintProgressToastId;
    if(SUCCEEDED(hr)&&status==COREWEBVIEW2_PRINT_STATUS_SUCCEEDED){Log(L"PRINT job sent to the printer");if(ours)contextToast.Hide();return;}
    Log(L"PRINT job failed hr="+HrText(hr)+L" status="+std::to_wstring((int)status));
    const std::wstring reason=status==COREWEBVIEW2_PRINT_STATUS_PRINTER_UNAVAILABLE?std::wstring(TR(print_offline)):std::wstring();   // other failures: no reason on the board
    contextToast.Show(PrintErrorSpec(reason),PageScreenRect(),[job](bool retry){if(!retry||halted)return;auto again=std::make_shared<PrintJob>(*job);again->cancelled=again->done=false;PrintJobStart(again);});
}
void PrintJobStart(const std::shared_ptr<PrintJob>& job){
    auto tab=job->tab.lock();if(halted||!tab||tab->closed)return;
    GetSystemTime(&job->startedUtc);job->title=tab->title;
    contextToast.Show(PrintProgressSpec(job->printer),PageScreenRect(),[job](bool cancel){if(cancel)PrintJobCancel(job);});
    const bool qa=!PrintSpool::QaValue(L"BROWSER_PRINT_QA_SINK").empty();
    PrintOp op;op.serial=tab->serial;
    op.start=[job,qa](unsigned token){
        auto t=job->tab.lock();Com<ICoreWebView2_16> v16;
        if(halted||!t||t->closed||!t->view.p||FAILED(t->view->QueryInterface(IID_ICoreWebView2_16,(void**)&v16.p))||!v16.p){PrintOpDone(token);PrintJobEnded(job,E_ABORT,COREWEBVIEW2_PRINT_STATUS_OTHER_ERROR);return;}
        if(qa){   // QA: the job a printer would get goes to the sink, held BROWSER_PRINT_QA_DELAY ms, failing on BROWSER_PRINT_QA_FAIL
            auto settings=PrintSettingsFor(job->st,true,L"");
            auto done=Callback<ICoreWebView2PrintToPdfStreamCompletedHandler,HRESULT,IStream*>([job,token](HRESULT hr,IStream* stream)->HRESULT{
                PrintOpDone(token);auto bytes=SUCCEEDED(hr)&&stream?PrintBytes(stream):std::make_shared<std::vector<BYTE>>();
                const auto fail=PrintSpool::QaValue(L"BROWSER_PRINT_QA_FAIL");const int delay=_wtoi(PrintSpool::QaValue(L"BROWSER_PRINT_QA_DELAY").c_str());
                PrintAfter((UINT)std::clamp(delay,0,60000),[job,bytes,fail]{
                    if(!job->cancelled&&fail.empty()&&!bytes->empty())PrintQaSink(bytes,job->st,job->printer);
                    PrintJobEnded(job,S_OK,fail==L"offline"?COREWEBVIEW2_PRINT_STATUS_PRINTER_UNAVAILABLE:!fail.empty()||bytes->empty()?COREWEBVIEW2_PRINT_STATUS_OTHER_ERROR:COREWEBVIEW2_PRINT_STATUS_SUCCEEDED);});
                return S_OK;});
            const HRESULT hr=settings.p?v16->PrintToPdfStream(settings.p,done):E_NOINTERFACE;done->Release();
            if(FAILED(hr)){Log(L"PRINT PrintToPdfStream refused hr="+HrText(hr));PrintOpDone(token);PrintJobEnded(job,hr,COREWEBVIEW2_PRINT_STATUS_OTHER_ERROR);}
            return;
        }
        auto settings=PrintSettingsFor(job->st,true,job->printer);
        auto done=Callback<ICoreWebView2PrintCompletedHandler,HRESULT,COREWEBVIEW2_PRINT_STATUS>([job,token](HRESULT hr,COREWEBVIEW2_PRINT_STATUS status)->HRESULT{
            PrintOpDone(token);PrintJobEnded(job,hr,status);return S_OK;});
        const HRESULT hr=settings.p?v16->Print(settings.p,done):E_NOINTERFACE;done->Release();
        if(FAILED(hr)){Log(L"PRINT Print refused hr="+HrText(hr));PrintOpDone(token);PrintJobEnded(job,hr,COREWEBVIEW2_PRINT_STATUS_OTHER_ERROR);}
    };
    PrintQueue(std::move(op));
}
void PrintAccepted(const PD::State& st){
    auto session=printSession;auto tab=PrintTab();if(!session||!tab)return;
    const std::wstring printer=st.printer>=0&&st.printer<(int)session->names.size()?session->names[(size_t)st.printer]:L"";
    printRenderer.Drop(++session->generation);
    if(!st.SavesPdf()){auto job=std::make_shared<PrintJob>();job->st=st;job->printer=printer;job->tab=tab;PrintJobStart(job);return;}
    PrintOp op;op.serial=tab->serial;
    op.start=[st,weak=std::weak_ptr<Tab>(tab)](unsigned token){   // « Enregistrer »: the PDF with the ranges, into the download folder
        auto t=weak.lock();Com<ICoreWebView2_16> v16;
        if(halted||!t||t->closed||!t->view.p||FAILED(t->view->QueryInterface(IID_ICoreWebView2_16,(void**)&v16.p))||!v16.p){PrintOpDone(token);return;}
        auto settings=PrintSettingsFor(st,true,L"");
        const auto folder=EffectiveDownloadFolder();
        if(folder.empty()){Log(L"PRINT no download folder");PrintOpDone(token);return;}
        std::error_code ignored;std::filesystem::create_directories(folder,ignored);const std::wstring path=UniqueDownloadPath(folder,PdfFileName(*t));
        const auto uri=t->home?std::wstring():t->uri;const auto started=NativeChrome::DownloadsUI::UnixNowMs();
        auto done=Callback<ICoreWebView2PrintToPdfStreamCompletedHandler,HRESULT,IStream*>([path,uri,started,token](HRESULT hr,IStream* stream)->HRESULT{
            PrintOpDone(token);
            if(FAILED(hr)||!stream){Log(L"PRINT PDF failed hr="+HrText(hr));return S_OK;}
            auto bytes=PrintBytes(stream);if(bytes->empty()){Log(L"PRINT PDF empty");return S_OK;}
            PrintSaveBytes(bytes,path,uri,started);return S_OK;});
        const HRESULT hr=settings.p?v16->PrintToPdfStream(settings.p,done):E_NOINTERFACE;done->Release();
        if(FAILED(hr)){Log(L"PRINT PrintToPdfStream refused hr="+HrText(hr));PrintOpDone(token);}
    };
    PrintQueue(std::move(op));
}

// ---- pump (worker results on the UI thread) ---------------------------------------------------------
LRESULT CALLBACK PrintPumpProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    switch(msg){
    case PrintPreviewMessage:{
        auto session=printSession;const auto results=printRenderer.Take();
        if(!session||!printDialog.Visible())return 0;
        for(const auto& r:results){
            if(r.generation!=session->generation)continue;
            if(r.kind==PrintPreview::Result::Count){Log(L"PRINT preview "+std::to_wstring(r.count)+L" page(s)");printDialog.SetDocument(r.count);}
            else if(r.kind==PrintPreview::Result::Page)printDialog.SetPage(r.page,r.width,r.height,r.pixels);
            else if(r.page<0){Log(L"PRINT preview PDF unreadable hr="+HrText(r.hr));printDialog.SetUnavailable();}
            else Log(L"PRINT preview page "+std::to_wstring(r.page)+L" not drawn hr="+HrText(r.hr));
        }
        return 0;}
    case PrintPaperMessage:{
        std::unique_ptr<PrintPaperResult> r(reinterpret_cast<PrintPaperResult*>(l));auto session=printSession;
        if(!r||!session||!printDialog.Visible()||r->printer!=printDialog.Current().printer)return 0;   // still this printer (an orientation change meanwhile keeps its paper)
        float wmm=r->width,hmm=r->height;if(!r->ok)PrintSpool::RegionPaper(wmm,hmm);
        printDialog.SetPaper(wmm,hmm);PrintPreviewNow();return 0;}
    case PrintSavedMessage:PrintSaved(reinterpret_cast<PrintSavedResult*>(l));return 0;
    case PrintRetryMessage:{auto session=printSession;if(session&&(unsigned)w==session->generation&&printDialog.Visible())SetTimer(h,1,250,nullptr);return 0;}
    case WM_TIMER:{KillTimer(h,w);auto later=printLater.find(w);if(later!=printLater.end()){auto f=std::move(later->second);printLater.erase(later);if(!halted)f();return 0;}
        if(auto s=printSession;s&&printDialog.Visible()){--s->generation;PrintPreviewNow();}return 0;}
    }
    return DefWindowProcW(h,msg,w,l);
}

// ---- entry points -----------------------------------------------------------------------------------
float PrintCorner(){return NativeChrome::Design::Viewport::Radius(IsZoomed(window)||InFullscreen());}
void PrintPage(std::shared_ptr<Tab> tab){
    if(halted||!tab||tab->closed||!tab->view.p||WorkspaceParked())return;
    if(printDialog.Visible()||printDialog.Closing())return;
    Com<ICoreWebView2_16> v16;Com<ICoreWebView2Environment6> env6;
    const bool ours=printDialog.Window()&&printPump&&PrintPreview::Renderer::Available()&&SUCCEEDED(tab->view->QueryInterface(IID_ICoreWebView2_16,(void**)&v16.p))&&v16.p
        &&environment.p&&SUCCEEDED(environment->QueryInterface(IID_ICoreWebView2Environment6,(void**)&env6.p))&&env6.p;
    if(!ours){Log(L"PRINT window unavailable");PrintFallback(tab);return;}
    auto session=std::make_shared<PrintSession>();session->tab=tab;session->serial=tab->serial;session->owner=WorkspaceNow();
    PD::State st;st.printers.push_back(TR(cmd_save_pdf));session->names.push_back(L"");st.pdf=0;
    for(auto& name:PrintSpool::Names()){st.printers.push_back(name);session->names.push_back(name);}
    for(size_t i=0;i<session->names.size();++i)if(session->names[i]==printLastDestination){st.printer=(int)i;break;}
    PrintSpool::RegionPaper(st.paperWidth,st.paperHeight);
    printSession=session;printDialog.SetReducedMotion(motion.ReducedMotion());
    if(!printDialog.Show(st,PageScreenRect(),PrintCorner())){printSession.reset();Log(L"PRINT window could not open");PrintFallback(tab);return;}
    Log(L"PRINT window open ("+std::to_wstring(st.printers.size()-1)+L" printer(s))");
    PrintDestinationChanged();
}
// window.print() from a page: the top document's call reaches us as a message (PD::PageScript, src/print-dialog.h).
void RegisterPrintScript(const std::shared_ptr<Tab>& t,const PopupHost::RequestPtr& popup){
    if(!t||!t->view.p)return;
    auto* barrier=PopupScriptBarrier(popup,L"print");HRESULT added=t->view->AddScriptToExecuteOnDocumentCreated(PD::PageScript,barrier);
    if(barrier){if(FAILED(added))popups.Finish(popup,false);barrier->Release();}
    if(FAILED(added))Log(L"PRINT window.print script not added: the engine's preview stays for window.print()");
}
bool PrintPageMessage(const std::shared_ptr<Tab>& t,const std::wstring& message){
    if(message!=PD::PageMessage)return false;
    if(halted||!t||t->closed||t!=Active()){Log(L"PRINT window.print ignored (not the active tab)");return true;}
    if(GetTickCount64()<printPageQuietUntil){Log(L"PRINT window.print ignored (just cancelled)");return true;}
    PrintPage(t);if(auto s=printSession;s&&printDialog.Visible())s->fromPage=true;
    return true;
}
// ---- host hooks (main.cpp / elements-integration.h) ----------------------------------------------------
void CreatePrintSurfaces(){
    WNDCLASSW pc{};pc.lpfnWndProc=PrintPumpProc;pc.hInstance=instance;pc.lpszClassName=L"BrowserPrintPump";RegisterClassW(&pc);
    printPump=CreateWindowExW(0,pc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);
    if(!printPump||!printRenderer.Start(printPump,PrintPreviewMessage))Log(L"PRINT preview worker unavailable: the engine's preview is kept");
    PD::Dialog::Hooks h;
    h.wake=[]{RequestElementFrame();};h.keyboard=[]{return keyboardModality;};
    h.changed=[](const PD::State&){PrintDestinationChanged();};
    h.want=[](int page,UINT w,UINT hgt){if(auto s=printSession)printRenderer.Want(s->generation,page,w,hgt);};
    h.accept=[](const PD::State& st){Log(L"PRINT accepted: "+std::wstring(st.SavesPdf()?L"PDF":L"printer"));PrintAccepted(st);if(auto t=PrintTab();t&&t==Active())FocusPage();};
    h.cancel=[]{auto s=printSession;Log(L"PRINT cancelled");if(s&&s->fromPage)printPageQuietUntil=GetTickCount64()+1000;if(s)printRenderer.Drop(++s->generation);if(auto t=PrintTab();t&&t==Active())FocusPage();};
    h.addPrinter=[]{Log(L"PRINT Windows printer settings asked for");if(PrintSpool::QaMode())return; /* QA: never opens Windows Settings */if((INT_PTR)ShellExecuteW(window,L"open",L"ms-settings:printers",nullptr,nullptr,SW_SHOWNORMAL)<=32)Log(L"PRINT printer settings could not open");};
    if(!printDialog.Create(instance,window,base,std::move(h)))Log(L"PRINT window unavailable: the engine's preview is kept");
}
void DisposePrintSurfaces(){
    printDialog.Abort();printDialog.Dispose();printRenderer.Stop();printSession.reset();printOps.clear();printLater.clear();
    if(printPump){DestroyWindow(printPump);printPump=nullptr;}
}
void PrintFrame(NativeChrome::Motion::Time now){printDialog.Frame(now);}
NativeChrome::Motion::Wake PrintNextWake(NativeChrome::Motion::Time now){return printDialog.NextWake(now);}
void PrintReposition(){if(printDialog.Visible())printDialog.Reposition(PageScreenRect(),PrintCorner());}
bool PrintSurfaceFocus(HWND f){HWND w=printDialog.Window();return w&&f&&(f==w||IsChild(w,f));}
// The tab under the window is no longer on screen (another tab, another profile) or is closing.
void PrintTabChanged(){if(printDialog.Visible()&&PrintTab()!=Active())printDialog.Abort();}
void PrintTabClosed(const std::shared_ptr<Tab>& t){
    if(!t)return;
    if(auto s=printSession;s&&s->serial==t->serial&&printDialog.Visible())printDialog.Abort();
    printOps.erase(std::remove_if(printOps.begin(),printOps.end(),[&](const PrintOp& o){return o.serial==t->serial;}),printOps.end());
    if(printOpRunning&&printOpSerial==t->serial)PrintOpDone(printOpToken);   // its engine may never answer now
}
