#pragma once
// Sandbox des composants — internal French page served by main at
// https://browser-debug.invalid/sandbox (WebResourceRequested, never the network) to exercise
// every component of Paper page p-D-0 (C0 index, A1–A9, B01–B09, C1–C9). It never draws an
// HTML copy of a native component: buttons either take the real web path (alert/confirm/prompt,
// beforeunload, getUserMedia, geolocation, notifications, clipboard, requestFullscreen + keyboard
// lock, mailto:, *.invalid hosts, Ctrl+F content) or send an allow-listed command through
// chrome.webview.postMessage that main dispatches to the SAME production native component
// (in « Démonstration » mode: fictitious origins/accounts, confirmations only logged).
// Results come back through ResultScript() into a visible log (aria-live) with a reset button.
//
// Bridge security (with main): page only in the tab main opened (browser://debug, menu,
// --debug-sandbox), top-level source == Url exactly, random per-launch token, command and
// variant in the allowlist, bounded length; nothing destroys data or closes anything.
#include <windows.h>
#include <optional>
#include <string>
#include <vector>
#include "error-pages.h"

namespace NativeChrome {
namespace DebugSandbox {
inline constexpr wchar_t Host[] = L"browser-debug.invalid";
inline constexpr wchar_t Url[] = L"https://browser-debug.invalid/sandbox";
inline constexpr size_t MaxMessage = 512;

// `keyboard`: the page button was activated from the keyboard (Entrée / Espace), so the native
// component opens with a visible focus ring; a pointer click opens it with the ring hidden.
struct Command { std::wstring name; std::wstring variant; bool keyboard = false; };
// Allow-listed commands and their variants ("" = the command takes no variant).
struct CommandSpec { const wchar_t* name; std::vector<const wchar_t*> variants; const wchar_t* meaning; };
inline const std::vector<CommandSpec>& Commands() {
    static const std::vector<CommandSpec> list = {
        {L"modal.generic", {L"default", L"danger", L"disabled", L"error", L"busy"}, L"A1 : vraie modale, contenu de démonstration ; variante = état du bouton principal / champ"},
        {L"modal.alert", {L"default", L"long"}, L"A2 : modale alert native, origine démo « exemple.test indique »"},
        {L"modal.confirm", {L"default"}, L"A2 : modale confirm native, origine démo"},
        {L"modal.prompt", {L"default", L"error"}, L"A3 : modale prompt native ; error = validation en échec"},
        {L"modal.beforeUnload", {L"leave", L"reload"}, L"A4 : modale « quitter / recharger ce site »"},
        {L"modal.clearData", {L"default", L"busy"}, L"A5 : effacer les données en mode Démonstration (rien n'est effacé) ; busy = « Effacement… »"},
        {L"modal.closeWindow", {L"default"}, L"A6 : fermer la fenêtre (Démonstration : rien n'est fermé)"},
        {L"modal.openManyTabs", {L"default"}, L"A6 : ouvrir beaucoup d'onglets (Démonstration)"},
        {L"modal.deleteBookmarks", {L"one", L"many"}, L"A6 : supprimer des favoris (Démonstration : rien n'est supprimé)"},
        {L"modal.quit", {L"default"}, L"A6 : quitter Browser (Démonstration : rien n'est fermé)"},
        {L"toast.tabLimit", {L"blocked", L"partial"}, L"A7 : toast natif limite de 128 onglets ; partial = ouverture partielle (restauration)"},
        {L"modal.about", {L"upToDate", L"checking", L"available", L"error"}, L"A8 : À propos de Browser, état de version"},
        {L"modal.private", {L"current", L"concept"}, L"A9 : navigation privée, actuel ou concept"},
        {L"prompt.permission", {L"camera", L"microphone", L"cameraMicrophone", L"geolocation", L"notifications", L"clipboard", L"blocked"}, L"B01/B02 : vrai popover d'autorisation, origine démo ; blocked = état bloqué"},
        {L"permission.demo", {L"active", L"stopped", L"status", L"reset"}, L"B02 : état démo après décision (actif simulé, arrêté, état du site, réinitialiser) ; jamais un vrai site"},
        {L"prompt.passwordFill", {L"one", L"two"}, L"B03 : remplir un mot de passe, comptes fictifs"},
        {L"prompt.passwordSave", {L"save", L"update"}, L"B04 : enregistrer / mettre à jour, compte fictif"},
        {L"password.manager", {L"list", L"detail", L"delete"}, L"B05 : réglages mots de passe, données fictives en mémoire"},
        {L"prompt.basicAuth", {L"default", L"error"}, L"B06 : authentification HTTP (démo)"},
        {L"prompt.externalApp", {L"default"}, L"B06 : ouvrir une application externe (démo)"},
        {L"prompt.dangerousDownload", {L"default"}, L"B07 : téléchargement dangereux bloqué (démo)"},
        {L"prompt.clientCertificate", {L"default"}, L"B08 : certificat client (concept)"},
        {L"prompt.screenShare", {L"default", L"empty"}, L"B08 : partage d'écran avec aperçus fictifs (démo)"},
        {L"prompt.keepFile", {L"default"}, L"B08 : conserver le fichier (concept)"},
        {L"find.open", {L"default", L"seed"}, L"C1/C2 : ouvre la vraie barre Ctrl+F ; seed = pré-remplie « oxyde »"},
        {L"error.page", {L"nameNotResolved", L"refused", L"timedOut", L"reset", L"unreachable", L"generic"}, L"C4 : page d'erreur native dans un nouvel onglet (adresse *.invalid)"},
        {L"error.offline", {L"offline", L"retrying", L"restored"}, L"C3 : simule hors ligne (nouvel onglet), reprise en cours, connexion rétablie"},
        {L"tooltip.show", {L"label", L"shortcut", L"detail", L"tab", L"disabled"}, L"C5 : vraie info-bulle native sous le bouton de la sandbox"},
        {L"fullscreen.hint", {L"page", L"browser", L"locked"}, L"C6/C7 : indication plein écran native (sans changer de mode)"},
        {L"toast.status", {L"success", L"failure", L"progress", L"brief"}, L"C8 : toast d'état natif"},
        {L"toast.pageSaved", {L"default"}, L"C9 : toast « Page enregistrée » (fichier fictif)"},
        {L"shortcuts.open", {L"default"}, L"C5 : panneau natif « Raccourcis clavier » (Ctrl+/)"},
        {L"sandbox.ping", {L""}, L"Vérifie le pont (réponse « pont actif » dans le journal)"},
    };
    return list;
}
inline const std::vector<std::wstring>& AllowedCommands() {
    static const std::vector<std::wstring> names = [] { std::vector<std::wstring> n; for (auto& c : Commands()) n.push_back(c.name); return n; }();
    return names;
}
inline const CommandSpec* Find(const std::wstring& name) { for (auto& c : Commands()) if (name == c.name) return &c; return nullptr; }
inline bool Allowed(const std::wstring& name, const std::wstring& variant) {
    const auto* c = Find(name); if (!c) return false;
    for (auto* v : c->variants) if (variant == v) return true;
    return false;
}
// {"token":"…","command":"…","variant":"…","via":"keyboard|pointer"} (variant, via optional), as a string or its JSON form.
inline std::optional<Command> ParseCommand(const std::wstring& message, const std::wstring& token) {
    if (message.size() > MaxMessage) return std::nullopt;
    auto m = FlatJson::Parse(message, MaxMessage, 4);
    if (!m || !FlatJson::OnlyKeys(*m, {L"token", L"command", L"variant", L"via"}, {L"token", L"command"})) return std::nullopt;
    if (!FlatJson::SameToken((*m)[L"token"], token)) return std::nullopt;
    Command c{(*m)[L"command"], m->count(L"variant") ? (*m)[L"variant"] : L""};
    if (m->count(L"via")) { const auto& via = (*m)[L"via"]; if (via != L"keyboard" && via != L"pointer") return std::nullopt; c.keyboard = via == L"keyboard"; }
    if (!Allowed(c.name, c.variant)) return std::nullopt;
    return c;
}
// JS that main runs (ExecuteScript) to append `line` to the visible log. Safe for any text.
inline std::wstring ResultScript(const std::wstring& line) {
    std::wstring text = line.size() > 2000 ? line.substr(0, 2000) + L"…" : line;
    return L"window.__sandboxLog&&window.__sandboxLog(\"" + WebText::Js(text) + L"\",\"natif\");";
}

// ---- Catalogue ----------------------------------------------------------------------
enum class Status { Integrated, Demo, NativeKept, Note, Index };
inline const wchar_t* StatusLabel(Status s) {
    switch (s) { case Status::Integrated: return L"Intégré"; case Status::Demo: return L"Démonstration"; case Status::NativeKept: return L"Natif conservé"; case Status::Note: return L"Note"; case Status::Index: return L"Sommaire"; }
    return L"";
}
// A trigger: an allow-listed command (`command`, `variant`) or a real web test (`js`, an id of
// the page script's table below).
// `tag`: status of this variant's production path when it differs from the entry's (mixed
// entries show both badges and mark each trigger).
enum class Tag { Inherit, Integrated, Demo };
struct Trigger { const wchar_t* label; const wchar_t* command; const wchar_t* variant; const wchar_t* js; Tag tag = Tag::Inherit; };
struct Entry { const wchar_t* id; const wchar_t* title; Status status; const wchar_t* note; std::vector<Trigger> triggers; };
inline const std::vector<Entry>& Catalogue() {
    static const std::vector<Entry> list = {
        {L"C0", L"Sommaire et handoff — familles A / B / C", Status::Index, L"Cette page : chaque artboard de la page Paper « Éléments & popups » a sa fiche ci-dessous.", {}},
        {L"A1", L"Modale : composant, boutons et états", Status::Demo, L"Le composant natif de production, contenu fictif.", {
            {L"Normal", L"modal.generic", L"default", nullptr}, {L"Action dangereuse", L"modal.generic", L"danger", nullptr}, {L"Bouton désactivé", L"modal.generic", L"disabled", nullptr},
            {L"Erreur de champ", L"modal.generic", L"error", nullptr}, {L"Chargement", L"modal.generic", L"busy", nullptr}}},
        {L"A2", L"Dialogues JS — alert et confirm", Status::Integrated, L"window.alert / window.confirm réels de cette page (ScriptDialogOpening).", {
            {L"window.alert()", nullptr, nullptr, L"alert"}, {L"window.confirm()", nullptr, nullptr, L"confirm"},
            {L"alert démo (origine fictive)", L"modal.alert", L"default", nullptr}, {L"alert, texte long", L"modal.alert", L"long", nullptr}, {L"confirm démo", L"modal.confirm", L"default", nullptr}}},
        {L"A3", L"Dialogue de saisie — prompt et validation", Status::Integrated, L"window.prompt réel ; la variante d'erreur passe par la commande.", {
            {L"window.prompt()", nullptr, nullptr, L"prompt"}, {L"prompt démo", L"modal.prompt", L"default", nullptr}, {L"Validation en échec", L"modal.prompt", L"error", nullptr}}},
        {L"A4", L"Quitter un site (beforeunload)", Status::Integrated, L"Activez l'interrupteur, puis rechargez ou quittez la page : la vraie modale s'ouvre.", {
            {L"Installer / retirer beforeunload", nullptr, nullptr, L"beforeunload"}, {L"Recharger la page", nullptr, nullptr, L"reload"}, {L"Quitter la page", nullptr, nullptr, L"leave"},
            {L"Démo « quitter »", L"modal.beforeUnload", L"leave", nullptr}, {L"Démo « recharger »", L"modal.beforeUnload", L"reload", nullptr}}},
        {L"A5", L"Confirmation — effacer les données", Status::Integrated, L"Vraie modale des Réglages ; depuis la sandbox elle s'ouvre en mode Démonstration : confirmer ne fait que journaliser, rien n'est effacé.", {
            {L"Ouvrir", L"modal.clearData", L"default", nullptr}, {L"État « Effacement… »", L"modal.clearData", L"busy", nullptr}}},
        {L"A6", L"Confirmations — fenêtre, favoris, quitter", Status::Integrated, L"Fermer la fenêtre (WM_CLOSE) et « Ouvrir tout » (plus de 20 favoris) sont branchés ; depuis la sandbox, tout s'ouvre en mode Démonstration : rien n'est fermé ni supprimé.", {
            {L"Fermer la fenêtre", L"modal.closeWindow", L"default", nullptr}, {L"Ouvrir beaucoup d'onglets", L"modal.openManyTabs", L"default", nullptr},
            {L"Supprimer un favori", L"modal.deleteBookmarks", L"one", nullptr, Tag::Demo}, {L"Supprimer des favoris", L"modal.deleteBookmarks", L"many", nullptr, Tag::Demo}, {L"Quitter Browser", L"modal.quit", L"default", nullptr, Tag::Demo}}},
        {L"A7", L"Toast — limite de 128 onglets", Status::Integrated, L"Branché sur le refus du 129e onglet ; la sandbox l'affiche sans ouvrir 128 onglets.", {
            {L"Bloqué", L"toast.tabLimit", L"blocked", nullptr}, {L"Ouverture partielle", L"toast.tabLimit", L"partial", nullptr, Tag::Demo}}},
        {L"A8", L"À propos de Browser — états de version", Status::Integrated, L"Menu « À propos » ; les autres états de version sont fictifs.", {
            {L"À jour", L"modal.about", L"upToDate", nullptr}, {L"Vérification", L"modal.about", L"checking", nullptr, Tag::Demo}, {L"Mise à jour disponible", L"modal.about", L"available", nullptr, Tag::Demo}, {L"Erreur", L"modal.about", L"error", nullptr, Tag::Demo}}},
        {L"A9", L"Navigation privée — actuel + concept futur", Status::Integrated, L"Menu « Navigation privée » ; le concept futur est une démonstration.", {
            {L"Actuel", L"modal.private", L"current", nullptr}, {L"Concept", L"modal.private", L"concept", nullptr, Tag::Demo}}},
        {L"B01", L"Contexte — Permission caméra + micro", Status::Integrated, L"Demandes réelles de cette origine https (PermissionRequested).", {
            {L"getUserMedia caméra + micro", nullptr, nullptr, L"cameraMicrophone"}, {L"Démo caméra + micro", L"prompt.permission", L"cameraMicrophone", nullptr}}},
        {L"B02", L"Permissions — variantes par type et états", Status::Integrated, L"Boutons réels, puis les variantes simulées sur le vrai popover (origine démo).", {
            {L"Caméra", nullptr, nullptr, L"camera"}, {L"Micro", nullptr, nullptr, L"microphone"}, {L"Géolocalisation", nullptr, nullptr, L"geolocation"},
            {L"Notifications", nullptr, nullptr, L"notifications"}, {L"Presse-papiers", nullptr, nullptr, L"clipboard"},
            {L"Démo caméra", L"prompt.permission", L"camera", nullptr}, {L"Démo micro", L"prompt.permission", L"microphone", nullptr}, {L"Démo position", L"prompt.permission", L"geolocation", nullptr},
            {L"Démo notifications", L"prompt.permission", L"notifications", nullptr}, {L"Démo presse-papiers", L"prompt.permission", L"clipboard", nullptr}, {L"Démo bloqué", L"prompt.permission", L"blocked", nullptr},
            {L"Démo : utilisation en cours", L"permission.demo", L"active", nullptr}, {L"Démo : utilisation arrêtée", L"permission.demo", L"stopped", nullptr},
            {L"Démo : état du site", L"permission.demo", L"status", nullptr}, {L"Démo : réinitialiser", L"permission.demo", L"reset", nullptr}}},
        {L"B03", L"Contexte — Mots de passe : remplir (2 comptes)", Status::Demo, L"Comptes fictifs uniquement (camille.demo@exemple.test), jamais lus ni enregistrés.", {
            {L"Un compte", L"prompt.passwordFill", L"one", nullptr}, {L"Deux comptes", L"prompt.passwordFill", L"two", nullptr}}},
        {L"B04", L"Mots de passe — enregistrer, mettre à jour, remplir", Status::Demo, L"Compte fictif en mémoire.", {
            {L"Enregistrer", L"prompt.passwordSave", L"save", nullptr}, {L"Mettre à jour", L"prompt.passwordSave", L"update", nullptr}, {L"Remplir", L"prompt.passwordFill", L"two", nullptr}}},
        {L"B05", L"Réglages — Mots de passe (liste, détail, suppression)", Status::Demo, L"Données fictives en mémoire.", {
            {L"Liste", L"password.manager", L"list", nullptr}, {L"Détail", L"password.manager", L"detail", nullptr}, {L"Suppression", L"password.manager", L"delete", nullptr}}},
        {L"B06", L"Authentification HTTP + application externe", Status::Integrated, L"Le lien mailto: est réel (adresse fictive) ; l'authentification est une démo.", {
            {L"Lien mailto: (fictif)", nullptr, nullptr, L"mailto"}, {L"Démo application externe", L"prompt.externalApp", L"default", nullptr},
            {L"Authentification HTTP", L"prompt.basicAuth", L"default", nullptr}, {L"Identifiants refusés", L"prompt.basicAuth", L"error", nullptr}}},
        {L"B07", L"Téléchargement dangereux bloqué", Status::Demo, L"Aucun fichier n'est téléchargé.", {{L"Ouvrir", L"prompt.dangerousDownload", L"default", nullptr}}},
        {L"B08", L"Concepts — certificat client, partage d'écran, téléchargement", Status::Demo, L"Concepts à confirmer.", {
            {L"Certificat client", L"prompt.clientCertificate", L"default", nullptr}, {L"Partage d'écran", L"prompt.screenShare", L"default", nullptr}, {L"Partage d'écran (aucune source)", L"prompt.screenShare", L"empty", nullptr}, {L"Conserver le fichier", L"prompt.keepFile", L"default", nullptr}}},
        {L"B09", L"Note technique — faisabilité WebView2 (hors UI)", Status::Note, L"Pas d'interface : document de faisabilité.", {}},
        {L"C1", L"Contexte — Rechercher dans la page (Ctrl+F)", Status::Integrated, L"Ctrl+F sur le bloc de test « Texte pour la recherche » ci-dessous.", {
            {L"Ouvrir la barre", L"find.open", L"default", nullptr}, {L"Ouvrir avec « oxyde »", L"find.open", L"seed", nullptr}}},
        {L"C2", L"Rechercher dans la page — états", Status::Integrated, L"Vide, résultat, aucun résultat (« oxyde de zinc »), focus clavier (Tab), options Alt+C / Alt+W.", {
            {L"Ouvrir la barre", L"find.open", L"default", nullptr}}},
        {L"C3", L"Contexte — Erreur réseau « Vous êtes hors ligne »", Status::Integrated, L"Simulation dans un nouvel onglet ; la reprise réelle suit l'événement online.", {
            {L"Simuler hors ligne", L"error.offline", L"offline", nullptr}, {L"Rechargement en cours", L"error.offline", L"retrying", nullptr}, {L"Connexion rétablie", L"error.offline", L"restored", nullptr}}},
        {L"C4", L"Erreurs réseau — variantes et reprise", Status::Integrated, L"Les liens *.invalid échouent réellement (DNS) ; les autres variantes sont rendues par main.", {
            {L"Lien vers un hôte inexistant", nullptr, nullptr, L"invalidHost"}, {L"Introuvable (DNS)", L"error.page", L"nameNotResolved", nullptr}, {L"Connexion refusée", L"error.page", L"refused", nullptr},
            {L"Délai dépassé", L"error.page", L"timedOut", nullptr}, {L"Réinitialisée", L"error.page", L"reset", nullptr}, {L"Injoignable", L"error.page", L"unreachable", nullptr}, {L"Générique", L"error.page", L"generic", nullptr}}},
        {L"C5", L"Info-bulles et raccourcis clavier", Status::Demo, L"La vraie info-bulle native (variantes de démonstration) ; le panneau Raccourcis clavier s'ouvre aussi avec Ctrl+/.", {
            {L"Panneau Raccourcis clavier", L"shortcuts.open", L"default", nullptr, Tag::Integrated},
            {L"Libellé seul", L"tooltip.show", L"label", nullptr}, {L"Libellé + raccourci", L"tooltip.show", L"shortcut", nullptr}, {L"Avec détail", L"tooltip.show", L"detail", nullptr},
            {L"Titre d'onglet tronqué", L"tooltip.show", L"tab", nullptr}, {L"Bouton désactivé", L"tooltip.show", L"disabled", nullptr}}},
        {L"C6", L"Contexte — Vidéo en plein écran + indication Échap", Status::Integrated, L"requestFullscreen réel sur l'animation ci-dessous.", {
            {L"Plein écran (élément)", nullptr, nullptr, L"fullscreen"}, {L"Plein écran + clavier verrouillé", nullptr, nullptr, L"fullscreenLocked"}, {L"Indication seule", L"fullscreen.hint", L"page", nullptr}}},
        {L"C7", L"Plein écran — entrée, sortie, F11", Status::Integrated, L"F11 bascule le plein écran du navigateur.", {
            {L"Indication F11", L"fullscreen.hint", L"browser", nullptr}, {L"Indication clavier verrouillé", L"fullscreen.hint", L"locked", nullptr}}},
        {L"C8", L"Toasts d'état, notifications web, enregistrer la page", Status::Demo, L"Les notifications web restent celles de Windows (bouton Notifications en B02).", {
            {L"Réussite", L"toast.status", L"success", nullptr}, {L"Échec", L"toast.status", L"failure", nullptr}, {L"En cours", L"toast.status", L"progress", nullptr}, {L"Confirmation brève", L"toast.status", L"brief", nullptr}}},
        {L"C9", L"Contexte — Toast « Page enregistrée »", Status::Demo, L"Fichier fictif ; Ctrl+S ouvre le vrai sélecteur Windows.", {{L"Afficher le toast", L"toast.pageSaved", L"default", nullptr}}},
    };
    return list;
}
struct NativeKept { const wchar_t* title; const wchar_t* note; };
inline const std::vector<NativeKept>& NativeWindows() {
    static const std::vector<NativeKept> list = {
        {L"Sélecteur de fichiers", L"Ouvrir, enregistrer la page (Ctrl+S), choisir un dossier : fenêtre Windows, pré-remplie seulement."},
        {L"Impression", L"Dialogue et aperçu d'impression (Ctrl+P) de WebView2 / Windows."},
        {L"Windows Hello", L"Vérification d'identité système (mots de passe, passkeys)."},
        {L"Centre de notifications", L"Notifications web : nom « Browser », icône fleur, origine en attribution."},
        {L"Partage Windows", L"Feuille de partage du système."},
    };
    return list;
}

inline std::string Utf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
// The page (UTF-8). `fontRegular` / `fontMedium`: optional data: URIs of Inter Display
// (the served CSP allows font-src data: only); system-ui otherwise.
inline std::string Html(const std::wstring& token, const std::wstring& fontRegular = L"", const std::wstring& fontMedium = L"") {
    using WebText::Html;
    std::wstring h = LR"(<!doctype html><html lang="fr"><meta charset="utf-8"><meta name="color-scheme" content="dark"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Sandbox des composants</title><style>)";
    if (!fontRegular.empty()) h += L"@font-face{font-family:\"Inter Display\";src:url(" + fontRegular + L") format(\"truetype\");font-weight:400}";
    if (!fontMedium.empty()) h += L"@font-face{font-family:\"Inter Display\";src:url(" + fontMedium + L") format(\"truetype\");font-weight:500 600}";
    h += LR"CSS(
:root{color-scheme:dark}
html,body{margin:0;background:#171717;color:#FFFFFFE6;font-family:"Inter Display",Inter,system-ui,sans-serif;font-feature-settings:"cv01","cv02","cv03","cv04","cv13";font-size:14px;line-height:20px;-webkit-font-smoothing:antialiased}
.page{max-width:1180px;margin:0 auto;padding:48px 24px 96px;display:grid;grid-template-columns:minmax(0,1fr) 340px;gap:32px}
@media (max-width:900px){.page{grid-template-columns:1fr}.log{position:static!important;max-height:none!important}}
.eyebrow{color:#FFFFFF80;font-weight:500;letter-spacing:.02em;text-transform:uppercase;margin:0 0 8px}
h1{font-size:32px;line-height:40px;font-weight:600;margin:0 0 8px}
h2{font-size:16px;line-height:20px;font-weight:500;margin:40px 0 12px}
h3{font-size:14px;line-height:20px;font-weight:500;margin:0}
p{margin:0;color:#FFFFFFB3}
.muted{color:#FFFFFF80}
code,kbd{font:inherit;font-weight:500;background:#FFFFFF14;border-radius:3px;padding:0 6px;color:#FFFFFFB3}
.legend{display:flex;flex-wrap:wrap;gap:12px 24px;padding:16px 0;border-top:1px solid #FFFFFF14;border-bottom:1px solid #FFFFFF14;margin-top:24px}
.legend div{display:flex;gap:8px;align-items:center}
.badge{display:inline-flex;align-items:center;height:20px;padding:0 6px;border-radius:3px;font-size:13px;line-height:16px;font-weight:500;white-space:nowrap}
.b-int{background:rgb(62 103 255 / 16%);color:#A9BBFF}.b-demo{background:#FFFFFF14;color:#FFFFFFE6}.b-nat{border:1px dashed #FFFFFF4D;color:#FFFFFFB3}.b-note{border:1px solid #FFFFFF26;color:#FFFFFF80}.b-idx{background:#FFFFFF0A;color:#FFFFFF80}
.card{background:#292929;border:1px solid #FFFFFF0D;border-radius:10px;padding:16px;margin-top:12px}
.card header{display:flex;gap:8px;align-items:center}
.card .id{width:36px;color:#FFFFFF80;font-weight:500;flex:none}.card h3{flex:1}
.card p{margin:4px 0 0 44px}
.triggers{display:flex;flex-wrap:wrap;gap:8px;margin:12px 0 0 44px}
button,.link{height:32px;padding:0 10px;border:0;border-radius:6px;background:#FFFFFF14;color:#FFFFFFE6;font:inherit;font-weight:500;cursor:pointer;display:inline-flex;align-items:center;gap:6px;text-decoration:none}
button:hover,.link:hover{background:#333333}button:active,.link:active{background:#3E3E3E}
button.web{background:transparent;border:1px solid #FFFFFF26}button.web:hover{background:#333333}
button[aria-pressed=true]{background:#3E3E3E;color:#fff}
:focus-visible{outline:none;box-shadow:inset 0 0 0 1px #FFFFFF80!important}
.tag{font-size:12px;color:#FFFFFF80;font-weight:400}
.log{position:sticky;top:24px;align-self:start;background:#292929;border:1px solid #FFFFFF0D;border-radius:10px;padding:16px;max-height:calc(100vh - 48px);display:flex;flex-direction:column;gap:12px}
.log header{display:flex;justify-content:space-between;align-items:center}
.log ol{list-style:none;margin:0;padding:0;overflow:auto;display:flex;flex-direction:column;gap:6px;font-variant-numeric:tabular-nums}
.log li{color:#FFFFFFB3;overflow-wrap:anywhere}.log li b{font-weight:500;color:#FFFFFF80;margin-right:6px}.log li.natif b{color:#A9BBFF}
.log .empty{color:#FFFFFF80}
.find{background:#FFFFFF0A;border-radius:6px;padding:16px;color:#FFFFFFB3}
.find p+p{margin-top:8px}
.stage{margin-top:12px;display:flex;gap:16px;align-items:center;flex-wrap:wrap}
canvas{width:320px;height:180px;border-radius:6px;background:#0E0E0E;display:block}
.native{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:12px;margin-top:12px}
.native div{border:1px dashed #FFFFFF26;border-radius:10px;padding:16px}
.access li{margin:4px 0;color:#FFFFFFB3}
)CSS";
    h += L"</style><div class=\"page\"><main>";
    h += L"<p class=\"eyebrow\">Débogage · interne</p><h1>Sandbox des composants</h1>";
    h += L"<p>Chaque fiche correspond à un artboard de la page Paper « Éléments &amp; popups — conception complète ». Les boutons déclenchent les vrais composants natifs du navigateur, jamais une copie HTML : soit par le vrai chemin web (dialogues JavaScript, autorisations, plein écran, liens), soit par une commande du pont vers le composant de production en mode Démonstration (données fictives, rien n'est effacé, fermé ni enregistré).</p>";
    h += L"<h2>Accès</h2><ul class=\"access\"><li>Taper <code>browser://debug</code> dans la barre d'adresse.</li><li>Raccourci <kbd>Ctrl+Maj+F12</kbd>.</li><li>Lancer le navigateur avec <code>--debug-sandbox</code>.</li><li>La page n'est servie qu'à l'onglet ouvert par le navigateur ; ses commandes portent un jeton propre à ce lancement.</li></ul>";
    h += L"<div class=\"legend\" aria-label=\"Légende\">"
         L"<div><span class=\"badge b-int\">Intégré</span><span class=\"muted\">composant de production branché sur le vrai événement</span></div>"
         L"<div><span class=\"badge b-demo\">Démonstration</span><span class=\"muted\">composant de production, données fictives</span></div>"
         L"<div><span class=\"badge b-nat\">Natif conservé</span><span class=\"muted\">fenêtre système Windows, jamais redessinée</span></div>"
         L"<div><span class=\"tag\">Contour = test web réel · fond = commande du pont</span></div></div>";
    auto statusClass = [](Status s) { switch (s) { case Status::Integrated: return L"b-int"; case Status::Demo: return L"b-demo"; case Status::NativeKept: return L"b-nat"; case Status::Note: return L"b-note"; default: return L"b-idx"; } };
    const wchar_t* families[][2] = {{L"A", L"A · Dialogues et confirmations"}, {L"B", L"B · Autorisations, identité, sécurité"}, {L"C", L"C · Recherche, erreurs, états navigateur"}};
    auto card = [&](const Entry& e) {
        auto tagStatus = [&](const Trigger& t) { return t.tag == Tag::Integrated ? Status::Integrated : t.tag == Tag::Demo ? Status::Demo : e.status; };
        bool mixed = false; Status other = e.status; for (auto& t : e.triggers) if (tagStatus(t) != e.status) { mixed = true; other = tagStatus(t); }
        std::wstring c = L"<section class=\"card\" id=\"" + Html(e.id) + L"\" aria-labelledby=\"t-" + Html(e.id) + L"\"><header><span class=\"id\">" + Html(e.id) + L"</span><h3 id=\"t-" + Html(e.id) + L"\">" + Html(e.title) + L"</h3><span class=\"badge " + statusClass(e.status) + L"\">" + StatusLabel(e.status) + L"</span>";
        if (mixed) c += L"<span class=\"badge " + std::wstring(statusClass(other)) + L"\">" + StatusLabel(other) + L"</span>";
        c += L"</header>";
        if (*e.note) c += L"<p>" + Html(e.note) + L"</p>";
        if (!e.triggers.empty()) {
            c += L"<div class=\"triggers\">";
            for (auto& t : e.triggers) {
                const std::wstring mark = mixed ? L" <span class=\"tag\">" + std::wstring(tagStatus(t) == Status::Integrated ? L"intégré" : L"démo") + L"</span>" : L"";
                if (t.command) c += L"<button type=\"button\" data-command=\"" + Html(t.command) + L"\" data-variant=\"" + Html(t.variant ? t.variant : L"") + L"\" data-label=\"" + Html(std::wstring(e.id) + L" · " + t.label) + L"\">" + Html(t.label) + mark + L"</button>";
                else if (std::wstring(t.js) == L"mailto") c += L"<a class=\"link\" href=\"mailto:camille.demo@exemple.test?subject=Sandbox\" data-log=\"B06 · mailto: ouvert (adresse fictive)\">" + Html(t.label) + L"</a>";
                else if (std::wstring(t.js) == L"invalidHost") c += L"<a class=\"link\" href=\"https://hote-inexistant.invalid/sandbox\" target=\"_blank\" data-log=\"C4 · navigation vers hote-inexistant.invalid\">" + Html(t.label) + L"</a>";
                else c += L"<button type=\"button\" class=\"web\" data-js=\"" + Html(t.js) + L"\"" + (std::wstring(t.js) == L"beforeunload" ? L" aria-pressed=\"false\"" : L"") + L">" + Html(t.label) + L"</button>";
            }
            c += L"</div>";
        }
        return c + L"</section>";
    };
    for (auto& e : Catalogue()) if (e.status == Status::Index) h += card(e);
    for (auto& f : families) {
        h += L"<h2 id=\"famille-" + std::wstring(f[0]) + L"\">" + Html(f[1]) + L"</h2>";
        for (auto& e : Catalogue()) if (e.id[0] == f[0][0] && e.status != Status::Index) h += card(e);
        if (f[0][0] == L'C') {
            h += L"<h2>Texte pour la recherche (Ctrl+F)</h2><div class=\"find\" id=\"texte-recherche\">"
                 L"<p>Le cuivre exposé à l'air humide se couvre d'une fine couche d'oxyde, puis de carbonate : le vert-de-gris. À haute température il forme l'oxyde de cuivre(I), rouge, puis l'oxyde de cuivre(II), noir.</p>"
                 L"<p>Oxyde, OXYDE, oxyde, oxydé, oxydée, oxydation. Été, été, ÉTÉ, ete. Cœur, cœur, CŒUR. L'œuvre « Œdipe » ; naïve, naïf, NAÏF.</p>"
                 L"<p>Mots entiers : chat, chats, chaton, achat. Casse : Paris, paris, PARIS. Recherche sans résultat : « oxyde de zinc ».</p></div>";
            h += L"<h2>Plein écran (C6)</h2><div class=\"stage\"><canvas id=\"scene\" width=\"640\" height=\"360\" aria-label=\"Animation de test pour le plein écran\"></canvas><p class=\"muted\">Échap quitte le plein écran de l'élément ; avec le clavier verrouillé, maintenez Échap 1,5 s. F11 : plein écran du navigateur.</p></div>";
        }
    }
    h += L"<h2>Natif conservé</h2><div class=\"native\">";
    for (auto& n : NativeWindows()) h += L"<div><header><span class=\"badge b-nat\">Natif conservé</span></header><h3 style=\"margin-top:8px\">" + Html(n.title) + L"</h3><p>" + Html(n.note) + L"</p></div>";
    h += L"</div></main>";
    h += L"<aside class=\"log\" aria-label=\"Journal des résultats\"><header><h3>Journal</h3><button type=\"button\" id=\"reset\">Réinitialiser</button></header>"
         L"<p class=\"muted\" id=\"bridge\">Pont : en attente…</p><ol id=\"log\" aria-live=\"polite\" aria-relevant=\"additions\"><li class=\"empty\">Aucun résultat pour l'instant.</li></ol></aside></div>";
    h += L"<script>\"use strict\";const TOKEN=\"" + WebText::Js(token) + L"\";";
    h += LR"JS(
const L=document.getElementById("log");let unload=null;
const time=()=>new Date().toLocaleTimeString("fr-FR");
window.__sandboxLog=(text,kind)=>{if(kind==="natif")document.getElementById("bridge").textContent="Pont : actif";const e=L.querySelector(".empty");if(e)e.remove();const li=document.createElement("li");if(kind)li.className=kind;const b=document.createElement("b");b.textContent=time();li.append(b,document.createTextNode(String(text)));L.prepend(li);while(L.children.length>200)L.lastChild.remove();};
const log=t=>window.__sandboxLog(t,"web");
const send=(command,variant,via)=>{try{const m=variant?{token:TOKEN,command,variant}:{token:TOKEN,command};if(via)m.via=via;window.chrome.webview.postMessage(JSON.stringify(m))}catch(e){log("Pont indisponible : "+e.message)}};
document.getElementById("reset").addEventListener("click",()=>{L.replaceChildren();const li=document.createElement("li");li.className="empty";li.textContent="Journal réinitialisé.";L.append(li)});
const media=async(c,label)=>{try{const s=await navigator.mediaDevices.getUserMedia(c);s.getTracks().forEach(t=>t.stop());log(label+" : autorisé")}catch(e){log(label+" : "+e.name)}};
const scene=document.getElementById("scene");
const full=async lock=>{try{await scene.requestFullscreen();log("C6 · plein écran demandé");if(lock&&navigator.keyboard&&navigator.keyboard.lock){await navigator.keyboard.lock(["Escape"]);log("C6 · Échap verrouillé par la page")}}catch(e){log("C6 · plein écran refusé : "+e.name)}};
document.addEventListener("fullscreenchange",()=>log(document.fullscreenElement?"C6 · entrée en plein écran":"C6 · sortie du plein écran"));
const tests={
 alert:()=>{window.alert("Bonjour depuis la sandbox des composants.");log("A2 · alert fermée")},
 confirm:()=>log("A2 · confirm → "+(window.confirm("Confirmer cette action de démonstration ?")?"OK":"Annuler")),
 prompt:()=>{const v=window.prompt("Votre prénom (fictif) :","Camille");log("A3 · prompt → "+(v===null?"annulé":"« "+v+" »"))},
 beforeunload:b=>{if(unload){removeEventListener("beforeunload",unload);unload=null}else{unload=e=>{e.preventDefault();e.returnValue=""};addEventListener("beforeunload",unload)}b.setAttribute("aria-pressed",String(!!unload));log("A4 · beforeunload "+(unload?"installé":"retiré"))},
 reload:()=>{log("A4 · rechargement demandé");location.reload()},
 leave:()=>{log("A4 · départ demandé");location.href="about:blank"},
 camera:()=>media({video:true},"B02 · caméra"),microphone:()=>media({audio:true},"B02 · micro"),cameraMicrophone:()=>media({video:true,audio:true},"B01 · caméra + micro"),
 geolocation:()=>navigator.geolocation.getCurrentPosition(()=>log("B02 · position : autorisée"),e=>log("B02 · position : refusée ("+e.code+")")),
 notifications:async()=>{try{log("B02 · notifications : "+await Notification.requestPermission())}catch(e){log("B02 · notifications : "+e.name)}},
 clipboard:async()=>{try{const t=await navigator.clipboard.readText();log("B02 · presse-papiers : lu ("+t.length+" caractères)")}catch(e){log("B02 · presse-papiers : "+e.name)}},
 fullscreen:()=>full(false),fullscreenLocked:()=>full(true)
};
document.addEventListener("click",e=>{const b=e.target.closest("button,a");if(!b)return;
 if(b.dataset.command){send(b.dataset.command,b.dataset.variant,e.detail===0?"keyboard":"pointer");log("→ "+b.dataset.label)}
 else if(b.dataset.js&&tests[b.dataset.js])tests[b.dataset.js](b);
 else if(b.dataset.log)log(b.dataset.log)});
{const ctx=scene.getContext("2d");let t=0;const draw=()=>{const w=scene.width,h=scene.height;const g=ctx.createLinearGradient(0,0,0,h);g.addColorStop(0,"#1E2A44");g.addColorStop(1,"#0E0E0E");ctx.fillStyle=g;ctx.fillRect(0,0,w,h);const y=h*.55+Math.sin(t/40)*h*.08;ctx.fillStyle="#3E67FF";ctx.beginPath();ctx.arc(w*.5,y,h*.12,0,Math.PI*2);ctx.fill();ctx.fillStyle="#171717";ctx.fillRect(0,h*.62,w,h*.38);t++;if(!matchMedia("(prefers-reduced-motion: reduce)").matches)requestAnimationFrame(draw)};draw()}
send("sandbox.ping");
)JS";
    h += L"</script></html>";
    return Utf8(h);
}
} // namespace DebugSandbox
} // namespace NativeChrome
