// Screen-reader view of the owner-drawn chrome (issue #2).
//
// The production window procedure runs on this thread (no WebView2 runtime); a real
// UI Automation client on a second thread reads the tree the way Narrator or NVDA
// would: names, control types, shortcuts, enabled and selected states, Invoke /
// Select / Value patterns, focus and notification events, and the address EDIT
// re-parented into the toolbar.
#include "../../src/main.cpp"
#include <uiautomationclient.h>
#include <atomic>
#include <cstdio>
#include <thread>

static int failures=0;
static void Expect(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);++failures;}else std::printf("ok   %s\n",what);}

// ---- UI thread access from the client thread --------------------------------
constexpr UINT RunMessage=WM_APP+40;
static HWND helper=nullptr;static DWORD uiThread=0;static std::atomic<int> offThread{0},snapshots{0};
static LRESULT CALLBACK HelperProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    if(msg==RunMessage){(*reinterpret_cast<std::function<void()>*>(l))();return 0;}
    return DefWindowProcW(h,msg,w,l);
}
static void OnUi(std::function<void()> f){SendMessageW(helper,RunMessage,0,(LPARAM)&f);}
template<class F> static bool WaitFor(F condition,int ms=3000){
    for(int waited=0;waited<ms;waited+=20){if(condition())return true;Sleep(20);}return condition();
}

// ---- Client helpers -----------------------------------------------------------
static const CLSID ClsidUIAutomation={0xff48dba4,0x60ef,0x4201,{0xaa,0x87,0x54,0x10,0x3e,0xef,0x59,0x4e}};
static const CLSID ClsidUIAutomation8={0xe22ad333,0xb25f,0x460c,{0x83,0xd0,0x05,0x81,0x10,0x73,0x95,0xc9}};
static IUIAutomation* uia=nullptr;
static std::wstring Text(BSTR b){std::wstring s=b?b:L"";SysFreeString(b);return s;}
static std::wstring Name(IUIAutomationElement* e){BSTR b=nullptr;if(FAILED(e->get_CurrentName(&b)))return L"<unavailable>";return Text(b);}
static std::wstring Accelerator(IUIAutomationElement* e){BSTR b=nullptr;e->get_CurrentAcceleratorKey(&b);return Text(b);}
static std::wstring AutomationId(IUIAutomationElement* e){BSTR b=nullptr;e->get_CurrentAutomationId(&b);return Text(b);}
static CONTROLTYPEID Type(IUIAutomationElement* e){CONTROLTYPEID t=0;e->get_CurrentControlType(&t);return t;}
static bool Enabled(IUIAutomationElement* e){BOOL b=FALSE;e->get_CurrentIsEnabled(&b);return b!=FALSE;}
static bool Selected(IUIAutomationElement* e){
    IUIAutomationSelectionItemPattern* p=nullptr;BOOL b=FALSE;
    if(SUCCEEDED(e->GetCurrentPatternAs(UIA_SelectionItemPatternId,__uuidof(IUIAutomationSelectionItemPattern),(void**)&p))&&p){p->get_CurrentIsSelected(&b);p->Release();}
    return b!=FALSE;
}
struct Item { IUIAutomationElement* element; std::wstring name,parent; CONTROLTYPEID type; };
static void Walk(IUIAutomationTreeWalker* walker,IUIAutomationElement* parent,const std::wstring& parentName,std::vector<Item>& out,int depth=0){
    IUIAutomationElement* child=nullptr;walker->GetFirstChildElement(parent,&child);
    while(child){
        Item item{child,Name(child),parentName,Type(child)};out.push_back(item);
        if(getenv("A11Y_TRACE"))std::fwprintf(stderr,L"%*ls%ls (%d)\n",depth*2,L"",item.name.c_str(),(int)item.type);
        if(depth>6||out.size()>200){std::fputs("FAIL: tree too deep or too large (cycle)\n",stderr);std::exit(1);}
        Walk(walker,child,item.name,out,depth+1);
        IUIAutomationElement* next=nullptr;walker->GetNextSiblingElement(child,&next);child=next;
    }
}
static std::vector<Item> Tree(){
    IUIAutomationElement* root=nullptr;IUIAutomationTreeWalker* walker=nullptr;std::vector<Item> items;
    if(FAILED(uia->ElementFromHandle(window,&root))||!root)return items;
    uia->get_ControlViewWalker(&walker);Walk(walker,root,L"<window>",items);walker->Release();root->Release();
    return items;
}
static void Release(std::vector<Item>& items){for(auto& i:items)i.element->Release();items.clear();}
static IUIAutomationElement* Find(std::vector<Item>& items,const std::wstring& name){for(auto& i:items)if(i.name==name)return i.element;return nullptr;}

struct FocusHandler final:IUIAutomationFocusChangedEventHandler {
    std::atomic<long> refs{1};std::atomic<int> count{0};CRITICAL_SECTION lock;std::wstring last;
    FocusHandler(){InitializeCriticalSection(&lock);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(iid==__uuidof(IUnknown)||iid==__uuidof(IUIAutomationFocusChangedEventHandler)){*out=this;AddRef();return S_OK;}*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* e)override{
        auto name=Name(e);EnterCriticalSection(&lock);last=name;LeaveCriticalSection(&lock);++count;return S_OK;
    }
    std::wstring Last(){EnterCriticalSection(&lock);auto s=last;LeaveCriticalSection(&lock);return s;}
};
struct NotificationHandler final:IUIAutomationNotificationEventHandler {
    std::atomic<long> refs{1};CRITICAL_SECTION lock;std::wstring text;
    NotificationHandler(){InitializeCriticalSection(&lock);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(iid==__uuidof(IUnknown)||iid==__uuidof(IUIAutomationNotificationEventHandler)){*out=this;AddRef();return S_OK;}*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    HRESULT STDMETHODCALLTYPE HandleNotificationEvent(IUIAutomationElement*,NotificationKind,NotificationProcessing,BSTR display,BSTR)override{
        EnterCriticalSection(&lock);text=display?display:L"";LeaveCriticalSection(&lock);return S_OK;
    }
    std::wstring Text(){EnterCriticalSection(&lock);auto s=text;LeaveCriticalSection(&lock);return s;}
};

static void Client(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    // CUIAutomation8 (Windows 8+) also serves IUIAutomation2..6; fall back to the original class.
    if(FAILED(CoCreateInstance(ClsidUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IUIAutomation),(void**)&uia))&&
       FAILED(CoCreateInstance(ClsidUIAutomation,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IUIAutomation),(void**)&uia))){
        Expect(false,"UI Automation client available");PostThreadMessageW(uiThread,WM_QUIT,0,0);return;
    }
    std::puts("client: reading tree");auto items=Tree();std::printf("client: %d elements\n",(int)items.size());
    // 1. Every chrome control is announced with a name and the right control type.
    struct Expected { const wchar_t* name; CONTROLTYPEID type; const wchar_t* parent; };
    const Expected expected[]={
        {L"Liste des onglets",UIA_ButtonControlTypeId,L"<window>"},{L"Onglets",UIA_TabControlTypeId,L"<window>"},
        {L"Accueil",UIA_TabItemControlTypeId,L"Onglets"},{L"Exemple",UIA_TabItemControlTypeId,L"Onglets"},{L"Onglet sans titre",UIA_TabItemControlTypeId,L"Onglets"},
        {L"Nouvel onglet",UIA_ButtonControlTypeId,L"<window>"},{L"Réduire",UIA_ButtonControlTypeId,L"<window>"},
        {L"Agrandir",UIA_ButtonControlTypeId,L"<window>"},{L"Fermer la fenêtre",UIA_ButtonControlTypeId,L"<window>"},
        {L"Navigation",UIA_ToolBarControlTypeId,L"<window>"},{L"Précédent",UIA_ButtonControlTypeId,L"Navigation"},
        {L"Suivant",UIA_ButtonControlTypeId,L"Navigation"},{L"Adresse ou recherche",UIA_EditControlTypeId,L"Navigation"},
        {L"Actualiser",UIA_ButtonControlTypeId,L"Navigation"},{L"Copier le lien",UIA_ButtonControlTypeId,L"Navigation"},
        {L"Historique",UIA_ButtonControlTypeId,L"Navigation"},{L"Téléchargements",UIA_ButtonControlTypeId,L"Navigation"},
        {L"Réglages",UIA_ButtonControlTypeId,L"Navigation"}};
    bool all=true;
    for(auto& e:expected){bool found=false;for(auto& i:items)if(i.name==e.name&&i.type==e.type&&i.parent==e.parent)found=true;
        if(!found){std::fwprintf(stderr,L"  missing: %ls\n",e.name);all=false;}}
    Expect(all,"named buttons, tab list, tab items and toolbar are exposed in the tree");
    bool unnamed=false;for(auto& i:items)if(i.name.empty()&&i.type!=UIA_PaneControlTypeId){unnamed=true;std::fwprintf(stderr,L"  unnamed control type %d under %ls\n",(int)i.type,i.parent.c_str());}
    Expect(!unnamed,"no unnamed chrome control");
    // Toolbar order follows the screen.
    std::vector<std::wstring> toolbar;for(auto& i:items)if(i.parent==L"Navigation")toolbar.push_back(i.name);
    Expect(toolbar==std::vector<std::wstring>{L"Précédent",L"Suivant",L"Actualiser",L"Connexion sécurisée",L"Adresse ou recherche",L"Copier le lien",L"Ajouter aux favoris",L"Historique",L"Téléchargements",L"Réglages"},"toolbar reading order");
    Expect(!Find(items,L"Nouvelle fenêtre privée"),"no private-window button in the chrome (it lives in the profile menu)");

    // 2. States and shortcuts.
    auto* back=Find(items,L"Précédent");auto* reload=Find(items,L"Actualiser");auto* copy=Find(items,L"Copier le lien");
    Expect(back&&!Enabled(back),"Précédent is reported disabled without history");
    Expect(reload&&Enabled(reload)&&copy&&Enabled(copy),"Actualiser and Copier le lien are enabled");
    Expect(back&&Accelerator(back)==L"Alt+Flèche gauche","shortcut moved from the name to AcceleratorKey");
    Expect(Find(items,L"Nouvel onglet")&&Accelerator(Find(items,L"Nouvel onglet"))==L"Ctrl+T","Nouvel onglet shortcut");
    auto* home=Find(items,L"Accueil");auto* example=Find(items,L"Exemple");auto* untitled=Find(items,L"Onglet sans titre");
    Expect(example&&Selected(example)&&home&&!Selected(home),"selected tab is reported selected");
    if(example){int position=0,size=0;VARIANT v;if(SUCCEEDED(example->GetCurrentPropertyValue(UIA_PositionInSetPropertyId,&v))&&v.vt==VT_I4)position=v.lVal;
        if(SUCCEEDED(example->GetCurrentPropertyValue(UIA_SizeOfSetPropertyId,&v))&&v.vt==VT_I4)size=v.lVal;Expect(position==2&&size==3,"tab position: 2 of 3");}
    if(auto* address=Find(items,L"Adresse ou recherche")){IUIAutomationValuePattern* value=nullptr;BSTR text=nullptr;
        if(SUCCEEDED(address->GetCurrentPatternAs(UIA_ValuePatternId,__uuidof(IUIAutomationValuePattern),(void**)&value))&&value){value->get_CurrentValue(&text);value->Release();}
        Expect(Text(text)==L"https://example.com/","address value is the page URL");}
    {IUIAutomationElement* tabs=Find(items,L"Onglets");IUIAutomationSelectionPattern* selection=nullptr;IUIAutomationElementArray* chosen=nullptr;int length=0;std::wstring name;
        if(tabs&&SUCCEEDED(tabs->GetCurrentPatternAs(UIA_SelectionPatternId,__uuidof(IUIAutomationSelectionPattern),(void**)&selection))&&selection){
            if(SUCCEEDED(selection->GetCurrentSelection(&chosen))&&chosen){chosen->get_Length(&length);IUIAutomationElement* e=nullptr;if(length&&SUCCEEDED(chosen->GetElement(0,&e))){name=Name(e);e->Release();}chosen->Release();}
            selection->Release();}
        Expect(length==1&&name==L"Exemple","tab list selection is the active tab");}

    // 3. Actions.
    if(back){IUIAutomationInvokePattern* invoke=nullptr;HRESULT hr=E_FAIL;
        if(SUCCEEDED(back->GetCurrentPatternAs(UIA_InvokePatternId,__uuidof(IUIAutomationInvokePattern),(void**)&invoke))&&invoke){hr=invoke->Invoke();invoke->Release();}
        Expect(hr==(HRESULT)UIA_E_ELEMENTNOTENABLED,"invoking a disabled button is refused");}
    if(home){IUIAutomationSelectionItemPattern* item=nullptr;
        if(SUCCEEDED(home->GetCurrentPatternAs(UIA_SelectionItemPatternId,__uuidof(IUIAutomationSelectionItemPattern),(void**)&item))&&item){item->Select();item->Release();}
        Expect(WaitFor([&]{int c=-1;OnUi([&]{c=current;});return c==0;})&&Selected(home)&&!Selected(example),"SelectionItem.Select switches tab");}
    // Close the untitled tab through its close button (a child of the tab item).
    {IUIAutomationElement* close=nullptr;for(auto& i:items)if(i.parent==L"Onglet sans titre"&&AutomationId(i.element)==L"CloseTab")close=i.element;
        Expect(close&&Name(close)==L"Fermer l'onglet Onglet sans titre","close button is named after its tab");
        if(close){IUIAutomationInvokePattern* invoke=nullptr;if(SUCCEEDED(close->GetCurrentPatternAs(UIA_InvokePatternId,__uuidof(IUIAutomationInvokePattern),(void**)&invoke))&&invoke){invoke->Invoke();invoke->Release();}}
        Expect(WaitFor([&]{size_t n=0;OnUi([&]{n=pages.size();});return n==2;}),"Invoke on a close button closes that tab");
        BSTR b=nullptr;Expect(untitled&&untitled->get_CurrentName(&b)==(HRESULT)UIA_E_ELEMENTNOTAVAILABLE,"a closed tab's element is no longer available");
        if(b)SysFreeString(b);}
    Release(items);

    // 4. Keyboard focus in the chrome raises focus events with the control's name.
    FocusHandler focus;uia->AddFocusChangedEventHandler(nullptr,&focus);
    OnUi([]{SetFocus(window);keyboardFocus=0;WndProc(window,WM_KEYDOWN,VK_TAB,0);});
    Expect(WaitFor([&]{return focus.Last()==L"Liste des onglets";}),"Tab announces « Liste des onglets »");
    OnUi([]{WndProc(window,WM_KEYDOWN,VK_TAB,0);});
    Expect(WaitFor([&]{return focus.Last()==L"Accueil";}),"next Tab announces the first tab");
    {IUIAutomationElement* focused=nullptr;bool has=false;
        IUIAutomationElement* root=nullptr;uia->ElementFromHandle(window,&root);
        IUIAutomationCondition* condition=nullptr;VARIANT v;v.vt=VT_BOOL;v.boolVal=VARIANT_TRUE;uia->CreatePropertyCondition(UIA_HasKeyboardFocusPropertyId,v,&condition);
        if(root&&condition&&SUCCEEDED(root->FindFirst(TreeScope_Descendants,condition,&focused))&&focused){has=Name(focused)==L"Accueil";focused->Release();}
        if(condition)condition->Release();if(root)root->Release();
        Expect(has,"HasKeyboardFocus is set on the focused tab");}
    uia->RemoveFocusChangedEventHandler(&focus);

    // 5. "Lien copié" is spoken after a successful copy.
    IUIAutomation5* uia5=nullptr;
    if(SUCCEEDED(uia->QueryInterface(__uuidof(IUIAutomation5),(void**)&uia5))&&uia5){
        NotificationHandler note;IUIAutomationElement* root=nullptr;uia->ElementFromHandle(window,&root);
        uia5->AddNotificationEventHandler(root,TreeScope_Subtree,nullptr,&note);
        OnUi([]{AnnounceCopied();});
        Expect(WaitFor([&]{return note.Text()==L"Lien copié";}),"copy confirmation notification « Lien copié »");
        uia5->RemoveNotificationEventHandler(root,&note);root->Release();uia5->Release();
    }else std::puts("skip notification check (IUIAutomation5 unavailable)");

    // 6. While editing, the real EDIT (value, text, caret) carries the address label
    //    and is the only address element.
    OnUi([]{Command('L');});
    {IUIAutomationElement* edit=nullptr;std::wstring name,shortcut;if(SUCCEEDED(uia->ElementFromHandle(address,&edit))&&edit){name=Name(edit);shortcut=Accelerator(edit);edit->Release();}
        Expect(name==L"Adresse ou recherche"&&shortcut==L"Ctrl+L","the address EDIT is announced « Adresse ou recherche »");}
    items=Tree();int edits=0;IUIAutomationElement* found=nullptr;
    for(auto& i:items)if(i.type==UIA_EditControlTypeId){++edits;found=i.element;}
    Expect(edits==1,"the open address appears once");
    if(found){UIA_HWND handle=nullptr;found->get_CurrentNativeWindowHandle(&handle);
        VARIANT text;BOOL hasText=SUCCEEDED(found->GetCurrentPropertyValue(UIA_IsTextPatternAvailablePropertyId,&text))&&text.vt==VT_BOOL&&text.boolVal;
        Expect((HWND)handle==address&&hasText&&Name(found)==L"Adresse ou recherche","it is the real, named EDIT with its text pattern");
        // The label merges into the EDIT: no phantom child element under it.
        IUIAutomationTreeWalker* raw=nullptr;IUIAutomationElement* child=nullptr;uia->get_RawViewWalker(&raw);
        if(raw){raw->GetFirstChildElement(found,&child);raw->Release();}
        Expect(!child,"the label adds no element of its own");if(child)child->Release();}
    Release(items);
    OnUi([]{SendMessageW(address,WM_KEYDOWN,VK_ESCAPE,0);});
    items=Tree();{auto* closed=Find(items,L"Adresse ou recherche");UIA_HWND handle=(UIA_HWND)1;if(closed)closed->get_CurrentNativeWindowHandle(&handle);
        Expect(closed&&!handle,"after Escape the address is the chrome element again");}
    Release(items);

    Expect(snapshots>0&&offThread==0,"providers run on the UI thread only");
    uia->Release();uia=nullptr;CoUninitialize();
    PostThreadMessageW(uiThread,WM_QUIT,0,0);
}

int main(){setvbuf(stdout,nullptr,_IONBF,0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);instance=GetModuleHandleW(nullptr);uiThread=GetCurrentThreadId();
    WNDCLASSW helperClass{};helperClass.lpfnWndProc=HelperProc;helperClass.hInstance=instance;helperClass.lpszClassName=L"BrowserAccessibilityTestHelper";RegisterClassW(&helperClass);
    helper=CreateWindowW(helperClass.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);
    WNDCLASSW wc{};wc.lpfnWndProc=WndProc;wc.hInstance=instance;wc.lpszClassName=L"BrowserAccessibilityTest";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);
    CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"browser",WS_POPUP|WS_CLIPCHILDREN,0,0,1180,815,nullptr,nullptr,instance,nullptr);
    if(!window||!accessibility.Attached()){std::fprintf(stderr,"FAIL: window or UI Automation bridge unavailable\n");return 1;}
    // Providers read the browser's globals: every snapshot must be taken on this (UI) thread.
    {auto host=AccessibleHost();auto nodes=host.nodes;host.nodes=[nodes]{if(GetCurrentThreadId()!=uiThread)++offThread;++snapshots;return nodes();};accessibility.Attach(host);}
    auto make=[](const wchar_t* title,const wchar_t* uri){auto t=std::make_shared<Tab>();t->title=title;t->uri=uri;t->home=!*uri;return t;};
    pages={make(L"Accueil",L""),make(L"Exemple",L"https://example.com/"),make(L"",L"")};current=1;
    chrome.width=1180;Update();ShowWindow(window,SW_SHOWNOACTIVATE);
    std::thread client(Client);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    client.join();
    DestroyWindow(window);DestroyWindow(helper);CoUninitialize();
    if(failures){std::fprintf(stderr,"%d accessibility check(s) failed\n",failures);return 1;}
    std::puts("PASS: screen readers see the chrome");return 0;
}
