#pragma once
// Annotation store (V1): the file contract shared by browser.exe (writer, native UI) and
// browser-annotations-mcp.exe (reader/claimer, MCP stdio bridge). See design/annotations-v1-brief.md.
//
//   <root> = <dataDir>\annotations
//     projects.json                      {"schema":"browser-annotation-projects/1","projects":[...]}
//     readers\<client>.json              written by the bridge (<= 1 write / 10 s / client)
//     <projectId>\<annId>\annotation.json   "schema":"browser-annotation/1"
//     <projectId>\<annId>\shot.png          cropped capture (optional)
//     <projectId>\<annId>\state.json        "schema":"browser-annotation-state/1" (authoritative status)
//     <projectId>\<annId>\claim             lease {"token","agent","expiresAt"}, created with CREATE_NEW
//     <projectId>\<annId>\claim.lock        short lock (CREATE_NEW + DELETE_ON_CLOSE), never left behind
//     <projectId>\drafts\<annId>\...        drafts: never listed for agents
//
// Threading: no UI, no globals, no background work. Every Store method performs synchronous file I/O and
// BLOCKS the calling thread (claim/release/resolve/reopen may also wait up to LockTimeoutMs for the short
// lock). The browser must call them off the UI thread. Functions documented "pure" do no I/O.
// Several Store objects (threads or processes) may target the same root at once.
//
// Safety: ids and project ids are validated before any path is built; the root, project, drafts and
// annotation directories must be real directories (no symlink/junction); files are opened with
// FILE_FLAG_OPEN_REPARSE_POINT and refused if they are reparse points or hard-linked. Every write goes
// through a temporary file + MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH). Readers are strict: unknown
// schema major, invalid JSON, truncated file, id/project mismatch => the entry is skipped, never a crash.
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
constexpr const char* AnnotationSchema = "browser-annotation/1";
constexpr const char* StateSchema = "browser-annotation-state/1";
constexpr const char* ProjectsSchema = "browser-annotation-projects/1";

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
constexpr std::size_t MaxLabelChars = 80;        // project label, agent label, client label
constexpr std::size_t MaxEvents = 50;
constexpr std::size_t MaxProjects = 100;
constexpr std::size_t MaxOriginsPerProject = 20;
constexpr std::uint64_t MaxAnnotationFileBytes = 256 * 1024;
constexpr std::uint64_t MaxStateFileBytes = 64 * 1024;
constexpr std::uint64_t MaxProjectsFileBytes = 256 * 1024;
constexpr std::uint64_t MaxSmallFileBytes = 4 * 1024;  // claim, readers\*.json
constexpr std::uint64_t MaxShotBytes = 1536 * 1024;    // 1.5 MiB
constexpr std::uint32_t MaxShotSide = 1600;
constexpr std::size_t MaxAnnotationsPerProject = 500;  // drafts included
constexpr std::uint64_t MaxStoreBytes = 200ull * 1024 * 1024;
constexpr std::int64_t LeaseSeconds = 30 * 60;
constexpr std::int64_t DraftRetentionSeconds = 7 * 86400;
constexpr std::int64_t ClosedRetentionSeconds = 30 * 86400;  // resolved / dismissed
constexpr std::int64_t ReaderWriteIntervalSeconds = 10;
constexpr unsigned LockTimeoutMs = 2000;
constexpr unsigned StaleClaimFileSeconds = 60;  // an unreadable claim file older than this (real time) is expired

// ---------------------------------------------------------------- identifiers (pure unless noted)
// "ann_" + 26 lowercase base32 chars (130 random bits). NewAnnotationId/NewClaimToken use BCryptGenRandom.
std::string NewAnnotationId();
std::string NewClaimToken();  // "clm_" + 26 lowercase base32 chars
bool IsValidAnnotationId(std::string_view id);
bool IsValidClaimToken(std::string_view token);
// Project id: [a-z0-9][a-z0-9-]{0,39}, not "drafts"/"readers", not a Windows device name.
bool IsValidProjectId(std::string_view id);
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
    std::string id, project;
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

struct Project {
    std::string id, label;
    std::vector<std::string> origins;  // "scheme://host[:port]"
};

struct Reader {
    std::string client;   // slug (file name)
    std::string label;    // display name ("Claude Desktop")
    std::string project;  // empty = none
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
    NotFound,      // no such annotation/project (or skipped as corrupt)
    Invalid,       // bad argument (id, project, outcome, empty summary, bad PNG...)
    Conflict,      // claimed by someone else (holder/holderUntil filled)
    WrongToken,    // claim_token does not match the current lease
    NotClaimable,  // status is resolved / needs_info / dismissed
    Busy,          // the short lock could not be taken within LockTimeoutMs
    LimitReached,  // MaxAnnotationsPerProject or MaxStoreBytes
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

struct ListQuery {
    std::string project;            // empty = every project
    std::optional<Status> status;   // nullopt = every status
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

struct PurgeReport { std::size_t drafts = 0, closed = 0, tempFiles = 0; };

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

    // Projects (projects.json). Load skips invalid entries; a missing/corrupt file yields an empty list.
    std::vector<Project> LoadProjects() const;
    Outcome SaveProjects(const std::vector<Project>& projects) const;

    // Writes (creates or edits) an annotation. Empty a.id => a new id is generated. Sanitizes `a` in place,
    // sets createdAt (kept from the existing file on edit) and updatedAt. png: nullptr keeps the existing
    // capture (if any); otherwise the PNG is validated and written as shot.png. draft=false publishes: a new
    // state.json "open" is written if none exists. annotation.json is written last, so readers never see a
    // half-written entry. Enforces MaxAnnotationsPerProject (purging the oldest closed ones first) and
    // MaxStoreBytes.
    Outcome WriteAnnotation(Annotation& a, const std::string* png, bool draft) const;
    // drafts\<id> -> <project>\<id> and state "open".
    Outcome PublishDraft(const std::string& project, const std::string& id) const;
    // Reads one entry (effective state). Draft entries have the default open state.
    Outcome Read(const std::string& project, const std::string& id, Entry& out, bool draft = false) const;
    // Finds a published annotation by id in any project (or only `project` when non-empty).
    Outcome Find(const std::string& id, Entry& out, const std::string& project = "") const;
    // shot.png bytes (validated PNG).
    Outcome ReadShot(const std::string& project, const std::string& id, std::string& png, bool draft = false) const;
    std::wstring ShotPath(const std::string& project, const std::string& id, bool draft = false) const;
    ListPage List(const ListQuery& q) const;

    // Lease (agent side). token empty => new claim; token = current lease token => renewal (30 min more).
    Outcome Claim(const std::string& project, const std::string& id, const std::string& agent,
                  const std::string& token, ClaimResult& out) const;
    Outcome Release(const std::string& project, const std::string& id, const std::string& token,
                    const std::string& actor) const;
    // outcome: "resolved" | "needs_info" | "dismissed"; summary required (<= MaxSummaryChars).
    Outcome Resolve(const std::string& project, const std::string& id, const std::string& token,
                    const std::string& outcome, const std::string& summary, const std::string& actor,
                    State* newState = nullptr) const;

    // User side (drawer): "Libérer" and "Rouvrir" (any status -> open; lease dropped).
    Outcome ForceRelease(const std::string& project, const std::string& id) const;
    Outcome Reopen(const std::string& project, const std::string& id) const;
    // Deletion: one annotation (any state), or every annotation and draft of a project.
    Outcome Delete(const std::string& project, const std::string& id, bool draft = false) const;
    Outcome DeleteProject(const std::string& project) const;

    // Retention (call when the drawer opens / at launch, never on a timer): drafts older than 7 days,
    // resolved/dismissed older than 30 days, temporary files left by a crash.
    PurgeReport Purge() const;
    std::uint64_t StoreBytes() const;

    // readers\<client>.json (the bridge throttles to one write / 10 s / client).
    Outcome WriteReader(const Reader& r) const;
    std::vector<Reader> ListReaders() const;

private:
    std::wstring root_;
    Clock clock_;
};

}  // namespace BrowserAnnotations
