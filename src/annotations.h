#pragma once
// Annotation store (V2): the file contract shared by browser.exe (writer, native UI) and
// browser-annotations-mcp.exe (reader/claimer, MCP stdio bridge). See design/annotations-store-bridge.md.
//
// V2 groups annotations by SITE: the origin "scheme://host[:port]" of the annotated page is the key; there
// is no project any more (projects.json is ignored and never written).
//
//   <root> = <dataDir>\annotations
//     readers\<client>.json              written by the bridge (<= 1 write / 10 s / client)
//     <siteKey>\<annId>\annotation.json     "schema":"browser-annotation/2", "site":"https://example.com"
//     <siteKey>\<annId>\shot.png            cropped capture (optional)
//     <siteKey>\<annId>\state.json          "schema":"browser-annotation-state/1" (authoritative status)
//     <siteKey>\<annId>\claim               lease {"token","agent","expiresAt"}, created with CREATE_NEW
//     <siteKey>\<annId>\claim.lock          short lock (CREATE_NEW + DELETE_ON_CLOSE), never left behind
//     <siteKey>\drafts\<annId>\...          drafts: never listed for agents
//     trash\<siteKey>\<annId>\...           deleted, restorable for a few seconds ("Annuler"), then purged
//
// <siteKey> = SiteKey(origin): a safe folder name, readable part + hash ("localhost-5173-1f2e3d4c").
// V1 stores are read tolerantly: a V1 project folder (e.g. "boutique") is just another folder; its
// "browser-annotation/1" entries are listed with site = origin of their page URL. Nothing is migrated.
//
// Threading: no UI, no globals, no background work. Every Store method performs synchronous file I/O and
// BLOCKS the calling thread (claim/release/resolve/reopen may also wait up to LockTimeoutMs for the short
// lock). The browser must call them off the UI thread. Functions documented "pure" do no I/O.
// Several Store objects (threads or processes) may target the same root at once.
//
// Safety: ids and site keys are validated before any path is built; the root, site, drafts and
// annotation directories must be real directories (no symlink/junction); files are opened with
// FILE_FLAG_OPEN_REPARSE_POINT and refused if they are reparse points or hard-linked. Every write goes
// through a temporary file + MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH). Readers are strict: unknown
// schema major, invalid JSON, truncated file, id/site mismatch => the entry is skipped, never a crash.
//
// Every Store method taking `site` accepts either an origin ("https://example.com", or any URL of that site)
// or a folder key as found in Annotation::siteKey (V2 key or V1 project folder).
//
// Time is Unix seconds (UTC) from the Store's clock, which tests replace to simulate retention/leases.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "strict-json.h"

namespace BrowserAnnotations {

// ---------------------------------------------------------------- contract constants
constexpr const char* AnnotationSchema = "browser-annotation/2";  // "browser-annotation/1" is still read
constexpr const char* StateSchema = "browser-annotation-state/1";
constexpr const char* ProjectsSchema = "browser-annotation-projects/1";  // V1, no longer read or written

constexpr std::size_t MaxNoteChars = 4000;       // characters = Unicode code points
constexpr std::size_t MaxTextChars = 2000;       // target.text / selectionText
constexpr std::size_t MaxSummaryChars = 2000;    // resolution summary
constexpr std::size_t MaxEventSummaryChars = 280;
constexpr std::size_t MaxTitleChars = 300;
constexpr std::size_t MaxUrlBytes = 4096;
constexpr std::size_t MaxSelectorChars = 1024;
constexpr std::size_t MaxPathEntries = 16;       // ancestors kept (closest to the target)
constexpr std::size_t MaxPathEntryChars = 200;
constexpr std::size_t MaxAttrValueChars = 512;
constexpr std::size_t MaxStyleValueChars = 200;
constexpr std::size_t MaxSourceAttributes = 8;
constexpr std::size_t MaxLabelChars = 80;        // agent label, client label
constexpr std::size_t MaxEvents = 50;
constexpr std::uint64_t MaxAnnotationFileBytes = 256 * 1024;
constexpr std::uint64_t MaxStateFileBytes = 64 * 1024;
constexpr std::uint64_t MaxProjectsFileBytes = 256 * 1024;
constexpr std::uint64_t MaxSmallFileBytes = 4 * 1024;  // claim, readers\*.json
constexpr std::uint64_t MaxShotBytes = 1536 * 1024;    // 1.5 MiB
constexpr std::uint32_t MaxShotSide = 1600;
constexpr std::size_t MaxAnnotationsPerSite = 500;  // drafts included
constexpr std::size_t MaxAnnotationsPerProject = MaxAnnotationsPerSite;  // V1 name
constexpr std::uint64_t MaxStoreBytes = 200ull * 1024 * 1024;
constexpr std::int64_t LeaseSeconds = 30 * 60;
constexpr std::int64_t DraftRetentionSeconds = 7 * 86400;
constexpr std::int64_t ClosedRetentionSeconds = 30 * 86400;  // "done": resolved / needs_info / dismissed
constexpr std::int64_t TrashRetentionSeconds = 120;  // a trashed entry is purged after this (undo toast = 6 s)
constexpr std::int64_t ReaderWriteIntervalSeconds = 10;
constexpr unsigned LockTimeoutMs = 2000;
constexpr unsigned StaleClaimFileSeconds = 60;  // an unreadable claim file older than this (real time) is expired

// ---------------------------------------------------------------- identifiers (pure unless noted)
// "ann_" + 26 lowercase base32 chars (130 random bits). NewAnnotationId/NewClaimToken use BCryptGenRandom.
std::string NewAnnotationId();
std::string NewClaimToken();  // "clm_" + 26 lowercase base32 chars
bool IsValidAnnotationId(std::string_view id);
bool IsValidClaimToken(std::string_view token);
// V1 project id: [a-z0-9][a-z0-9-]{0,39}, not "drafts"/"readers"/"trash", not a Windows device name.
bool IsValidProjectId(std::string_view id);
// Folder key of a site: [a-z0-9][a-z0-9-]{0,63}, not "drafts"/"readers"/"trash", not a device name.
// Every V1 project id is a valid site key (V1 folders stay readable).
bool IsValidSiteKey(std::string_view key);
// Client slug for readers\<client>.json from an untrusted clientInfo.name: [a-z0-9-], <= 40, never empty.
std::string ClientSlug(std::string_view clientName);

// ---------------------------------------------------------------- time (pure)
std::int64_t SystemNow();                        // current UTC Unix seconds (reads the system clock)
std::string FormatTime(std::int64_t unixSeconds);  // "2026-11-09T14:03:12Z"
bool ParseTime(std::string_view text, std::int64_t& unixSeconds);  // accepts optional ".fff" before Z

// ---------------------------------------------------------------- text and URL policy (pure)
// Keeps at most maxChars code points; drops control characters (\n and \t kept only when keepNewlines)
// and bidi overrides/isolates; replaces invalid UTF-8 with U+FFFD.
std::string CleanText(std::string_view text, std::size_t maxChars, bool keepNewlines = false);
// Drops query and fragment unless the host is localhost, *.localhost, 127.x.x.x or [::1]. Rejects
// credentials in the URL (they are removed). `redacted` reports whether something was removed.
std::string RedactUrl(std::string_view url, bool* redacted = nullptr);
// The ~30 computed-style properties an annotation may carry (layout, typography, colour).
const std::vector<std::string>& AllowedStyleProperties();
// Element attributes an annotation may carry (href is stored without query/fragment).
const std::vector<std::string>& AllowedAttributes();
// ---------------------------------------------------------------- sites (pure)
// Origin of a URL: "http(s)://host[:port]" lower-case, default port and credentials dropped (same rule as
// AnnotationHost::OriginOf). Any other scheme (file:, data:, about:...) or an unparsable URL => "".
// Accepts an origin as well (idempotent).
std::string SiteOrigin(std::string_view urlOrOrigin);
// Folder key for an origin (SiteOrigin applied first): readable host[-port] (a-z0-9 and '-', <= 40 chars)
// + '-' + 8 hex digits of a 64-bit FNV-1a hash of the origin. "" (no origin) => "local-<hash>".
// Deterministic across processes and versions: the bridge and the browser compute the same folder.
std::string SiteKey(std::string_view origin);
// What the UI shows for a site: "example.com", "localhost:5173"; "" => "Autres pages".
std::string SiteLabel(std::string_view origin);
// True when `s` looks like an origin/URL ("...://...") rather than a folder key.
bool IsSiteOrigin(std::string_view s);

// PNG signature + IHDR check, size <= MaxShotBytes, sides in 1..MaxShotSide.
bool InspectPng(std::string_view bytes, std::uint32_t& width, std::uint32_t& height);

// ---------------------------------------------------------------- model
enum class Status { Open, Claimed, Resolved, NeedsInfo, Dismissed };
const char* StatusName(Status s);                     // "open" | "claimed" | "resolved" | "needs_info" | "dismissed"
bool ParseStatus(std::string_view text, Status& out);

struct Rect { double x = 0, y = 0, width = 0, height = 0; };
struct Viewport { double width = 0, height = 0, scrollX = 0, scrollY = 0, devicePixelRatio = 1, zoom = 1; };
struct NamedValue {
    std::string name, value;
    bool isNull = false;  // attributes only: JSON null (attribute absent)
};

struct Source {
    std::string channel, version, storeTag;  // source.instance {channel, version, store}
    std::int64_t tabSerial = 0, navStamp = 0;
    std::string url;  // RedactUrl() is applied on write and on read
    bool urlRedacted = false;
    std::string title;
    std::string frameKind = "main";  // "main" | "same-origin" | "cross-origin"
};

struct Target {
    std::string kind = "element";  // "element" | "selection"
    std::string selector;
    bool selectorUnique = false;
    std::vector<std::string> path, shadowPath;
    std::string tag, role, accessibleName;
    std::vector<NamedValue> attributes;  // AllowedAttributes() only
    std::string text;
    std::optional<std::string> selectionText;
    Rect rect;          // CSS px
    Viewport viewport;
    std::vector<NamedValue> styles;            // AllowedStyleProperties() only
    std::vector<NamedValue> sourceAttributes;  // hints.sourceAttributes: data-* only, <= MaxSourceAttributes
};

struct Screenshot {
    bool present = false;  // shot.png exists (file name and mime are fixed: shot.png, image/png)
    std::uint32_t width = 0, height = 0;
    double scale = 1;
    std::string crop;  // e.g. "element+margin"
    std::uint64_t bytes = 0;
};

struct Annotation {
    std::string id;
    std::string site;     // origin "https://example.com" ("" = page without an http(s) origin)
    std::string siteKey;  // folder (filled by WriteAnnotation and on read; ignored on input)
    std::int64_t createdAt = 0, updatedAt = 0;
    std::string note;
    Source source;
    Target target;
    Screenshot screenshot;
};

struct Lease { std::string agent; std::int64_t until = 0; };
struct Resolution { std::string outcome, summary, by; std::int64_t at = 0; };
struct Event { std::int64_t at = 0; std::string actor, from, to, summary; };
struct State {
    Status status = Status::Open;
    std::int64_t updatedAt = 0;
    std::optional<Lease> claimedBy;
    std::optional<Resolution> resolution;
    std::vector<Event> events;  // oldest first, <= MaxEvents
};

struct Reader {
    std::string client;   // slug (file name)
    std::string label;    // display name ("Claude Desktop")
    std::string site;     // origin the bridge is restricted to (--site); empty = every site
    std::int64_t lastReadAt = 0;
};

// One annotation as seen by readers. `state` is the EFFECTIVE state: a claim whose lease expired reads as
// open (nothing is written by a read); a valid lease on an open/claimed annotation reads as claimed.
struct Entry {
    Annotation annotation;
    State state;
    bool draft = false;
};

// ---------------------------------------------------------------- serialization (pure)
// Sanitize() enforces every bound and allow-list above (truncation, control characters, URL redaction,
// attribute/style allow-lists, path length, finite numbers). Write and read paths both apply it.
void Sanitize(Annotation& a);
StrictJson::Value AnnotationToJson(const Annotation& a);
bool AnnotationFromJson(const StrictJson::Value& v, Annotation& out, std::string* error = nullptr);
StrictJson::Value StateToJson(const State& s);
bool StateFromJson(const StrictJson::Value& v, State& out, std::string* error = nullptr);
// What agents and the drawer see for one entry: annotation fields + status, claimedBy, resolution, events.
StrictJson::Value EntryToJson(const Entry& e, bool includeEvents = true);
// "Copier en Markdown": the annotation as Markdown for pasting into an agent chat. Captured page content is
// fenced and labelled as data. `shotPath` (optional) is mentioned when non-empty.
std::string ToMarkdown(const Entry& e, const std::wstring& shotPath = L"");

// ---------------------------------------------------------------- store location (pure)
std::wstring StoreRootForDataDir(const std::wstring& dataDir);  // <dataDir>\annotations
// Store root for an executable placed next to browser.exe, mirroring UpdateCore::ResolveDataPaths for that
// sibling browser.exe: <installRoot>\versions\<M.m.B>\* and the stable copy <installRoot>\bin\* map to
// <installRoot>\data\annotations; anything else (portable/preview/worktree) to <exe dir>\annotations.
// installRoot defaults to %LOCALAPPDATA%\browser.
std::wstring DefaultStoreRoot(const std::wstring& exePath, const std::wstring& installRoot = L"");

// ---------------------------------------------------------------- store operations (all BLOCK on file I/O)
enum class Code {
    Ok,
    NotFound,      // no such annotation/site (or skipped as corrupt)
    Invalid,       // bad argument (id, site, outcome, empty summary, bad PNG...)
    Conflict,      // claimed by someone else (holder/holderUntil filled)
    WrongToken,    // claim_token does not match the current lease
    NotClaimable,  // status is resolved / needs_info / dismissed
    Busy,          // the short lock could not be taken within LockTimeoutMs
    LimitReached,  // MaxAnnotationsPerSite or MaxStoreBytes
    Unsafe,        // reparse point / hard link / path escaping the root
    Io,            // other file system failure
};
const char* CodeName(Code c);  // "ok", "not_found", "invalid", "conflict", ...

struct Outcome {
    Code code = Code::Ok;
    std::string message;      // French, user/agent readable
    std::string holder;       // Conflict: agent holding the lease
    std::int64_t holderUntil = 0;
    bool ok() const { return code == Code::Ok; }
};

struct ClaimResult {
    std::string token, agent;
    std::int64_t expiresAt = 0;
    bool renewed = false;
};

// History panel filters: "En cours" = open + claimed; "Terminées" = resolved + needs_info + dismissed.
enum class Progress { Any, InProgress, Done };
bool IsDone(Status s);  // resolved / needs_info / dismissed

struct ListQuery {
    std::string site;               // empty = every site; origin (also matches V1 entries) or folder key
    std::optional<Status> status;   // nullopt = every status
    Progress progress = Progress::Any;  // applied together with `status`
    bool drafts = false;            // true = list drafts\ instead of published annotations (browser only)
    bool newestFirst = false;       // default: oldest first (work queue order)
    std::size_t limit = 0;          // 0 = no limit
    std::string cursor;             // from ListPage::nextCursor
};
struct ListPage {
    std::vector<Entry> entries;
    std::string nextCursor;  // empty when there is nothing more
    std::size_t skipped = 0;  // corrupt/unsafe entries ignored
    bool badCursor = false;   // the query cursor was malformed (entries is then empty)
};

struct PurgeReport { std::size_t drafts = 0, closed = 0, tempFiles = 0, trash = 0; };

// History panel grouping of a newest-first list: groups by site in order of first appearance (the most
// recently annotated site first), entries keep their order. Pure.
struct SiteGroup {
    std::string site, siteKey, label;  // label = SiteLabel(site)
    std::vector<Entry> entries;
};
std::vector<SiteGroup> GroupBySite(std::vector<Entry> entries);

using Clock = std::function<std::int64_t()>;

class Store {
public:
    explicit Store(std::wstring root, Clock clock = nullptr);
    const std::wstring& Root() const { return root_; }
    std::int64_t Now() const;

    // Creates <root> (one level; its parent must exist). Refuses a root that is a reparse point.
    Outcome EnsureRoot() const;
    // True when <root> exists, is a directory and not a reparse point.
    bool RootUsable() const;

    // Writes (creates or edits) an annotation. Empty a.id => a new id is generated. The site is a.site
    // (SiteOrigin applied) or, when a.site is empty, the origin of a.source.url; a.site and a.siteKey are
    // set on success. An edit must keep the site. Sanitizes `a` in place,
    // sets createdAt (kept from the existing file on edit) and updatedAt. png: nullptr keeps the existing
    // capture (if any); otherwise the PNG is validated and written as shot.png. draft=false publishes: a new
    // state.json "open" is written if none exists. annotation.json is written last, so readers never see a
    // half-written entry. Enforces MaxAnnotationsPerSite (purging the oldest closed ones first) and
    // MaxStoreBytes.
    Outcome WriteAnnotation(Annotation& a, const std::string* png, bool draft) const;
    // drafts\<id> -> <siteKey>\<id> and state "open".
    Outcome PublishDraft(const std::string& site, const std::string& id) const;
    // Reads one entry (effective state). Draft entries have the default open state.
    Outcome Read(const std::string& site, const std::string& id, Entry& out, bool draft = false) const;
    // Finds a published annotation by id in any site (or only `site` when non-empty: an origin also matches
    // V1 entries of that origin kept in legacy folders).
    Outcome Find(const std::string& id, Entry& out, const std::string& site = "") const;
    // shot.png bytes (validated PNG).
    Outcome ReadShot(const std::string& site, const std::string& id, std::string& png, bool draft = false) const;
    std::wstring ShotPath(const std::string& site, const std::string& id, bool draft = false) const;
    ListPage List(const ListQuery& q) const;

    // Lease (agent side). token empty => new claim; token = current lease token => renewal (30 min more).
    Outcome Claim(const std::string& site, const std::string& id, const std::string& agent,
                  const std::string& token, ClaimResult& out) const;
    // reason (optional, <= MaxEventSummaryChars): why the agent gives it back (« pas d'accès au code »); shown in the history.
    Outcome Release(const std::string& site, const std::string& id, const std::string& token,
                    const std::string& actor, const std::string& reason = "") const;
    // outcome: "resolved" | "needs_info" | "dismissed"; summary required (<= MaxSummaryChars).
    Outcome Resolve(const std::string& site, const std::string& id, const std::string& token,
                    const std::string& outcome, const std::string& summary, const std::string& actor,
                    State* newState = nullptr) const;

    // User side (drawer): "Libérer" and "Rouvrir" (any status -> open; lease dropped).
    Outcome ForceRelease(const std::string& site, const std::string& id) const;
    Outcome Reopen(const std::string& site, const std::string& id) const;
    // Permanent deletion: one annotation (any state), or every annotation and draft of a site folder.
    Outcome Delete(const std::string& site, const std::string& id, bool draft = false) const;
    Outcome DeleteSite(const std::string& site) const;
    Outcome DeleteProject(const std::string& project) const { return DeleteSite(project); }  // V1 name

    // History "Supprimer" + "Annuler" (6 s toast). Trash() moves <siteKey>\<id> to trash\<siteKey>\<id> in
    // one rename: from then on agents no longer see it (list/get/claim => not found). Restore() moves it
    // back unchanged (state, lease, capture). DiscardTrash() deletes it for good (call when the toast
    // closes); anything left in trash (crash, browser closed) is removed by Purge() after
    // TrashRetentionSeconds. Busy when a file of the entry is open (agent call in flight): retry.
    // Restore => Conflict if an entry with that id exists again, NotFound if the trash item is gone.
    Outcome Trash(const std::string& site, const std::string& id) const;
    Outcome Restore(const std::string& site, const std::string& id) const;
    Outcome DiscardTrash(const std::string& site, const std::string& id) const;

    // Launcher counter: published annotations an agent is handling now (effective status "claimed" with a
    // valid lease). Reads only state.json + claim per entry. Trash and drafts are not counted.
    std::size_t CountClaimed() const;

    // Retention (call when the history opens / at launch, never on a timer): drafts older than 7 days, done
    // annotations (resolved / needs_info / dismissed) older than 30 days, trash older than
    // TrashRetentionSeconds, temporary files left by a crash. closedRetentionSeconds: the « Conserver les annotations
    // terminées » setting (7 / 30 / 90 days); <= 0 = the default ClosedRetentionSeconds.
    PurgeReport Purge(std::int64_t closedRetentionSeconds = 0) const;
    std::uint64_t StoreBytes() const;

    // readers\<client>.json (the bridge throttles to one write / 10 s / client).
    Outcome WriteReader(const Reader& r) const;
    std::vector<Reader> ListReaders() const;

private:
    std::wstring root_;
    Clock clock_;
};

}  // namespace BrowserAnnotations
