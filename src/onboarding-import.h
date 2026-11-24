#pragma once
#include "i18n.h"
// Onboarding — the import behind boards 3 to 5.1, without any window or global (tested on synthetic sources in
// tests/onboarding/onboarding_import_test.cpp). It adds nothing to the import itself: the catalog, the readers
// and the profile-store transactions are those of Réglages › « Importer depuis un navigateur… »
// (src/browser-catalog.h, src/browser-import.h). What is here is the onboarding's view of them:
//  * List(): the catalog's rows grouped the way board 3 shows them — one entry per browser under the name people
//    know (« Google Chrome »), its profiles under it (« Profil principal », « Profil 1 »), the browsers that
//    cannot be imported greyed with the reason, the HTML file last.
//  * Commit(): what was read becomes the profile's bookmarks (with their sites' icons from the source, as Réglages'
//    import) and history, one transaction per category, and an outcome the window can show (what was saved, and the
//    sentence of board 5.1 for the first thing that failed).
// The read itself (BrowserImport::Read) runs on the host's worker thread, between the two.
#include <cwctype>
#include <functional>
#include <sstream>
#include <string>
#include <vector>
#include "browser-import.h"
#include "browser-catalog.h"
#include "import-icons.h"
#include "onboarding-model.h"

namespace OnboardingImport {

namespace BI = BrowserImport;
namespace UI = NativeChrome::OnboardingUI;

// The names people know the browsers by (the boards write « Google Chrome »).
inline std::wstring BrowserName(BI::Browser b) {
    switch (b) {
    case BI::Browser::Chrome: return L"Google Chrome";     // i18n-exempt: product name
    case BI::Browser::Edge: return L"Microsoft Edge";      // i18n-exempt: product name
    default: return BI::BrandName(b);
    }
}
inline std::wstring Sentence(std::wstring s) { if (!s.empty()) s[0] = static_cast<wchar_t>(std::towupper(s[0])); return s; }
// « Profil principal » for Chromium's « Default », « Profil 1 » for its « Profile 1 » folder, else the profile's own name.
inline std::wstring ProfileLabel(const BI::Source& s) {
    if (BI::Chromium(s.browser) && s.isDefault) return Sentence(TR(import_profile_default));
    static const std::wstring prefix = L"Profile ";   // i18n-exempt: Chromium's folder name
    if (BI::Chromium(s.browser) && s.profile.rfind(prefix, 0) == 0 && s.profile.size() > prefix.size() && s.profile.find_first_not_of(L"0123456789", prefix.size()) == std::wstring::npos) return TRF(onboarding_import_profile_n, s.profile.substr(prefix.size()));
    return s.profile;
}

struct Listing {
    std::vector<UI::BrowserSource> shown;               // board 3's « Navigateur » list
    std::vector<std::vector<BI::Source>> sources;       // [browser][profile]: what each choice imports
    std::vector<std::wstring> names;                    // the name used in sentences (« Google Chrome est ouvert… »)
    std::size_t profiles = 0, unavailable = 0;          // for the log (counts only)
};
using LogoLoader = std::function<std::shared_ptr<const NativeChrome::FaviconImage>(const BI::Row&)>;
inline Listing List(const std::vector<BI::Row>& rows, const LogoLoader& logo) {
    Listing l;
    auto add = [&](UI::BrowserSource s, const std::wstring& name) { l.shown.push_back(std::move(s)); l.sources.emplace_back(); l.names.push_back(name); return l.shown.size() - 1; };
    for (const auto& r : rows) {
        if (r.state == BI::RowState::Html) { UI::BrowserSource f; f.name = TR(import_source_html); f.file = true; l.sources[add(f, TR(import_source_file_short))].push_back(r.source); continue; }
        if (!r.Available()) {
            UI::BrowserSource u; u.name = r.state == BI::RowState::NoData ? BrowserName(r.source.browser) : r.name; u.detail = BI::RowDetail(r); u.available = false;
            if (logo && !r.icon.empty()) u.logo = logo(r);
            add(u, u.name); ++l.unavailable; continue;
        }
        const std::wstring name = BrowserName(r.source.browser);
        std::size_t at = 0; for (; at < l.shown.size(); ++at) if (l.shown[at].available && !l.shown[at].file && l.shown[at].name == name) break;
        if (at == l.shown.size()) { UI::BrowserSource b; b.name = name; b.detail = BI::RowDetail(r); if (logo && !r.icon.empty()) b.logo = logo(r); at = add(b, name); }
        l.shown[at].profiles.push_back({ProfileLabel(r.source), r.source.bookmarks, r.source.history}); l.sources[at].push_back(r.source); ++l.profiles;
    }
    return l;
}
// The source a pick of the window stands for (null when it is not in the lists any more).
inline const BI::Source* Pick(const Listing& l, int browser, int profile) {
    if (browser < 0 || browser >= static_cast<int>(l.sources.size())) return nullptr;
    const auto& p = l.sources[static_cast<std::size_t>(browser)];
    return profile >= 0 && profile < static_cast<int>(p.size()) ? &p[static_cast<std::size_t>(profile)] : nullptr;
}

// The source's bookmarks bar as it was read: the bar root's own children, in order, the ones the import keeps
// (folders, and links the browser can open). Boards 4 – 5.1 show them in the browser mock's bar.
inline std::vector<UI::BarItem> BarItems(const std::vector<BI::Item>& roots, std::size_t limit = 48) {
    std::vector<UI::BarItem> out;
    for (const auto& root : roots) {
        if (BI::RoleOf(root.key) != BI::RootRole::Bar) continue;
        for (const auto& it : root.children) {
            if (out.size() >= limit) break;
            if (!it.folder && !Bookmarks::Admissible(it.url)) continue;
            out.push_back({it.title.empty() ? it.url : it.title, it.folder});
        }
    }
    return out;
}

struct Files { std::wstring dataDir, bookmarks, history, map; };   // the profile's folder and its three files
inline std::wstring CountsLog(const BI::Counts& c) {
    return L"imported=" + std::to_wstring(c.imported) + L" present=" + std::to_wstring(c.present) + L" unsupported=" + std::to_wstring(c.unsupported) + L" full=" + std::to_wstring(c.full) + L" deep=" + std::to_wstring(c.deep) + L" folders=" + std::to_wstring(c.folders);
}
// Why a category could not be read, as a sentence for board 5.1.
inline std::wstring Why(BI::Failure f, const BI::Source& s, const std::wstring& name) {
    return f == BI::Failure::Busy && s.browser != BI::Browser::HtmlFile ? TRF(onboarding_import_open, name) : Sentence(BI::FailureText(f, s));
}
// UI thread. `excluded`: addresses the history never keeps (search result pages). `log`: counts only.
inline UI::ImportOutcome Commit(const Files& files, const BI::Source& s, const std::wstring& name, const BI::Options& options, const BI::ReadResult& read, std::int64_t now,
                                const std::function<bool(const std::wstring&)>& excluded, const std::function<void(const std::wstring&)>& log) {
    UI::ImportOutcome outcome;
    auto failed = [&](const std::wstring& reason) { if (outcome.detail.empty()) outcome.detail = reason; };
    auto say = [&](const std::wstring& line) { if (log) log(line); };
    if (options.bookmarks) {
        const auto& r = read.bookmarks;
        if (r.failure != BI::Failure::None) { failed(Why(r.failure, s, name)); say(L"ONBOARDING bookmarks read failed " + std::to_wstring(static_cast<int>(r.failure))); }   // i18n-exempt: log line (counts only)
        else {
            // The profile's file as it is now: the transaction merges with it again under the profile lock.
            Bookmarks::Store mine; { ProfileStore::Lock lock(files.dataDir); std::string bytes; ProfileStore::ReadAll(files.bookmarks, bytes); std::istringstream in(bytes); bool damaged = false; mine = Bookmarks::Store::Read(in, &damaged); }
            Bookmarks::Store was = mine;
            const auto c = BI::CommitBookmarks(files.dataDir, files.bookmarks, files.map, was, mine, s, r.roots, now, nullptr, nullptr);
            if (c.Saved()) {
                outcome.bookmarksSaved = true; outcome.bar = BarItems(r.roots);
                // The source's own icons of these sites, so the bar shows them from the first start (src/import-icons.h);
                // the host fetches the missing ones afterwards.
                const std::size_t icons = ImportIcons::Save(files.dataDir, r.icons).size();
                say(L"ONBOARDING bookmarks saved " + CountsLog(c.counts) + L" ignored=" + std::to_wstring(r.ignored) + L" icons=" + std::to_wstring(icons));   // i18n-exempt: log line (counts only)
            }
            else { failed(Sentence(c.identity == BI::Identity::Newer ? TR(import_map_newer) : c.identity == BI::Identity::Damaged ? TR(import_map_damaged) : c.identity == BI::Identity::Full ? TR(import_map_full) : TR(import_save_failed))); say(L"ONBOARDING bookmarks save failed result=" + std::to_wstring(static_cast<int>(c.result))); }   // i18n-exempt: log line (counts only)
        }
    }
    if (options.history) {
        const auto& r = read.history;
        if (r.failure != BI::Failure::None) { failed(Why(r.failure, s, name)); say(L"ONBOARDING history read failed " + std::to_wstring(static_cast<int>(r.failure))); }   // i18n-exempt: log line (counts only)
        else {
            AddressBar::History mine; { ProfileStore::Lock lock(files.dataDir); std::string bytes; ProfileStore::ReadAll(files.history, bytes); std::istringstream in(bytes); mine = AddressBar::History::Read(in); }
            AddressBar::History was = mine;
            const std::function<bool(const std::wstring&)> keepAll = [](const std::wstring&) { return false; };
            const auto c = BI::CommitHistory(files.dataDir, files.history, was, mine, r.visits, r.beyond, now, excluded ? excluded : keepAll);
            if (c.Saved()) { outcome.historySaved = true; say(L"ONBOARDING history saved " + CountsLog(c.counts) + L" ignored=" + std::to_wstring(r.ignored)); }   // i18n-exempt: log line (counts only)
            else { failed(Sentence(TR(import_save_failed))); say(L"ONBOARDING history save failed result=" + std::to_wstring(static_cast<int>(c.result))); }   // i18n-exempt: log line (counts only)
        }
    }
    return outcome;
}

} // namespace OnboardingImport
