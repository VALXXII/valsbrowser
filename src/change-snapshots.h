#pragma once
// Change snapshots (chat V1, contract C): our own copies of the project's files, so a chat turn can be undone
// without git. See design/chat-snapshots.md (API for the browser side, storage format, limits).
//
//   <root> = <data root>\chat-snapshots            one store for every profile, like <data root>\annotations
//     <projectKey>\project.json                    "schema":"browser-chat-snapshots/1", the project folder
//     <projectKey>\index                           last known state of the folder (path, size, date, SHA-256)
//     <projectKey>\blobs\<aa>\<62 hex>             file contents, raw, named by their SHA-256 (deduplicated)
//     <projectKey>\turns\<seq>-<turnId>.json       one record per turn: what it changed (before/after)
//     <projectKey>\open-turn.json + turn.lock      a turn is running (the lock is held while it runs)
//     <projectKey>\restore.json                    journal of a restore under way (crash-safe resume)
//     <projectKey>\lock                            short lock around every operation
//
// A TURN is one agent run: BeginTurn records the folder, EndTurn records it again and says what changed.
// A CHECKPOINT is the folder as a turn left it ("point de reprise"): Restore(checkpoint) undoes every later
// turn; UndoTurn(turn) undoes that turn and every later one ("Tout annuler" on the last turn).
//
// Undo puts back ONLY the files the undone turns changed, as they were before the earliest of them. A file the
// user changed after the turn is never overwritten silently: the call returns Status::Conflict with the files
// named and touches nothing; the UI then calls again with OnConflict::KeepMine or OnConflict::Overwrite.
//
// Threading: no UI, no globals, no background work. Every Store method does synchronous file I/O and BLOCKS
// the calling thread: call them off the UI thread. One Store serves one project at a time; its methods are
// serialized by an internal mutex. Several Store objects (threads or processes) may target the same project.
//
// Safety: never follows a symbolic link or junction inside the project, never writes outside the project
// folder, never touches a .git folder. Project files are replaced through a temporary file in the same folder
// + MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH); store files the same way. Contents are restored byte for
// byte (encoding, BOM and line endings are untouched).
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ChangeSnapshots {

// ---------------------------------------------------------------- contract constants
constexpr const char* ProjectSchema = "browser-chat-snapshots/1";
constexpr const char* TurnSchema = "browser-chat-snapshot-turn/1";
constexpr const char* RestoreSchema = "browser-chat-snapshot-restore/1";
constexpr const char* IndexHeader = "browser-chat-snapshot-index/1";
constexpr const wchar_t* StoreFolderName = L"chat-snapshots";  // under the browser's data root

constexpr std::size_t DefaultMaxTurns = 50;                           // turns WITH changes kept per project
constexpr std::uint64_t DefaultMaxStoreBytes = 500ull * 1024 * 1024;  // blobs of one project
constexpr std::uint64_t DefaultMaxFileBytes = 5ull * 1024 * 1024;     // bigger files are not copied
constexpr std::size_t DefaultMaxFiles = 100000;                       // more files than this: no snapshot
constexpr std::size_t DefaultMaxDiffBytes = 512 * 1024;               // Diff(): unified text, then truncated
constexpr unsigned DefaultLockTimeoutMs = 2000;

// Folder names never recorded, at any depth (ASCII case-insensitive): node_modules, .git, dist, build, .next,
// .astro, plus the usual tool caches (.turbo, .svelte-kit, .nuxt, .output, .vercel, .cache, .parcel-cache,
// .vite, .hg, .svn).
std::vector<std::string> DefaultExcludedDirectories();

// ---------------------------------------------------------------- options
struct Options {
    std::size_t maxTurns = DefaultMaxTurns;
    std::uint64_t maxStoreBytes = DefaultMaxStoreBytes;
    std::uint64_t maxFileBytes = DefaultMaxFileBytes;
    std::size_t maxFiles = DefaultMaxFiles;
    std::vector<std::string> excludedDirectories = DefaultExcludedDirectories();
    bool respectGitignore = true;  // .gitignore at the root and in sub-folders
    bool skipSecretFiles = true;   // .env, keys, credentials: listed when they change, never copied
    unsigned lockTimeoutMs = DefaultLockTimeoutMs;
    std::size_t hashThreads = 0;   // 0 = automatic (at most 8); only used when many files must be read
    // Tests only. Unix seconds for the records' dates.
    std::function<std::int64_t()> clock;
    // Tests only. Called at named points ("restore.journal", "restore.op", "restore.temp", "restore.applied",
    // "restore.records", "end.record", "end.index"); returning true stops the operation dead at that point, as a
    // crash would (Status::IoError, nothing cleaned up).
    std::function<bool(std::string_view point)> fault;
};

// ---------------------------------------------------------------- results
enum class Status {
    Ok,
    Conflict,         // files changed since the turn: NOTHING was touched; see Result::conflicts
    Partial,          // the restore ran, but some files could not be written; see Result::failed
    NoProject,        // no project bound: call Open or BeginTurn first
    InvalidProject,   // not an existing folder, a drive root, or a folder inside the store
    ProjectTooLarge,  // more than maxFiles files, or more than maxStoreBytes to copy: no snapshot, no turn
    TurnOpen,         // a turn is already running on this project (here, or in another window / process)
    NoTurnOpen,       // EndTurn without BeginTurn
    NotFound,         // unknown turn, checkpoint or file
    AlreadyUndone,    // the turn was undone before
    Busy,             // another operation holds the project's store (waited lockTimeoutMs)
    Corrupt,          // a record of the store is unreadable; nothing was touched
    IoError,          // the store or the project could not be read / written; detail says what
};
const char* StatusName(Status status);  // "ok", "conflict", ... (stable, for logs and for the panel)

enum class ChangeKind { Created, Modified, Deleted };
const char* ChangeKindName(ChangeKind kind);  // "created" | "modified" | "deleted"

// Why a file's content is not kept. Such a file is still LISTED when it changes, but undo leaves it as it is.
enum class NotKept {
    No,
    TooLarge,    // over maxFileBytes
    Secret,      // may hold secrets (.env, *.pem, id_rsa, .npmrc ...)
    Unreadable,  // could not be opened when the folder was recorded (locked by another program)
};
const char* NotKeptName(NotKept reason);  // "" | "too-large" | "secret" | "unreadable"

struct FileChange {
    std::string path;  // UTF-8, relative to the project folder, '/' separators
    ChangeKind kind = ChangeKind::Modified;
    bool binary = false;      // not text: additions/deletions are 0
    int additions = 0;        // lines
    int deletions = 0;        // lines
    bool restorable = true;   // false: the content before the turn was not kept (see notKept)
    NotKept notKept = NotKept::No;
    std::string renamedFrom;  // Created with exactly the content of a file this turn deleted (else empty)
    std::string renamedTo;    // Deleted, and its exact content was created elsewhere by this turn (else empty)
};

struct TurnSummary {
    std::string turnId;        // "trn_" + 26 chars
    std::string checkpointId;  // "chk_" + 26 chars: the folder as this turn left it
    std::uint64_t sequence = 0;  // 1, 2, 3... per project, never reused
    std::int64_t startedAt = 0;  // Unix seconds
    std::int64_t endedAt = 0;
    // Closed by recovery (the browser stopped during the turn): its changes are everything that differed when
    // the project was next opened, so they may include the user's own edits. Warn before undoing it.
    bool interrupted = false;
    bool undone = false;
    std::vector<FileChange> changes;  // sorted by path
    int additions = 0;                // totals over changes
    int deletions = 0;
};

// What recording the folder cost (BeginTurn / EndTurn), for logs and for the performance report.
struct ScanStats {
    std::size_t files = 0;            // files recorded (content kept)
    std::size_t directories = 0;
    std::size_t notKept = 0;          // too large / secret / unreadable: listed, not copied
    std::size_t skippedLinks = 0;     // symbolic links, junctions, cloud placeholders (not followed), unusable names
    std::size_t read = 0;             // files whose content had to be read (new, or size/date changed)
    std::size_t blobsWritten = 0;
    std::uint64_t bytesWritten = 0;
    std::uint64_t trackedBytes = 0;   // total size of the files recorded
    double milliseconds = 0;
};

enum class ConflictReason {
    ModifiedSinceTurn,   // the file is not as the turn left it
    DeletedSinceTurn,    // the turn left a file there; it is gone
    CreatedSinceTurn,    // the turn left nothing there; a file is there now
    EditedBetweenTurns,  // changed between two of the turns being undone, and a later turn changed it again
    InUse,               // read-only, or held open by another program: it cannot be replaced
    Blocked,             // a folder stands where the file must go (or a file where a folder must go)
    SnapshotLost,        // the content to put back is missing or damaged in the store
};
const char* ConflictReasonName(ConflictReason reason);  // "modified-since-turn", ...

struct Conflict {
    std::string path;
    ConflictReason reason = ConflictReason::ModifiedSinceTurn;
};

// What an undo does with the files in conflict.
enum class OnConflict {
    Refuse,     // default: any conflict => Status::Conflict, nothing touched
    KeepMine,   // undo the other files; the files in conflict stay exactly as they are
    Overwrite,  // put the old content back even over the user's changes (the user's explicit choice)
};

struct Result {
    Status status = Status::Ok;
    std::string detail;                    // technical, English, for logs (the UI writes its own sentences)
    std::vector<Conflict> conflicts;       // Conflict: why nothing was done. Otherwise: the files left alone.
    std::vector<std::string> restored;     // files put back or removed
    std::vector<std::string> skipped;      // left as they are (KeepMine conflicts, content not kept)
    std::vector<std::string> failed;       // Partial: could not be written
    std::vector<std::string> undoneTurns;  // turn ids now marked undone, oldest first
    bool resumedRestore = false;           // an interrupted restore was completed first (see Open)
    bool ok() const { return status == Status::Ok; }
    explicit operator bool() const { return ok(); }
};

struct FileDiff {
    std::string path;
    ChangeKind kind = ChangeKind::Modified;
    bool binary = false;
    bool restorable = true;
    NotKept notKept = NotKept::No;
    int additions = 0;
    int deletions = 0;
    // Unified diff, UTF-8, "\n" line ends, 3 lines of context:
    //   --- a/<path>   (or /dev/null)        +++ b/<path>   (or /dev/null)        @@ -l,n +l,n @@
    // Empty when binary or when a side was not kept. Lines are shown without their line ending.
    std::string unified;
    bool truncated = false;           // cut at maxBytes (at a line boundary)
    std::string encodingBefore;       // "utf-8" | "utf-8-bom" | "utf-16le" | "utf-16be" | "windows-1252" | "binary" | ""
    std::string encodingAfter;
    bool lineEndingsChanged = false;  // CRLF <-> LF between before and after
};

struct Usage {
    std::size_t turns = 0;        // records kept (undone ones included)
    std::size_t blobs = 0;
    std::uint64_t blobBytes = 0;
};

// ---------------------------------------------------------------- pure helpers (no I/O)
// Folder name of a project inside the store: readable part + hash of its full path ("mon-site-1f2e3d4c5b6a7988").
// `canonicalProjectDir` is the full path as Store::ProjectDir() returns it; the key is case-insensitive.
std::string ProjectKey(std::wstring_view canonicalProjectDir);

// Line diff of two file contents (any bytes). Text is UTF-8 (with or without BOM), UTF-16 with a BOM, or, when
// the bytes are not valid UTF-8, read as Windows-1252; anything with a NUL byte is binary. Lines are compared
// WITH their line ending, so CRLF -> LF counts as a change. `before` / `after` absent => created / deleted.
// The diff is exact up to 2 048 changed lines (and a work budget); beyond, the changed region is shown as
// "everything removed, everything added" (still a correct diff, with larger counts).
FileDiff DiffContents(std::string_view path, const std::string* before, const std::string* after,
                      bool wantUnified = true, std::size_t maxBytes = DefaultMaxDiffBytes);

// Whether `path` (relative, '/' separators) is ignored by ONE .gitignore sitting at the root. Supported: blank
// lines, # comments, ! negation, trailing / (folders only), leading or inner / (anchored), *, ?, [a-z], **/,
// /**, /**/, \ escapes. Matching is ASCII case-insensitive (git on Windows). A file under an ignored folder is
// ignored (it cannot be re-included).
bool GitignoreIgnores(std::string_view gitignoreText, std::string_view path, bool isDirectory);

// Whether a file name (no folder) is treated as possibly holding secrets (Options::skipSecretFiles).
bool MayHoldSecrets(std::string_view fileName);

// ---------------------------------------------------------------- the store
class Store {
public:
    // `storeRoot` = <data root>\chat-snapshots (created on first use). Does no I/O.
    explicit Store(std::wstring storeRoot, Options options = Options());
    ~Store();  // a turn still open is left as "interrupted": the next BeginTurn on the project closes it
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    // Binds the store to a project folder without starting a turn (e.g. to undo after a restart). Completes a
    // restore that a crash interrupted (Result::resumedRestore; files changed since then are left alone and
    // named in Result::conflicts) and closes a turn a crash left open. BeginTurn does the same.
    Result Open(const std::wstring& projectDir);

    // Records the folder before the agent runs. First time on a project: copies every kept file (seconds on
    // a large project). Afterwards: only the files whose size or date changed are read.
    // Errors: InvalidProject, ProjectTooLarge (no turn started: run the agent without undo), TurnOpen, Busy.
    Result BeginTurn(const std::wstring& projectDir, std::string* turnId = nullptr, ScanStats* stats = nullptr);

    // Records the folder after the agent ran and says what the turn changed. Always call it, even when the turn
    // failed or was cancelled. A turn without changes is recorded too (it has a checkpoint; it costs nothing).
    Result EndTurn(TurnSummary* summary = nullptr, ScanStats* stats = nullptr);

    // Undoes `turnId` and every later turn still applied: the files they changed go back to how they were
    // before `turnId`. Refused while a turn is running (TurnOpen).
    Result UndoTurn(const std::string& turnId, OnConflict onConflict = OnConflict::Refuse);

    // Back to the folder as the turn of `checkpointId` left it: undoes every later turn still applied. Ok with
    // nothing to do when it is already the latest state.
    Result Restore(const std::string& checkpointId, OnConflict onConflict = OnConflict::Refuse);

    // What the turn of `checkpointId` did to `file` (path as in FileChange::path), as a unified diff.
    Result Diff(const std::string& file, const std::string& checkpointId, FileDiff* diff,
                std::size_t maxBytes = DefaultMaxDiffBytes);

    // Turns recorded for the bound project, oldest first (undone ones included, flagged).
    Result Turns(std::vector<TurnSummary>* turns);

    // Applies maxTurns / maxStoreBytes now and deletes the contents nothing refers to any more. EndTurn does
    // it too. The latest turn with changes is always kept.
    Result Purge(Usage* after = nullptr);
    Result GetUsage(Usage* usage);

    // Deletes every snapshot of the bound project (records and contents). The project's files are untouched.
    Result Forget();

    bool HasProject() const;
    bool TurnIsOpen() const;                 // a turn begun by THIS object is running
    const std::wstring& ProjectDir() const;  // canonical full path of the bound project ("" when none)
    std::wstring ProjectStoreDir() const;    // <storeRoot>\<projectKey> ("" when none)
    const Options& GetOptions() const;
    void SetLimits(std::size_t maxTurns, std::uint64_t maxStoreBytes);  // settings changed; applies at next purge

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ChangeSnapshots
