#pragma once
// UI Automation provider for the owner-drawn chrome (tabs, toolbar, window buttons).
//
// The window answers WM_GETOBJECT(UiaRootObjectId) with a fragment root whose
// children are rebuilt on demand from the host's current state, so a screen reader
// (Narrator, NVDA, JAWS) sees named buttons, a tab list with selectable tab items,
// enabled/disabled states and keyboard focus. Child HWNDs (WebView2, the address
// EDIT) keep their own providers and patterns; IRawElementProviderHwndOverride only
// adds the name the host gives them (the EDIT has no visible label).
//
// Elements are identified by a stable 64-bit key chosen by the host, never by a
// pointer: every call re-reads the host snapshot, so an element whose control has
// gone (closed tab) answers UIA_E_ELEMENTNOTAVAILABLE instead of describing a
// different control.
//
// uiautomationcore.dll and oleaut32.dll are loaded at run time: no new link
// dependency for the browser or the native fixtures, and the browser still starts
// on systems without UiaRaiseNotificationEvent (before Windows 10 1709).
#include <windows.h>
#include <ole2.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace Accessibility {

constexpr HRESULT NotAvailable=static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
constexpr HRESULT NotEnabled=static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED);
constexpr HRESULT InvalidOperation=static_cast<HRESULT>(UIA_E_INVALIDOPERATION);

struct Api {
    decltype(&::UiaReturnRawElementProvider) returnProvider=nullptr;
    decltype(&::UiaHostProviderFromHwnd) hostFromHwnd=nullptr;
    decltype(&::UiaRaiseAutomationEvent) raiseEvent=nullptr;
    decltype(&::UiaRaiseStructureChangedEvent) raiseStructure=nullptr;
    decltype(&::UiaRaiseNotificationEvent) raiseNotification=nullptr; // Windows 10 1709+
    decltype(&::UiaClientsAreListening) listening=nullptr;
    decltype(&::UiaDisconnectProvider) disconnect=nullptr;
    decltype(&::SysAllocString) allocString=nullptr;
    decltype(&::SysFreeString) freeString=nullptr;
    decltype(&::SafeArrayCreateVector) createVector=nullptr;
    decltype(&::SafeArrayPutElement) putElement=nullptr;
    bool Ready()const{return returnProvider&&hostFromHwnd&&allocString&&freeString&&createVector&&putElement;}
    static const Api& Get(){static const Api api=Load();return api;}
private:
    template<class F> static void Bind(F& f,HMODULE module,const char* name){
        FARPROC p=module?GetProcAddress(module,name):nullptr;
        static_assert(sizeof(f)==sizeof(p),"function pointer size");
        std::memcpy(&f,&p,sizeof(p));
    }
    static Api Load(){
        Api api;
        HMODULE uia=LoadLibraryExW(L"uiautomationcore.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE ole=LoadLibraryExW(L"oleaut32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        Bind(api.returnProvider,uia,"UiaReturnRawElementProvider");
        Bind(api.hostFromHwnd,uia,"UiaHostProviderFromHwnd");
        Bind(api.raiseEvent,uia,"UiaRaiseAutomationEvent");
        Bind(api.raiseStructure,uia,"UiaRaiseStructureChangedEvent");
        Bind(api.raiseNotification,uia,"UiaRaiseNotificationEvent");
        Bind(api.listening,uia,"UiaClientsAreListening");
        Bind(api.disconnect,uia,"UiaDisconnectProvider");
        Bind(api.allocString,ole,"SysAllocString");
        Bind(api.freeString,ole,"SysFreeString");
        Bind(api.createVector,ole,"SafeArrayCreateVector");
        Bind(api.putElement,ole,"SafeArrayPutElement");
        return api; // the modules stay loaded for the process lifetime
    }
};

enum class Role { Button, Tab, TabItem, Edit, ToolBar, List, ListItem };
// One accessible control. `parent` 0 is the window (fragment root). Nodes are listed
// in depth-first reading order: a parent precedes its children.
struct Node {
    long long key=0,parent=0;
    Role role=Role::Button;
    std::wstring name,accelerator,help,status,automationId,value;
    RECT bounds{};            // screen pixels
    bool enabled=true,focusable=true,selected=false,offscreen=false;
    int position=0,count=0;   // 1-based place in its set (tab items)
};
// Callbacks run on the window's UI thread (providers use COM threading in its STA).
// Actions must not block: invoke/select/setValue are expected to post their work.
struct Host {
    HWND window=nullptr;
    std::function<std::vector<Node>()> nodes;
    std::function<long long()> focused;            // node with keyboard focus, 0 = none
    std::function<bool(HWND,Node&)> label;          // name/shortcut for a child HWND, if any
    std::function<void(long long)> invoke,select,focus;
    std::function<void(long long,const std::wstring&)> setValue;
};

class Root;
struct Context {
    LONG refs=1;const Api& api=Api::Get();Host host;bool alive=true;Root* root=nullptr;
    void AddRef(){InterlockedIncrement(&refs);}
    void Release(){if(!InterlockedDecrement(&refs))delete this;}
    std::vector<Node> Nodes()const{return alive&&host.nodes?host.nodes():std::vector<Node>{};}
    bool Find(long long key,Node& out)const{for(auto& n:Nodes())if(n.key==key){out=n;return true;}return false;}
};

inline void SetBool(VARIANT* v,bool value){v->vt=VT_BOOL;v->boolVal=value?VARIANT_TRUE:VARIANT_FALSE;}
inline void SetInt(VARIANT* v,int value){v->vt=VT_I4;v->lVal=value;}
inline HRESULT SetString(const Api& api,VARIANT* v,const std::wstring& value){
    if(value.empty())return S_OK;
    v->bstrVal=api.allocString(value.c_str());if(!v->bstrVal)return E_OUTOFMEMORY;
    v->vt=VT_BSTR;return S_OK;
}
inline int ControlType(Role role){
    switch(role){
    case Role::Tab:return UIA_TabControlTypeId;
    case Role::TabItem:return UIA_TabItemControlTypeId;
    case Role::Edit:return UIA_EditControlTypeId;
    case Role::ToolBar:return UIA_ToolBarControlTypeId;
    case Role::List:return UIA_ListControlTypeId;
    case Role::ListItem:return UIA_ListItemControlTypeId;
    default:return UIA_ButtonControlTypeId;
    }
}
constexpr ProviderOptions Options=ProviderOptions(ProviderOptions_ServerSideProvider|ProviderOptions_UseComThreading);

class Element final:public IRawElementProviderSimple,public IRawElementProviderFragment,public IInvokeProvider,
    public ISelectionProvider,public ISelectionItemProvider,public IValueProvider {
    LONG refs=1;Context* context;long long key;
public:
    Element(Context* c,long long k):context(c),key(k){context->AddRef();}
    ~Element(){context->Release();}
    long long Key()const{return key;}
    static IRawElementProviderFragment* Make(Context* c,long long key);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(iid==__uuidof(IRawElementProviderFragment))*out=static_cast<IRawElementProviderFragment*>(this);
        else if(iid==__uuidof(IInvokeProvider))*out=static_cast<IInvokeProvider*>(this);
        else if(iid==__uuidof(ISelectionProvider))*out=static_cast<ISelectionProvider*>(this);
        else if(iid==__uuidof(ISelectionItemProvider))*out=static_cast<ISelectionItemProvider*>(this);
        else if(iid==__uuidof(IValueProvider))*out=static_cast<IValueProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out)override{if(!out)return E_POINTER;*out=Options;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern,IUnknown** out)override{
        if(!out)return E_POINTER;*out=nullptr;Node n;if(!context->Find(key,n))return NotAvailable;
        const bool supported=(pattern==UIA_InvokePatternId&&n.role==Role::Button)||
            (pattern==UIA_SelectionPatternId&&(n.role==Role::Tab||n.role==Role::List))||
            (pattern==UIA_SelectionItemPatternId&&(n.role==Role::TabItem||n.role==Role::ListItem))||
            (pattern==UIA_ValuePatternId&&n.role==Role::Edit);
        if(supported){*out=static_cast<IRawElementProviderSimple*>(this);AddRef();}
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property,VARIANT* out)override{
        if(!out)return E_POINTER;out->vt=VT_EMPTY;Node n;if(!context->Find(key,n))return NotAvailable;
        const Api& api=context->api;
        switch(property){
        case UIA_ControlTypePropertyId:SetInt(out,ControlType(n.role));break;
        case UIA_NamePropertyId:return SetString(api,out,n.name);
        case UIA_AcceleratorKeyPropertyId:return SetString(api,out,n.accelerator);
        case UIA_HelpTextPropertyId:return SetString(api,out,n.help);
        case UIA_ItemStatusPropertyId:return SetString(api,out,n.status);
        case UIA_AutomationIdPropertyId:return SetString(api,out,n.automationId);
        case UIA_IsEnabledPropertyId:SetBool(out,n.enabled);break;
        case UIA_IsKeyboardFocusablePropertyId:SetBool(out,n.focusable);break;
        case UIA_HasKeyboardFocusPropertyId:SetBool(out,context->host.focused&&context->host.focused()==key);break;
        case UIA_IsOffscreenPropertyId:SetBool(out,n.offscreen);break;
        case UIA_PositionInSetPropertyId:if(n.position)SetInt(out,n.position);break;
        case UIA_SizeOfSetPropertyId:if(n.count)SetInt(out,n.count);break;
        default:break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out)override{
        if(!out)return E_POINTER;*out=nullptr;return S_OK;
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,IRawElementProviderFragment** out)override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out)override{
        if(!out)return E_POINTER;*out=nullptr;Node n;if(!context->Find(key,n))return NotAvailable;
        int ids[]={UiaAppendRuntimeId,(int)(key>>32),(int)(key&0xffffffff)};
        SAFEARRAY* array=context->api.createVector(VT_I4,0,3);if(!array)return E_OUTOFMEMORY;
        for(LONG i=0;i<3;++i)context->api.putElement(array,&i,&ids[i]);
        *out=array;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out)override{
        if(!out)return E_POINTER;*out={};Node n;if(!context->Find(key,n))return NotAvailable;
        if(!n.offscreen)*out={(double)n.bounds.left,(double)n.bounds.top,(double)(n.bounds.right-n.bounds.left),(double)(n.bounds.bottom-n.bounds.top)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out)override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetFocus()override{
        Node n;if(!context->Find(key,n))return NotAvailable;if(!n.enabled||!n.focusable)return NotEnabled;
        if(context->host.focus)context->host.focus(key);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out)override;

    // IInvokeProvider
    HRESULT STDMETHODCALLTYPE Invoke()override{
        Node n;if(!context->Find(key,n))return NotAvailable;if(!n.enabled)return NotEnabled;
        if(context->host.invoke)context->host.invoke(key);return S_OK;
    }
    // ISelectionProvider (tab strip)
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** out)override{
        if(!out)return E_POINTER;*out=nullptr;Node strip;if(!context->Find(key,strip))return NotAvailable;
        long long selected=0;for(auto& n:context->Nodes())if(n.parent==key&&n.selected){selected=n.key;break;}
        SAFEARRAY* array=context->api.createVector(VT_UNKNOWN,0,selected?1:0);if(!array)return E_OUTOFMEMORY;
        if(selected){auto* item=Make(context,selected);LONG i=0;context->api.putElement(array,&i,static_cast<IUnknown*>(item));item->Release();}
        *out=array;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* out)override{if(!out)return E_POINTER;*out=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* out)override{if(!out)return E_POINTER;*out=TRUE;return S_OK;}
    // ISelectionItemProvider (tab)
    HRESULT STDMETHODCALLTYPE Select()override{
        Node n;if(!context->Find(key,n))return NotAvailable;if(!n.enabled)return NotEnabled;
        if(context->host.select)context->host.select(key);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddToSelection()override{Node n;if(!context->Find(key,n))return NotAvailable;return n.selected?S_OK:InvalidOperation;}
    HRESULT STDMETHODCALLTYPE RemoveFromSelection()override{Node n;if(!context->Find(key,n))return NotAvailable;return InvalidOperation;}
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* out)override{if(!out)return E_POINTER;*out=FALSE;Node n;if(!context->Find(key,n))return NotAvailable;*out=n.selected;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** out)override{
        if(!out)return E_POINTER;*out=nullptr;Node n;if(!context->Find(key,n))return NotAvailable;
        if(n.parent)*out=static_cast<Element*>(Make(context,n.parent));
        return S_OK;
    }
    // IValueProvider (address)
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value)override{
        Node n;if(!context->Find(key,n))return NotAvailable;if(!n.enabled)return NotEnabled;
        if(context->host.setValue)context->host.setValue(key,value?value:L"");return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out)override{
        if(!out)return E_POINTER;*out=nullptr;Node n;if(!context->Find(key,n))return NotAvailable;
        *out=context->api.allocString(n.value.c_str());return *out?S_OK:E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out)override{if(!out)return E_POINTER;*out=FALSE;return S_OK;}
};

// Properties only, merged over a child HWND's own providers (its patterns and its
// place in the tree are untouched).
class Label final:public IRawElementProviderSimple {
    LONG refs=1;Context* context;HWND child;
public:
    Label(Context* c,HWND h):context(c),child(h){context->AddRef();}
    ~Label(){context->Release();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(IRawElementProviderSimple))return E_NOINTERFACE;
        *out=static_cast<IRawElementProviderSimple*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out)override{if(!out)return E_POINTER;*out=ProviderOptions(Options|ProviderOptions_OverrideProvider);return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID,IUnknown** out)override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property,VARIANT* out)override{
        if(!out)return E_POINTER;out->vt=VT_EMPTY;Node n;
        if(!context->alive||!context->host.label||!context->host.label(child,n))return S_OK;
        switch(property){
        case UIA_NamePropertyId:return SetString(context->api,out,n.name);
        case UIA_AcceleratorKeyPropertyId:return SetString(context->api,out,n.accelerator);
        case UIA_HelpTextPropertyId:return SetString(context->api,out,n.help);
        case UIA_AutomationIdPropertyId:return SetString(context->api,out,n.automationId);
        default:return S_OK;
        }
    }
    // Ties the override to its window, so it merges with it instead of becoming a child.
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!context->alive||!IsWindow(child))return NotAvailable;
        return context->api.hostFromHwnd(child,out);
    }
};

class Root final:public IRawElementProviderSimple,public IRawElementProviderFragment,
    public IRawElementProviderFragmentRoot,public IRawElementProviderHwndOverride {
    LONG refs=1;Context* context;
public:
    explicit Root(Context* c):context(c){context->AddRef();context->root=this;}
    ~Root(){if(context->root==this)context->root=nullptr;context->Release();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(iid==__uuidof(IRawElementProviderFragment))*out=static_cast<IRawElementProviderFragment*>(this);
        else if(iid==__uuidof(IRawElementProviderFragmentRoot))*out=static_cast<IRawElementProviderFragmentRoot*>(this);
        else if(iid==__uuidof(IRawElementProviderHwndOverride))*out=static_cast<IRawElementProviderHwndOverride*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}

    // The window's own name, type, bounds and focus come from its host provider.
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out)override{if(!out)return E_POINTER;*out=Options;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID,IUnknown** out)override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID,VARIANT* out)override{if(!out)return E_POINTER;out->vt=VT_EMPTY;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!context->alive)return NotAvailable;
        return context->api.hostFromHwnd(context->host.window,out);
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,IRawElementProviderFragment** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!context->alive)return NotAvailable;
        if(direction!=NavigateDirection_FirstChild&&direction!=NavigateDirection_LastChild)return S_OK;
        long long found=0;for(auto& n:context->Nodes())if(!n.parent){found=n.key;if(direction==NavigateDirection_FirstChild)break;}
        if(found)*out=Element::Make(context,found);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out)override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out)override{if(!out)return E_POINTER;*out={};return S_OK;}
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out)override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetFocus()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out)override{
        if(!out)return E_POINTER;*out=this;AddRef();return S_OK;
    }
    // Deepest control under a physical screen point (a tab's close button wins over its tab).
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x,double y,IRawElementProviderFragment** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!context->alive)return NotAvailable;
        long long found=0;
        for(auto& n:context->Nodes())if(!n.offscreen&&x>=n.bounds.left&&x<n.bounds.right&&y>=n.bounds.top&&y<n.bounds.bottom)found=n.key;
        if(found)*out=Element::Make(context,found);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!context->alive)return NotAvailable;
        const long long key=context->host.focused?context->host.focused():0;
        Node n;if(key&&context->Find(key,n))*out=Element::Make(context,key);
        return S_OK;
    }
    // Names a child HWND the host labels (the address EDIT).
    HRESULT STDMETHODCALLTYPE GetOverrideProviderForHwnd(HWND child,IRawElementProviderSimple** out)override{
        if(!out)return E_POINTER;*out=nullptr;Node n;
        if(context->alive&&context->host.label&&context->host.label(child,n))*out=new Label(context,child);
        return S_OK;
    }
};

inline IRawElementProviderFragment* Element::Make(Context* c,long long key){return new Element(c,key);}
inline HRESULT Element::get_FragmentRoot(IRawElementProviderFragmentRoot** out){
    if(!out)return E_POINTER;*out=nullptr;if(!context->alive||!context->root)return NotAvailable;
    *out=context->root;(*out)->AddRef();return S_OK;
}
inline HRESULT Element::Navigate(NavigateDirection direction,IRawElementProviderFragment** out){
    if(!out)return E_POINTER;*out=nullptr;
    const auto nodes=context->Nodes();int self=-1;
    for(int i=0;i<(int)nodes.size();++i)if(nodes[i].key==key){self=i;break;}
    if(self<0)return NotAvailable;
    const long long parent=nodes[self].parent;long long found=0;
    switch(direction){
    case NavigateDirection_Parent:
        if(!parent){if(context->root){*out=context->root;(*out)->AddRef();}return S_OK;}
        found=parent;break;
    case NavigateDirection_NextSibling:
        for(int i=self+1;i<(int)nodes.size();++i)if(nodes[i].parent==parent){found=nodes[i].key;break;}
        break;
    case NavigateDirection_PreviousSibling:
        for(int i=self-1;i>=0;--i)if(nodes[i].parent==parent){found=nodes[i].key;break;}
        break;
    case NavigateDirection_FirstChild:
        for(auto& n:nodes)if(n.parent==key){found=n.key;break;}
        break;
    case NavigateDirection_LastChild:
        for(auto& n:nodes)if(n.parent==key)found=n.key;
        break;
    }
    if(found)*out=Make(context,found);
    return S_OK;
}

// Owned by the host window: answers WM_GETOBJECT and raises events. All calls on
// the window's UI thread. Without uiautomationcore every call is a quiet no-op.
class Bridge {
    Context* context=nullptr;Root* root=nullptr;
    const Api& api()const{return Api::Get();}
    template<class F> void WithElement(long long key,F f){
        if(!root||!key)return;Node n;if(!context->Find(key,n))return;
        auto* element=static_cast<Element*>(Element::Make(context,key));f(element);element->Release();
    }
public:
    ~Bridge(){Detach();}
    void Attach(Host host){
        Detach();if(!api().Ready())return;
        context=new Context();context->host=std::move(host);root=new Root(context);
    }
    bool Attached()const{return root!=nullptr;}
    // True when some UIA client may receive events (cheap; checked before building any).
    bool Listening()const{return root&&(!api().listening||api().listening());}
    bool OnGetObject(HWND window,WPARAM w,LPARAM l,LRESULT& result){
        if(!root||window!=context->host.window||static_cast<long>(l)!=static_cast<long>(UiaRootObjectId))return false;
        result=api().returnProvider(window,w,l,static_cast<IRawElementProviderSimple*>(root));return true;
    }
    void Raise(long long key,EVENTID event){
        if(!api().raiseEvent||!Listening())return;
        WithElement(key,[&](Element* e){api().raiseEvent(static_cast<IRawElementProviderSimple*>(e),event);});
    }
    void ChildrenChanged(long long key){
        if(!api().raiseStructure||!Listening())return;
        WithElement(key,[&](Element* e){int ids[]={UiaAppendRuntimeId,(int)(key>>32),(int)(key&0xffffffff)};
            api().raiseStructure(static_cast<IRawElementProviderSimple*>(e),StructureChangeType_ChildrenInvalidated,ids,3);});
    }
    // Spoken confirmation of an action that changes no focus (e.g. "Lien copié").
    void Notify(long long key,const wchar_t* text,const wchar_t* activity){
        if(!api().raiseNotification||!Listening())return;
        WithElement(key,[&](Element* e){
            BSTR message=api().allocString(text),id=api().allocString(activity);
            if(message&&id)api().raiseNotification(static_cast<IRawElementProviderSimple*>(e),NotificationKind_ActionCompleted,NotificationProcessing_ImportantMostRecent,message,id);
            if(message)api().freeString(message);if(id)api().freeString(id);
        });
    }
    // Window destroyed: every outstanding element answers ELEMENTNOTAVAILABLE.
    void Detach(){
        if(!root)return;
        HWND window=context->host.window;context->alive=false;
        if(window&&IsWindow(window))api().returnProvider(window,0,0,nullptr);
        if(api().disconnect)api().disconnect(static_cast<IRawElementProviderSimple*>(root));
        root->Release();root=nullptr;context->Release();context=nullptr;
    }
};

} // namespace Accessibility
