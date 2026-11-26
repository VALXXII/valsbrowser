// Profiles, host side (included by main.cpp after audio-integration.h). Paper p-I-0, P01–P07;
// data model and lifecycle in profiles.h / profile-color.h (backend), UI in profile-menu.h,
// profile-dialog.h, profile-editor.h and settings-panel.h (Réglages › Profils).
//
//  * One profile per window, one process per window. Startup picks the profile (--profile <id>,
//    else the last used, else « Personnel ») and points every data path at its folder BEFORE the
//    window session, settings or WebView2 are touched. The default profile keeps the legacy layout.
//  * The profile button replaces the flower (chrome region 900); Ctrl+Maj+M opens the menu too.
//    Choosing another profile brings its most recent window forward or opens one; this window
//    never changes profile. Private windows: flower, « Privée » chip, reference accent, no menu.
//  * Accent: the window's accent is the profile's fill (OKLCH C .14, L solved for white ≥ 4.6:1);
//    danger and the white focus ring never change. Edits to a profile reach every window of it.
//  * Windows talk through WM_COPYDATA (« PROF ») and a registered broadcast message:
//      tab <index>            bring this window forward on that tab (preview → Entrée)
//      open <link or path>    open it in a new tab and come forward (main.cpp HandOverLaunchTarget)
//      close-for-delete <id>  close (keeping the session for « Rouvrir ») — the profile is deleted
//      delete-profile <id>    this window takes over the delete of another window's own profile
//    BrowserProfilesChanged   the registry changed: reload it, repaint, re-apply the accent.
#include "profiles.h"
#include "profile-menu.h"
#include "profile-dialog.h"
#include "profile-delete-dialog.h"
#include "profile-args.h"

namespace PM=NativeChrome::ProfileMenu; namespace PE=NativeChrome::ProfileEditor;
std::wstring profileRoot, profileId=Profiles::DefaultId; Profiles::Registry profileRegistry; bool profilesEnabled=false;
PM::Menu profileMenu; NativeChrome::ProfileDialog::Dialog profileDialog;
constexpr ULONG_PTR ProfileCopyData=0x464F5250; // 'PROF'
constexpr UINT_PTR ProfileUndoTimerId=0x5046, ProfileCloseWaitTimerId=0x5047, ProfilePublishTimerId=0x5048;
UINT ProfilesChangedMessage(){static const UINT m=RegisterWindowMessageW(L"BrowserProfilesChanged");return m;}
std::wstring ProfileExe(){std::wstring exe(32768,L'\0');const DWORD n=GetModuleFileNameW(nullptr,exe.data(),(DWORD)exe.size());exe.resize(n<exe.size()?n:0);return exe;}
const Profiles::Profile* CurrentProfile(){return profileRegistry.Find(profileId);}
Profiles::Profile DefaultProfileLook(){Profiles::Profile p;p.id=Profiles::DefaultId;p.name=Profiles::DefaultName();p.icon=Profiles::DefaultIcon;p.hue=0;p.intensity=0;p.neutral=true;return p;}
void TabMenu(int anchor);int KeyboardAnchor();void StartWorkspaces();void OpenHandedTarget(const std::wstring& target);
// Same-window profiles (profile-workspace.h, end of main.cpp).
bool SwitchWorkspace(const std::wstring& id);bool LeaveProfileHere(const std::wstring& id,bool openOther);std::vector<Profiles::WindowSlot> ThisWindowSlots();void RestoreWindowSet();int EvictProfileHere(const std::wstring& id);void WriteThisWindowSet();bool OpenSessionTabHere(const std::wstring& id,const std::wstring& session,int tab);void DisposeParkedWorkspaces();void MarkWorkspacePublish();void PublishDirtyWorkspaces();int WorkspaceQaCommand(const std::wstring& text);void EngineHeld(const std::wstring& id);
// « --profile <id> » for every relaunch of this window (orphan relaunch, handover, engine recovery):
// a session of Travail must be adopted by a Travail window, whatever « last used » says later.
std::wstring ProfileArgument(){return profilesEnabled&&!privateWindow&&Profiles::ValidId(profileId)?L" --profile "+profileId:L"";}
// Review R-2: the profile a private window opened from here belongs to (it reads that profile's vault, read-only):
// this window's profile; for a private window, the profile it was itself opened from (so it stays the same).
std::wstring privateOpener;
std::wstring PrivateOpenerProfile(){return privateWindow?privateOpener:profilesEnabled?profileId:L"";}

// ---- startup ------------------------------------------------------------------------------------
// Called right after ResolveBrowserPaths(): `dataDir` is the data root. Smoke and private windows
// keep the root as is (no registry write): private windows never belong to a profile.
void SelectProfile(const wchar_t* args){
    profileRoot=dataDir;
    if(privateWindow){   // review R-2: the vault of the profile this private window was opened from, read-only (none if unknown)
        const std::wstring opener=ProfileArgs::Value(args?args:L"",L"--profile");privateVaultDir=Profiles::PrivateReadDir(profileRoot,opener);
        privateOpener=!privateVaultDir.empty()&&opener!=Profiles::DefaultId&&Profiles::ValidId(opener)?opener:L"";}
    if(privateWindow||smoke||motionLabMode){Profiles::Profile d=DefaultProfileLook();profileRegistry.items={d};profilesEnabled=!privateWindow;return;}
    std::wstring requested;
    if(const wchar_t* p=args?wcsstr(args,L"--profile "):nullptr){p+=10;while(*p==L' ')++p;const wchar_t* e=p;while(*e&&*e!=L' ')++e;requested.assign(p,e);}
    profileRegistry=Profiles::Load(profileRoot);
    profileId=Profiles::Active(profileRegistry,requested);
    if(profileId.empty())profileId=Profiles::DefaultId;
    const auto paths=Profiles::ResolvePaths(profileRoot,profileId);
    dataDir=paths.dataDir;profileDir=paths.profileDir;sessionFile=paths.sessionFile;downloadsFile=paths.downloadsFile;settingsFile=paths.settingsFile;if(!paths.logFile.empty())logFile=paths.logFile;
    std::error_code ec;std::filesystem::create_directories(std::filesystem::path(dataDir),ec);
    if(!profileRegistry.readOnly&&profileRegistry.last!=profileId)Profiles::SetLast(profileRoot,profileRegistry,profileId);
    profilesEnabled=true;
}

// ---- window chrome: button + accent --------------------------------------------------------------
D2D1_COLOR_F ProfileInk(const Profiles::Profile& p){return PE::Color(PE::InkOf(p));}
// P05 « Accent de la fenêtre »: a profile's colour change reaches its open windows in a 120 ms fade, live
// during the gesture (each step starts from the accent on screen); reduced motion applies it at once.
// Switching windows never fades (each window already has its accent). The button's own icon / ink
// transitions live in Chrome::SetProfileButton.
constexpr UINT_PTR ProfileAnimTimerId=0x5049; constexpr float AccentFadeMs=120;
struct AccentTween{bool active=false;NativeChrome::Design::Rgb fromFill{},toFill{},fromInk{},toInk{},fromSelection{},toSelection{};ULONGLONG start=0;} accentTween;
NativeChrome::Design::Rgb RgbOf(unsigned long v){return {((v>>16)&255)/255.f,((v>>8)&255)/255.f,(v&255)/255.f};}
NativeChrome::Design::Rgb Mix(NativeChrome::Design::Rgb a,NativeChrome::Design::Rgb b,float t){return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};}
// The annotation mode's surfaces (band, note, launcher, history, connect modal, page frame) follow the accent too:
// AnnotationAccentChanged (annotation-mode.h) at each change and at each step of the fade, no clock of its own.
void AnnotationAccentChanged();
// The address field's selection run (Accent::Selection) repaints with the accent, same steps, no clock of its own.
void AddressSelectionChanged();
void SetAccents(unsigned long fill,unsigned long ink,unsigned long selection,bool animate){
    namespace A=NativeChrome::Design::Accent;
    A::SettledCurrent()=RgbOf(fill);
    if(!animate||ReducedMotionPreferred()){A::Current()=RgbOf(fill);A::InkCurrent()=RgbOf(ink);A::SelectionCurrent()=RgbOf(selection);accentTween.active=false;AnnotationAccentChanged();AddressSelectionChanged();return;}
    accentTween.fromFill=A::Get();accentTween.fromInk=A::Ink();accentTween.toFill=RgbOf(fill);accentTween.toInk=RgbOf(ink);
    accentTween.fromSelection=A::SelectionCurrent();accentTween.toSelection=RgbOf(selection);
    accentTween.start=GetTickCount64();accentTween.active=true;if(window)SetTimer(window,ProfileAnimTimerId,15,nullptr);
    AnnotationAccentChanged();AddressSelectionChanged();
}
void ProfileAnimTick(){
    namespace A=NativeChrome::Design::Accent;
    if(accentTween.active){
        const float u=std::clamp((GetTickCount64()-accentTween.start)/AccentFadeMs,0.f,1.f),t=NativeChrome::Motion::Eased(u,NativeChrome::Motion::Curve::EaseOut);
        A::Current()=Mix(accentTween.fromFill,accentTween.toFill,t);A::InkCurrent()=Mix(accentTween.fromInk,accentTween.toInk,t);
        A::SelectionCurrent()=Mix(accentTween.fromSelection,accentTween.toSelection,t);
        if(u>=1)accentTween.active=false;
        AnnotationAccentChanged();AddressSelectionChanged();
    }
    const bool button=chrome.ProfileButtonAnimating(NativeChrome::Motion::Now());
    InvalidateChrome();
    if(!accentTween.active&&!button)KillTimer(window,ProfileAnimTimerId);
}
void ApplyProfileChrome(bool animate=false){
    // Private window: the same slot, the incognito identity (Phosphor eye-slash, the existing private glyph) in
    // the reference blue's ink (OKLCH L .78, like every profile ink); the window accent stays the reference
    // blue. No profile, no registry, no sidecar.
    if(privateWindow){chrome.profileButton.privateWindow=true;chrome.SetProfileButton(TR(a11y_private_window),NativeChrome::Design::PrivateChip::eyeSlash,PE::Color(ProfileColor::Ink(266,.14f)),false);InvalidateChrome();return;}
    if(!profilesEnabled){chrome.profileButton.shown=false;return;}
    const Profiles::Profile* p=CurrentProfile();Profiles::Profile fallback=DefaultProfileLook();if(!p)p=&fallback;
    chrome.SetProfileButton(p->name,ProfileIcons::Fill(p->icon.c_str()),ProfileInk(*p),animate);
    if(animate&&chrome.profileButton.animating&&window)SetTimer(window,ProfileAnimTimerId,15,nullptr);
    // Neutral (the default grey « Personnel »): the reference blue stays the accent (Paper P07).
    // Fill (filled controls under white text) and ink (progress, download ring, checks): one profile colour.
    // A neutral profile keeps the blue FILL (white text needs it) but its ink is its own grey #B3B3B3: the
    // download ring and its check never show a colour the profile doesn't have (user report 2026-11-14).
    // The address selection is the profile's own fill in every case: the neutral profile selects in its grey
    // #747474 (FillOf), not in the reference blue its buttons keep.
    const auto ink=PE::InkOf(*p);const unsigned long inkRgb=(unsigned long)((ink.r<<16)|(ink.g<<8)|ink.b);
    const auto own=PE::FillOf(*p);const unsigned long ownRgb=(unsigned long)((own.r<<16)|(own.g<<8)|own.b);
    SetAccents(p->neutral?NativeChrome::Design::Accent::defaultRgb:ownRgb,inkRgb,ownRgb,animate);
    InvalidateChrome();
}
// Sidecar: tab titles/urls of this window for the other windows' menus (count, preview, routing).
// `cached`: the workspace in the globals is not the one on screen (parked or cold in this window).
void PublishProfileWindow(bool cached){
    if(!profilesEnabled||smoke||privateWindow||windowId.empty()||!window)return;
    std::vector<std::wstring> titles,urls;for(auto& t:pages){if(!t||t->closed)continue;titles.push_back(t->title);urls.push_back(t->home?L"":t->uri);}
    Profiles::PublishWorkspace(Profiles::ResolvePaths(profileRoot,profileId),profileId,windowId,window,titles,urls,current,cached||WorkspaceParked()?Profiles::WorkspaceState::Cached:Profiles::WorkspaceState::Active);
}
WindowSessions::Adoption AdoptThisWindow(){return Profiles::AdoptWorkspace(profileRoot,profileId,sessionFile,windowRequested,settings.restoreTabs);}
// Title changes: republished at most every 500 ms.
// The owner of the change (the workspace in the globals, parked or not) is marked; the timer publishes ITS sidecar.
void SchedulePublishProfileWindow(){if(profilesEnabled&&!smoke&&!privateWindow&&window){MarkWorkspacePublish();SetTimer(window,ProfilePublishTimerId,500,nullptr);}}
void UnpublishProfileWindow(){if(!profilesEnabled||smoke||privateWindow||windowId.empty())return;Profiles::UnpublishWindow(Profiles::ResolvePaths(profileRoot,profileId),windowId,window);}
// Every browser window (all profiles) hears registry changes.
void BroadcastProfilesChanged(){
    EnumWindows([](HWND h,LPARAM)->BOOL{wchar_t cls[64]={};if(h!=window&&GetClassNameW(h,cls,64)&&!wcscmp(cls,Profiles::WindowClass))PostMessageW(h,ProfilesChangedMessage(),0,0);return TRUE;},0);
}
void ReloadProfiles(){if(!profilesEnabled||smoke)return;profileRegistry=Profiles::Load(profileRoot);ApplyProfileChrome(true);if(settingsDrawer.Visible())settingsDrawer.Changed();}
// Answer: 1 accepted, 2 refused (a delete take-over while the receiver has its own undo pending), 0 none.
DWORD_PTR SendProfileCommand(HWND target,const std::wstring& text){COPYDATASTRUCT cd{ProfileCopyData,(DWORD)((text.size()+1)*sizeof(wchar_t)),(PVOID)text.c_str()};DWORD_PTR result=0;if(!SendMessageTimeoutW(target,WM_COPYDATA,(WPARAM)window,(LPARAM)&cd,SMTO_ABORTIFHUNG,2000,&result))return 0;return result;}

// ---- the menu ------------------------------------------------------------------------------------
std::wstring SiteHost(const std::wstring& url){std::wstring h=url;const auto s=h.find(L"://");if(s!=std::wstring::npos)h=h.substr(s+3);const auto e=h.find_first_of(L"/?#:");if(e!=std::wstring::npos)h=h.substr(0,e);return h;}
PM::Model ProfileMenuModel(){
    PM::Model m;m.tabCount=(int)pages.size();
    for(const auto& p:profileRegistry.items){
        PM::Profile row;row.id=p.id;row.name=p.name;row.icon=p.icon;row.ink=ProfileInk(p);row.current=p.id==profileId;
        const auto live=smoke?std::vector<Profiles::WindowInfo>{}:Profiles::LiveWindows(profileRoot,p.id);
        row.windows=(int)live.size();
        for(const auto& w:live){
            if(row.current&&w.sessionId==windowId){   // this window: the live tabs
                for(int i=0;i<(int)pages.size();++i){PM::Tab t;t.title=pages[i]->title;t.site=pages[i]->home?L"":SiteHost(pages[i]->uri);t.icon=pages[i]->favicon.image;if(!t.icon&&!t.site.empty())t.icon=SiteIcon(t.site);t.session=w.sessionId;t.index=i;row.tabs.push_back(t);}
                continue;
            }
            for(size_t i=0;i<w.urls.size();++i){PM::Tab t;t.title=i<w.titles.size()?w.titles[i]:L"";t.site=SiteHost(w.urls[i]);if(t.title.empty())t.title=t.site.empty()?std::wstring(TR(tab_new)):t.site;if(!t.site.empty())t.icon=SiteIcon(t.site);t.session=w.sessionId;t.index=(int)i;row.tabs.push_back(t);}
        }
        if(row.current&&std::none_of(live.begin(),live.end(),[](const Profiles::WindowInfo& w){return w.sessionId==windowId;})){row.windows=std::max(row.windows,1);}
        m.profiles.push_back(std::move(row));
    }
    return m;
}
RECT ProfileButtonScreen(){FocusAnchor(L"profile menu"); /* i18n-exempt: log tag. Mode Focus: the profile menu (Ctrl+Maj+M too) opens under the panel at rest */if(chrome.sidebar){const RECT c=RegionScreen(900);if(c.right>c.left)return c;} /* the side layouts: the column's profile button (in Focus, the column back at rest) */const auto p=NativeChrome::Design::ProfileButton::Plate();RECT r=ChromePx({p.left,p.top,p.right,p.bottom});MapWindowPoints(window,nullptr,(POINT*)&r,2);return r;}
void OpenProfileDialog(bool keyboard);
void OpenTabIn(const std::wstring& id,const std::wstring& session,int tab){
    if(id==profileId&&session==windowId){Select(tab);return;}
    if(OpenSessionTabHere(id,session,tab))return;   // a profile cached in this window: switch, then that tab
    for(const auto& w:Profiles::LiveWindows(profileRoot,id))if(w.sessionId==session&&w.hwnd){AllowSetForegroundWindow(ASFW_ANY);SendProfileCommand(w.hwnd,L"tab " /* i18n-exempt: window protocol */+std::to_wstring(tab));if(IsIconic(w.hwnd))ShowWindow(w.hwnd,SW_RESTORE);SetForegroundWindow(w.hwnd);return;}
    Profiles::FocusOrLaunch(profileRoot,id,ProfileExe());
}
// User requirement (2026-11-14): a click switches THIS window (same HWND, same process); the profile left
// stays cached in this window. Another window is the explicit « Ouvrir dans une nouvelle fenêtre ».
void SwitchProfile(const std::wstring& id){
    if(id==profileId){SetForegroundWindow(window);return;}
    if(!SwitchWorkspace(id))Log(L"PROFILES switch failed "+id);
}
// The row's context menu: one explicit action, distinct from the click.
void OpenProfileInNewWindow(const std::wstring& id){if(Profiles::Launch(ProfileExe(),id))Log(L"PROFILES new window "+id);else Log(L"PROFILES new window failed "+id);}
void ProfileRowMenu(const std::wstring& id,POINT at,bool keyboard){
    std::vector<CM::Item> items{CM::Command(1,TR(toast_open_in_new_window))};
    const int chosen=contextMenu.Track(items,at,nullptr,keyboard,ReducedMotionPreferred());
    if(chosen==1)OpenProfileInNewWindow(id);
}
void ShowProfileSettings();
void ProfileMenuOpen(bool keyboard){
    if(!profilesEnabled&&!privateWindow){TabMenu(KeyboardAnchor());return;}
    if(!smoke&&!privateWindow)profileRegistry=Profiles::Load(profileRoot);   // a private window never reads the registry
    PM::Model model;if(privateWindow){model.privateWindow=true;model.tabCount=(int)pages.size();}else model=ProfileMenuModel();
    chrome.profileButton.open=true;InvalidateChrome();UpdateWindow(window);
    ChromeTipHide();
    const auto choice=profileMenu.Track(model,ProfileButtonScreen(),keyboard,ReducedMotionPreferred());
    chrome.profileButton.open=false;InvalidateChrome();
    keyboardModality=choice.keyboard||keyboard;
    switch(choice.action){
    case PM::Action::TabList:TabMenu(900);break;
    case PM::Action::Switch:SwitchProfile(choice.profile);break;
    case PM::Action::Create:OpenProfileDialog(choice.keyboard);break;
    case PM::Action::Private:OpenPrivateWindow();break;   // the existing --private launch: no registry, no profile
    case PM::Action::Manage:ShowProfileSettings();break;
    case PM::Action::OpenTab:OpenTabIn(choice.profile,choice.session,choice.tab);break;
    case PM::Action::RowMenu:ProfileRowMenu(choice.profile,choice.at,choice.keyboard);break;
    case PM::Action::NewWindow:OpenProfileInNewWindow(choice.profile);break;   // the row's context action through UI Automation
    default:if(keyboard){keyboardFocus=900;SetFocus(window);InvalidateChrome();}break;
    }
}

// ---- create ----------------------------------------------------------------------------------------
void OpenProfileDialog(bool keyboard){
    if(!profilesEnabled||profileDialog.Open())return;
    if(!smoke)profileRegistry=Profiles::Load(profileRoot);
    if(profileRegistry.readOnly){VaultNotice(TR(profiles_read_only),TR(profiles_read_only_body));return;}
    NativeChrome::ProfileDialog::Spec spec;spec.keyboard=keyboard;spec.reduced=ReducedMotionPreferred();
    Profiles::Profile seed;seed.name=Profiles::UniqueDefaultName(profileRegistry);seed.icon=Profiles::FirstUnusedIcon(profileRegistry);seed.hue=Profiles::FurthestHue(profileRegistry);seed.intensity=.14f;seed.neutral=false;
    spec.seed=seed;{const auto a=NativeChrome::Design::Accent::Get();spec.accent=D2D1::ColorF(a.r,a.g,a.b,1);}
    spec.validateName=[](const std::wstring& n){return Profiles::ValidateName(profileRegistry,n,L"");};
    spec.submit=[](const Profiles::Profile& p,Profiles::NameError& ne)->std::wstring{
        if(smoke)return L"";
        Profiles::Registry reg=Profiles::Load(profileRoot);
        const auto made=Profiles::Create(profileRoot,reg,p,&ne);
        if(made.id.empty())return ne!=Profiles::NameError::None?std::wstring():std::wstring(TR(profile_create_failed));
        profileRegistry=reg;BroadcastProfilesChanged();if(settingsDrawer.Visible())settingsDrawer.Changed();
        if(!Profiles::Launch(ProfileExe(),made.id))Log(L"PROFILES created but its window did not start "+made.id);
        Log(L"PROFILES created "+made.id);return L"";
    };
    profileDialog.Run(std::move(spec));
}

// ---- edit (Réglages › Profils › <profil>) -----------------------------------------------------------
bool SaveProfileEdit(const Profiles::Profile& p,Profiles::NameError* ne){
    if(smoke)return true;
    Profiles::Registry reg=Profiles::Load(profileRoot);
    if(!Profiles::Update(profileRoot,reg,p,ne))return false;
    profileRegistry=reg;if(p.id==profileId)ApplyProfileChrome(true);BroadcastProfilesChanged();return true;
}

// Réglages › Profils: the list (empty in a private window: the row is not shown) and « Gérer… ».
std::vector<NativeChrome::SettingsUI::ProfileItem> SettingsProfiles(){
    std::vector<NativeChrome::SettingsUI::ProfileItem> out;if(!profilesEnabled)return out;
    for(const auto& p:profileRegistry.items){NativeChrome::SettingsUI::ProfileItem i;i.id=p.id;i.name=p.name;i.glyph=ProfileIcons::Fill(p.icon.c_str());i.ink=ProfileInk(p);i.current=p.id==profileId;out.push_back(i);}
    return out;
}
void ShowProfileSettings(){if(!profilesEnabled)return;if(!smoke)profileRegistry=Profiles::Load(profileRoot);settingsDrawer.ShowProfiles();SyncUpdateToastReserve();}
// ---- delete + undo ------------------------------------------------------------------------------------
struct PendingDelete{bool active=false;Profiles::Tombstone tomb;ULONGLONG until=0;int windows=0;};
PendingDelete profileUndo;
ProfileTakeOver::Gate takeover;   // a delete this window accepted to take over (see ReserveTakeOver)
std::wstring pendingDeleteId;ULONGLONG pendingDeleteDeadline=0;bool pendingDeleteKeyboard=false;
std::wstring DeleteErrorText(const std::wstring& key){
    if(key==L"in-use")return TR(profile_delete_in_use);
    if(key==L"open-windows")return TR(profile_delete_open_windows);
    if(key==L"last")return TR(profile_delete_last);
    if(key==L"busy")return TR(profile_delete_busy);
    if(key==L"read-only")return TR(profiles_read_only_body);
    return TR(profile_delete_failed);
}
void FinalizeProfileDelete(){
    if(!profileUndo.active)return;KillTimer(window,ProfileUndoTimerId);
    if(!Profiles::Finalize(profileRoot,profileUndo.tomb))Log(L"PROFILES finalize deferred (purged at next start)");
    profileUndo=PendingDelete{};
}
void ShowDeletedToast();
void UndoProfileDelete(){
    if(!profileUndo.active)return;KillTimer(window,ProfileUndoTimerId);
    Profiles::Registry reg=Profiles::Load(profileRoot);
    auto undo=profileUndo;profileUndo=PendingDelete{};
    if(!Profiles::Undo(profileRoot,reg,undo.tomb)){VaultNotice(TR(profile_undo_failed),TR(profile_undo_failed_body));if(undo.tomb.hold)CloseHandle(undo.tomb.hold);return;}
    profileRegistry=reg;BroadcastProfilesChanged();if(settingsDrawer.Visible())settingsDrawer.Changed();
    NativeChrome::ContextToast::Spec s;s.kind=NativeChrome::ContextToast::Kind::Card;s.profileCard=true;s.automationId=L"ProfileRestoredToast";s.icon=NativeChrome::ContextToast::Icon::Success;s.durationMs=undo.tomb.closedSessions.empty()?5000:0;
    s.message=TRF(profile_restored,undo.tomb.profile.name);
    const int n=(int)undo.tomb.closedSessions.size();
    s.detail=n?TRP(profile_restored_windows,n):std::wstring(TR(profile_restored_data));
    if(n)s.action=TR(profile_reopen_windows);
    const auto id=undo.tomb.profile.id;const auto sessions=undo.tomb.closedSessions;
    contextToast.Show(s,PageScreenRect(),[id,sessions](bool acted){if(acted)Profiles::ReopenSessions(ProfileExe(),id,sessions);});
    Log(L"PROFILES undo "+id);
}
NativeChrome::ContextToast::Spec DeletedToastSpec(){
    NativeChrome::ContextToast::Spec s;s.kind=NativeChrome::ContextToast::Kind::Card;s.profileCard=true;s.inlineAction=true;s.automationId=L"ProfileDeletedToast";s.icon=NativeChrome::ContextToast::Icon::Success;s.durationMs=0;s.closable=false;
    s.message=TRF(profile_deleted,profileUndo.tomb.profile.name);
    const ULONGLONG now=GetTickCount64();const int left=profileUndo.until>now?(int)((profileUndo.until-now+999)/1000):0;
    s.detail=TRF(profile_delete_countdown,std::to_wstring(left));s.action=TR(common_undo);return s;
}
void ShowDeletedToast(){
    contextToast.Show(DeletedToastSpec(),PageScreenRect(),[](bool acted){if(acted)UndoProfileDelete();});
    SetTimer(window,ProfileUndoTimerId,250,nullptr);
}
// The countdown: one update per second; it pauses while the toast has the pointer or the focus.
void ProfileUndoTick(){
    if(!profileUndo.active){KillTimer(window,ProfileUndoTimerId);return;}
    const bool held=contextToast.Window()&&(GetFocus()==contextToast.Window()||[]{POINT p;GetCursorPos(&p);RECT r{};GetWindowRect(contextToast.Window(),&r);return PtInRect(&r,p)!=FALSE;}());
    if(held){profileUndo.until+=250;return;}
    if(GetTickCount64()>=profileUndo.until){contextToast.Hide();FinalizeProfileDelete();return;}
    static int lastShown=-1;const auto spec=DeletedToastSpec();const int left=(int)((profileUndo.until-GetTickCount64()+999)/1000);
    if(left!=lastShown){lastShown=left;if(contextToast.Visible())contextToast.Update(spec);}
}
// The actual delete, once no window of the profile is open (this window is never one of them).
bool RunProfileDelete(const std::wstring& id,std::wstring& error,std::wstring* keyOut=nullptr){
    // One undo at a time, never finalised behind the user's back (re-review R1): refuse while one is pending.
    if(profileUndo.active){error=TR(profile_delete_undo_pending);if(keyOut)*keyOut=L"pending";Log(L"PROFILES delete refused "+id+L": an undo is pending");return false;}
    Profiles::Registry reg=Profiles::Load(profileRoot);Profiles::Tombstone tomb;std::wstring key;
    if(!Profiles::Delete(profileRoot,reg,id,tomb,key)){error=DeleteErrorText(key);if(keyOut)*keyOut=key;Log(L"PROFILES delete refused "+id+L": "+key);return false;}
    profileRegistry=reg;profileUndo.active=true;profileUndo.tomb=tomb;profileUndo.until=GetTickCount64()+Profiles::UndoMilliseconds;profileUndo.windows=(int)tomb.closedSessions.size();
    BroadcastProfilesChanged();if(settingsDrawer.Visible())settingsDrawer.Changed();
    ShowDeletedToast();Log(L"PROFILES deleted "+id+L" (undo 10 s)");return true;
}
// Asks every open window of `id` to close (keeping its session), then waits for them (bounded).
// Private windows mark themselves (Profiles::MarkPrivateWindow) so profile actions can tell them apart.
bool PrivateWindowsOpen(){return Profiles::PrivateWindowCount()>0;}
// Every browser window hears it (a window without a valid sidecar cannot be addressed); each one
// checks its own profile and closes keeping its session (SaveSession + Release keep = true).
void CloseProfileWindows(const std::wstring& id){
    if(!privateWindow&&profilesEnabled&&id!=profileId)LeaveProfileHere(id,false);   // cached here: dropped now
    struct Ctx{std::wstring text;};Ctx ctx{L"close-for-delete "+id};
    EnumWindows([](HWND h,LPARAM l)->BOOL{wchar_t cls[64]={};if(h!=window&&GetClassNameW(h,cls,64)&&!wcscmp(cls,Profiles::WindowClass))SendProfileCommand(h,reinterpret_cast<Ctx*>(l)->text);return TRUE;},(LPARAM)&ctx);
}
// Close-wait loop on a timer: done(true) once the profile has no open window, done(false) at the deadline.
std::function<void(bool)> closeWaitDone;
// Closed windows release their files a moment later (the WebView2 browser processes exit after the
// window): « in-use » / « busy » / « open-windows » are retried until the same bounded deadline.
void DeleteWhenReleased(const std::wstring& id,ULONGLONG deadline,std::function<void(bool,const std::wstring&)> done){
    pendingDeleteId=id;pendingDeleteDeadline=deadline;
    closeWaitDone=[id,deadline,done](bool clear){
        std::wstring error,key;
        if(clear&&RunProfileDelete(id,error,&key)){if(done)done(true,L"");return;}
        if(!clear){key=L"open-windows";error=DeleteErrorText(key);}
        if((key==L"in-use"||key==L"busy"||key==L"open-windows")&&GetTickCount64()<deadline){if(key==L"open-windows")CloseProfileWindows(id);DeleteWhenReleased(id,deadline,done);return;}
        if(done)done(false,error);
    };
    SetTimer(window,ProfileCloseWaitTimerId,250,nullptr);
}
void ProfileCloseWaitTick(){
    if(pendingDeleteId.empty()){KillTimer(window,ProfileCloseWaitTimerId);return;}
    const bool clear=Profiles::OpenWindowCount(profileRoot,pendingDeleteId)==0,late=GetTickCount64()>pendingDeleteDeadline;
    if(!clear&&!late)return;
    KillTimer(window,ProfileCloseWaitTimerId);pendingDeleteId.clear();auto done=std::move(closeWaitDone);closeWaitDone=nullptr;if(done)done(clear);
}
// Paper P04 (current edits): the dedicated delete dialog (profile-delete-dialog.h). « Annuler » has the
// focus; « Supprimer le profil » turns it busy (« Suppression… », controls at 32 %, Échap inert) while
// the windows close and the folder is set aside; a failure keeps the dialog with the error in line and
// « Réessayer ». Modal: Run() returns once the dialog is gone.
NativeChrome::ProfileDeleteDialog::Dialog profileDeleteDialog;
void AskDeleteProfile(const std::wstring& id,bool keyboard){
    Log(L"PROFILES delete asked "+id);
    if(!profilesEnabled||smoke||profileDeleteDialog.Open())return;
    profileRegistry=Profiles::Load(profileRoot);const Profiles::Profile* p=profileRegistry.Find(id);if(!p)return;
    if(profileRegistry.items.size()<2){VaultNotice(TR(profile_minimum_required),TR(profile_minimum_required_body));return;}
    if(profileRegistry.readOnly){VaultNotice(TR(profiles_read_only),DeleteErrorText(L"read-only"));return;}
    // One undo at a time, never finalised behind the user's back: a second delete waits (review L4).
    // The pending notice is never replaced: it takes the focus (its countdown pauses) so « Annuler » is right there.
    if(profileUndo.active){Log(L"PROFILES delete refused while an undo is pending");if(contextToast.Visible())contextToast.Focus();return;}
    if(takeover.Busy()){Log(L"PROFILES delete refused while a take-over is in progress");VaultNotice(TR(profile_delete_in_progress),TR(profile_delete_in_progress_body));return;}
    // Private windows share the default profile's engine folder: « Personnel » waits until they are closed (review M2).
    if(id==Profiles::DefaultId&&PrivateWindowsOpen()){VaultNotice(TR(profile_close_private_first),TRF(profile_close_private_first_body,p->name));return;}
    const Profiles::Profile target=*p;
    NativeChrome::ProfileDeleteDialog::Spec spec;
    // Its windows close for the delete, cancelling any download still running in them (this window's or
    // another process's): a conditional consequence, stated whenever the profile has windows open.
    {const int open=std::max(Profiles::OpenWindowCount(profileRoot,target.id),target.id==profileId?1:0);
     if(open>1)spec.note=TR(profile_delete_downloads_many);
     else if(open==1)spec.note=target.id==profileId?TR(profile_delete_downloads_this):TR(profile_delete_downloads_its);}
    spec.name=target.name;spec.glyph=ProfileIcons::Fill(target.icon.c_str());spec.ink=ProfileInk(target);spec.keyboard=keyboard;spec.reduced=ReducedMotionPreferred();
    bool closeSelf=false;
    spec.confirm=[target,&closeSelf]{
        if(target.id==profileId&&LeaveProfileHere(target.id,true)){
            // Own profile (same-window model): this window switched to another profile and dropped the deleted one's
            // cache; it deletes locally and owns the undo. Its other windows leave it too.
            CloseProfileWindows(target.id);
            DeleteWhenReleased(target.id,GetTickCount64()+8000,[](bool ok,const std::wstring& error){if(ok)profileDeleteDialog.Finish();else profileDeleteDialog.Fail(error);});
            return;
        }
        if(target.id==profileId){
            // Fallback (this window could not switch): a window of another profile takes the delete over.
            std::wstring host;for(const auto& q:profileRegistry.items)if(q.id!=target.id){if(!Profiles::LiveWindows(profileRoot,q.id).empty()){host=q.id;break;}if(host.empty())host=q.id;}
            if(host.empty()){profileDeleteDialog.Fail(DeleteErrorText(L"last"));return;}
            // Every live window of another profile is asked in turn; one with its own undo pending refuses
            // (re-review R1). Nothing of this profile closes unless a host accepted (or was launched).
            bool sent=false,busy=false,anyWindow=false;
            for(const auto& q:profileRegistry.items){if(q.id==target.id||sent)continue;
                for(const auto& w:Profiles::LiveWindows(profileRoot,q.id)){if(!w.hwnd)continue;anyWindow=true;
                    DWORD pid=0;GetWindowThreadProcessId(w.hwnd,&pid);AllowSetForegroundWindow(pid);
                    const auto answer=SendProfileCommand(w.hwnd,L"delete-profile "+target.id);
                    if(answer==1){sent=true;host=q.id;break;}if(answer==2)busy=true;}}
            if(!sent&&!anyWindow)sent=Profiles::Launch(ProfileExe(),host,L"--delete-profile "+target.id);
            if(!sent){profileDeleteDialog.Fail(busy?TR(profile_delete_undo_pending_elsewhere):TR(profile_delete_no_takeover));return;}
            Log(L"PROFILES own delete handed over to "+host);closeSelf=true;profileDeleteDialog.Finish();return;
        }
        CloseProfileWindows(target.id);
        DeleteWhenReleased(target.id,GetTickCount64()+8000,[](bool ok,const std::wstring& error){if(ok)profileDeleteDialog.Finish();else profileDeleteDialog.Fail(error);});
    };
    profileDeleteDialog.Run(std::move(spec));
    if(closeSelf){CloseProfileWindows(target.id);closeWithoutPrompt=true;PostMessageW(window,WM_CLOSE,0,0);}
}
// Hand-over receiver: a window of another profile deletes `id` once its windows are gone.
// A take-over accepted by this window is RESERVED synchronously, in the WM_COPYDATA answer itself (or at
// start for --delete-profile): until it ends (deleted, failed, timed out), this window refuses any other
// delete — local or taken over — so the requester, which closes on « accepted », never closes for nothing.
bool ReserveTakeOver(const std::wstring& id){return takeover.Reserve(id,profileUndo.active,privateWindow||!profilesEnabled||!Profiles::ValidId(id)||id==profileId);}
void TakeOverProfileDelete(const std::wstring& id){
    if(!takeover.Owns(id)){Log(L"PROFILES take-over ignored "+id+L": not reserved");return;}
    if(profileUndo.active){takeover.Clear();Log(L"PROFILES take-over refused "+id+L": an undo is pending here");VaultNotice(TR(profile_not_deleted),TR(profile_delete_undo_pending_here));return;}   // cannot happen while reserved
    // Bring this window forward: the undo notice lives here (review M1).
    if(IsIconic(window))ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);
    CloseProfileWindows(id);   // whatever is still open of that profile closes, keeping its sessions (review H1)
    DeleteWhenReleased(id,GetTickCount64()+10000,[](bool ok,const std::wstring& error){takeover.Clear();if(!ok)VaultNotice(TR(profile_not_deleted),error);});
}

// ---- messages -----------------------------------------------------------------------------------------
int ProfileCopyDataMessage(LPARAM l){
    const auto* cd=reinterpret_cast<const COPYDATASTRUCT*>(l);if(!cd||cd->dwData!=ProfileCopyData||!cd->lpData||cd->cbData<sizeof(wchar_t))return 0;
    std::wstring text(static_cast<const wchar_t*>(cd->lpData),cd->cbData/sizeof(wchar_t));while(!text.empty()&&!text.back())text.pop_back();
    // « open <link or path> »: another launch (a link clicked elsewhere, « Ouvrir avec ») hands it to this window.
    if(text.rfind(L"open " /* i18n-exempt: window protocol */,0)==0){if(privateWindow||text.size()<6)return 0;const std::wstring target=text.substr(5);Defer([target]{OpenHandedTarget(target);});return 1;}
    if(text.rfind(L"tab " /* i18n-exempt: window protocol */,0)==0){const int n=_wtoi(text.c_str()+4);Defer([n]{if(n>=0&&n<(int)pages.size())Select(n);if(IsIconic(window))ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);});return 1;}
    // A profile being deleted leaves this window: switched away if on screen, its cache dropped (session kept
    // for « Rouvrir »); the window itself never closes for it. Private windows belong to no profile (review M2).
    if(text.rfind(L"close-for-delete ",0)==0){if(!privateWindow&&profilesEnabled){const std::wstring id=text.substr(17);Defer([id]{LeaveProfileHere(id,false);});}return 1;}
    if(text.rfind(L"qa " /* i18n-exempt: window protocol */,0)==0)return WorkspaceQaCommand(text.substr(3));   // --workspace-test only (else 0)
    if(text.rfind(Profiles::EvictCommand,0)==0){if(privateWindow||!profilesEnabled)return Profiles::EvictNotApplicable;return EvictProfileHere(text.substr(wcslen(Profiles::EvictCommand)));}
    if(text.rfind(L"delete-profile ",0)==0){const std::wstring id=text.substr(15);if(!ReserveTakeOver(id))return 2;Defer([id]{TakeOverProfileDelete(id);});return 1;}   // reserved before answering
    return 0;
}
// Cold start: the other profiles' windows left open last time come back (each in its own process),
// unless that profile does not restore its tabs.
void RestoreOtherProfiles(){
    if(!profilesEnabled||smoke||privateWindow||windowId.empty())return;
    for(const auto& id:Profiles::ProfilesWithOrphans(profileRoot,profileRegistry,ThisWindowSlots())){
        std::ifstream in(std::filesystem::path(Profiles::ResolvePaths(profileRoot,id).settingsFile),std::ios::binary);
        if(in){const auto st=BrowserSettings::Read(in);if(!st.restoreTabs)continue;}
        if(Profiles::Launch(ProfileExe(),id))Log(L"PROFILES restored windows of "+id);
    }
}
void MarkPrivateWindow(){if(privateWindow&&window)Profiles::MarkPrivateWindow(window);}
// F6 from the chrome reaches the profile notice (« Annuler » / « Rouvrir ses fenêtres ») while one is up;
// its countdown pauses while it has the focus; Échap or F6 there gives the focus back (review H2).
bool FocusContextToast(){return contextToast.Visible()&&contextToast.Focus();}
void ProfilesStarted(const wchar_t* args){
    MarkPrivateWindow();if(privateWindow){ApplyProfileChrome();return;}
    // A window started to take over a delete (--delete-profile) never restores orphans: the profile being
    // deleted would be reopened (review H1).
    const bool takingOver=args&&wcsstr(args,L"--delete-profile ")!=nullptr;
    StartWorkspaces();ApplyProfileChrome();PublishProfileWindow();RestoreWindowSet();if(!takingOver)RestoreOtherProfiles();
    if(const wchar_t* p=args?wcsstr(args,L"--delete-profile "):nullptr){p+=17;while(*p==L' ')++p;const wchar_t* e=p;while(*e&&*e!=L' ')++e;const std::wstring id(p,e);if(ReserveTakeOver(id))Defer([id]{TakeOverProfileDelete(id);});}
}
// Close: the set keeps every profile of this window for the next start; each workspace saves, releases its
// engine lease, unpublishes and keeps its session (WM_CLOSE releases the active one's claim).
void ProfilesClosing(){if(profileUndo.active)FinalizeProfileDelete();WriteThisWindowSet();DisposeParkedWorkspaces();UnpublishProfileWindow();}   /* the active engine lease: its holder (this process) is pruned once it has exited */
