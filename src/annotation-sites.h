#pragma once
// Annotation mode — where « Annoter » is on (#125, Val's decision of 2026-11-24). Pure C++17, no Win32: unit-tested in
// tests/annotations/annotation_sites_test.cpp. The host (src/annotation-mode.h) asks it before the mode starts, when the
// mode's page is re-armed after a navigation, and for every message of the page script.
//
//   on   local projects: http(s) on localhost, *.localhost, 127.0.0.0/8 (strict dotted quad) and [::1]; local files
//        (file:///C:/…, file://localhost/…). Exactly the classification of the chat's « sites locaux seulement »
//        (src/chat-local.h, ChatLocal::Classify): look-alikes (localhost.example.com, 127.0.0.1.example.com, 0x7f.1,
//        2130706433), credentials in the URL and network shares (file://server/share) are not local.
//   off  everything else: live sites, and any page that is neither http(s) nor file (about:, data:, …). The toolbar
//        button is shown in its unavailable state (« Annoter — marche sur vos projets locaux »), the page menu has no
//        « Annoter l'élément », Ctrl+Maj+A and the chat's shortcut do not start the mode.
//   A private window follows the same rule unless OnlineInPrivateWindow is set.
#include <string_view>
#include "chat-local.h"

namespace AnnotationSites {

// Val, 2026-11-24 (answer « A »): a private window changes nothing — local addresses and files on, live sites off.
// true = a private window may annotate live sites too.
inline constexpr bool OnlineInPrivateWindow = false;

enum class Place { Local, LocalFile, Online, Other };   // Other: not a web page the mode knows (about:, data:, javascript:…)
// `url`: the page's address as the engine reports it (UTF-8).
inline Place Of(std::string_view url) {
    const auto site = ChatLocal::Classify(url);
    if (site.local) return site.file ? Place::LocalFile : Place::Local;
    if (site.scheme == "http" || site.scheme == "https") return Place::Online;
    return Place::Other;
}
// The rule with its private-window choice as a parameter (the tests try both answers).
inline bool AllowedWith(std::string_view url, bool privateWindow, bool onlineInPrivate) {
    const Place p = Of(url);
    return p == Place::Local || p == Place::LocalFile || (p == Place::Online && privateWindow && onlineInPrivate);
}
inline bool Allowed(std::string_view url, bool privateWindow) { return AllowedWith(url, privateWindow, OnlineInPrivateWindow); }

} // namespace AnnotationSites
