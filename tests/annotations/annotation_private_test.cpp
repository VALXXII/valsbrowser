// A private window's annotations are erased when it closes (#125, src/annotation-private.h); a private window that did
// not close normally leaves them to the next start; a normal window's annotations are never touched. Temp roots only
// (build\annotations-test\tmp), never a real profile.
// Usage: annotation_private_test.exe   (run from the repository root)
#include "annotations_test_util.h"
#include "../../src/annotation-private.h"

using namespace AnnTest;
namespace BA = BrowserAnnotations; namespace AP = AnnotationPrivate;

// What the host does for a private window's note: its line first, then the note (src/annotation-mode.h RecordPrivate).
static BA::Annotation PrivateNote(const BA::Store& store, AP::Ledger& ledger, const std::wstring& dir, const std::string& site, const std::string& text) {
    BA::Annotation a = Sample(site, text); a.id = BA::NewAnnotationId();
    CHECK(ledger.Open(dir));
    CHECK(ledger.Add(BA::SiteKey(BA::SiteOrigin(a.site.empty() ? BA::RedactUrl(a.source.url) : a.site)), a.id));
    const std::string png = TinyPng();
    CHECK(store.WriteAnnotation(a, &png, false).ok());
    return a;
}
static bool Present(const BA::Store& store, const BA::Annotation& a) { BA::Entry e; return store.Read(a.siteKey, a.id, e).ok(); }

int main() {
    // 1. A private window closes: its notes go, a normal window's note on the same site stays.
    {
        const std::wstring root = TempDir(L"private-close"); BA::Store store(root + L"\\annotations"); const std::wstring dir = root + L"\\annotations-private";
        BA::Annotation normal = Sample("http://localhost:5173", "Note d'une fenêtre normale"); const std::string png = TinyPng();
        CHECK(store.WriteAnnotation(normal, &png, false).ok());
        AP::Ledger ledger;
        const auto p1 = PrivateNote(store, ledger, dir, "http://localhost:5173", "Note privée 1");
        const auto p2 = PrivateNote(store, ledger, dir, "file:///C:/projet/index.html", "Note privée sur un fichier");
        CHECK(Present(store, p1) && Present(store, p2) && ledger.Entries().size() == 2);
        const std::wstring path = ledger.Path();
        // While the window is open another start leaves its notes alone (its ledger is held).
        const AP::Report live = AP::CleanOrphans(dir, store);
        CHECK(live.ledgers == 0 && Present(store, p1) && Exists(path));
        const AP::Report r = ledger.Erase(store);
        CHECK(r.deleted == 2 && r.failed == 0 && r.complete);
        CHECK(!Present(store, p1) && !Present(store, p2));
        CHECK(!Exists(store.ShotPath(p1.siteKey, p1.id)));
        CHECK(Present(store, normal));                     // the normal window's note survives
        CHECK(!Exists(path));                              // nothing left behind: no ledger
        std::printf("ok private window closed: its 2 notes erased, the normal note kept\n");
    }
    // 2. A private window that crashed: the next start erases its notes (and only its).
    {
        const std::wstring root = TempDir(L"private-crash"); BA::Store store(root + L"\\annotations"); const std::wstring dir = root + L"\\annotations-private";
        BA::Annotation normal = Sample("boutique", "Normale"); CHECK(store.WriteAnnotation(normal, nullptr, false).ok());
        BA::Annotation p; std::wstring path;
        { AP::Ledger crashed; p = PrivateNote(store, crashed, dir, "http://127.0.0.1:8080", "Privée, fenêtre plantée"); path = crashed.Path(); crashed.Close(); }   // the process is gone: its handle closed
        CHECK(Present(store, p) && Exists(path));
        AP::Ledger running; const auto q = PrivateNote(store, running, dir, "http://localhost:3000", "Privée, fenêtre encore ouverte");
        const AP::Report r = AP::CleanOrphans(dir, store);
        CHECK(r.ledgers == 1 && r.deleted == 1 && r.complete);
        CHECK(!Present(store, p) && !Exists(path));
        CHECK(Present(store, q) && Exists(running.Path()));   // another private window still open: untouched
        CHECK(Present(store, normal));
        running.Erase(store); CHECK(!Present(store, q));
        AP::CleanOrphans(dir, store); CHECK(!Exists(dir));    // the empty folder goes too
        std::printf("ok crashed private window: erased at the next start, a running one and the normal note untouched\n");
    }
    // 3. Copies in the trash and a line whose note was never written (crash between the two).
    {
        const std::wstring root = TempDir(L"private-trash"); BA::Store store(root + L"\\annotations"); const std::wstring dir = root + L"\\annotations-private";
        AP::Ledger ledger; const auto t = PrivateNote(store, ledger, dir, "http://localhost:5173", "Mise à la corbeille");
        CHECK(store.Trash(t.siteKey, t.id).ok() && !Present(store, t));
        CHECK(ledger.Add(BA::SiteKey("http://localhost:5173"), BA::NewAnnotationId()));   // never written
        const AP::Report r = ledger.Erase(store);
        CHECK(r.complete && r.failed == 0 && r.missing == 1);
        CHECK(!Exists(store.Root() + L"\\trash\\" + std::wstring(t.siteKey.begin(), t.siteKey.end()) + L"\\" + std::wstring(t.id.begin(), t.id.end())));
        std::printf("ok trash copy erased, a line without its note is harmless\n");
    }
    // 4. A note still on its way when the window closes: the ledger stays, the next start finishes the job.
    {
        const std::wstring root = TempDir(L"private-late"); BA::Store store(root + L"\\annotations"); const std::wstring dir = root + L"\\annotations-private";
        AP::Ledger ledger; BA::Annotation late = Sample("http://localhost:5173", "Écrite après la fermeture"); late.id = BA::NewAnnotationId();
        CHECK(ledger.Open(dir) && ledger.Add(BA::SiteKey("http://localhost:5173"), late.id));
        const std::wstring path = ledger.Path();
        const AP::Report r = ledger.Erase(store, true);
        CHECK(!r.complete && Exists(path));
        CHECK(store.WriteAnnotation(late, nullptr, false).ok() && Present(store, late));   // the write lands after the close
        const AP::Report next = AP::CleanOrphans(dir, store);
        CHECK(next.deleted == 1 && !Present(store, late) && !Exists(path));
        std::printf("ok a late note is erased at the next start\n");
    }
    // 5. No private window ever annotated: nothing to do, nothing created.
    {
        const std::wstring root = TempDir(L"private-none"); BA::Store store(root + L"\\annotations");
        const AP::Report r = AP::CleanOrphans(root + L"\\annotations-private", store);
        CHECK(r.ledgers == 0 && !Exists(root + L"\\annotations-private"));
    }
    // Lot R (decision A, O12): the chat works in a private window. What it keeps there (conversations, site memory,
    // the copies of the project's files taken before a turn) lives in <random>.data beside the ledger.
    auto fill = [](const std::wstring& data) {
        CreateDirectoryW((data + L"\\annotations-chat").c_str(), nullptr); CreateDirectoryW((data + L"\\annotations-chat\\conversations").c_str(), nullptr);
        CreateDirectoryW((data + L"\\snapshots").c_str(), nullptr); CreateDirectoryW((data + L"\\snapshots\\t1").c_str(), nullptr);
        const bool ok = WriteText(data + L"\\annotations-chat\\conversations\\c_1.json", "{\"head\":{\"title\":\"Conversation privée\"}}") && WriteText(data + L"\\annotations-chat\\projects.json", "{}")
                        && WriteText(data + L"\\snapshots\\t1\\index.html", "<h1>copie</h1>");
        SetFileAttributesW((data + L"\\snapshots\\t1\\index.html").c_str(), FILE_ATTRIBUTE_READONLY);   // a read-only copy must go too
        return ok; };
    // 6. The private window closes: its chat folder goes with its ledger; a link inside it is removed, never followed.
    {
        const std::wstring root = TempDir(L"private-chat-close"); BA::Store store(root + L"\\annotations"); const std::wstring dir = root + L"\\annotations-private";
        const std::wstring outside = root + L"\\projet-utilisateur"; CreateDirectoryW(outside.c_str(), nullptr); CHECK(WriteText(outside + L"\\index.html", "<h1>le vrai fichier</h1>"));
        AP::Ledger ledger; CHECK(ledger.DataDir().empty());                 // no ledger yet: no folder
        CHECK(ledger.Open(dir));
        const std::wstring data = ledger.DataDir(), path = ledger.Path();
        CHECK(!data.empty() && Exists(data) && data == path.substr(0, path.size() - 7) + L".data" && data.rfind(dir + L"\\", 0) == 0);
        CHECK(ledger.DataDir() == data);                                    // the same folder at every call
        CHECK(fill(data));
        const bool linked = CreateJunction(data + L"\\lien", outside);       // nothing of the kind is made by the chat: the delete must still be safe
        if (!linked) std::printf("note: junction unavailable here, link case skipped\n");
        const auto note = PrivateNote(store, ledger, dir, "http://localhost:5173", "Élément envoyé dans le chat privé");
        const AP::Report live = AP::CleanOrphans(dir, store);               // another start while the window is open: nothing touched
        CHECK(live.ledgers == 0 && Exists(data + L"\\annotations-chat\\conversations\\c_1.json") && Present(store, note));
        const AP::Report r = ledger.Erase(store);
        CHECK(r.complete && r.failed == 0 && r.deleted == 1);
        CHECK(!Exists(data) && !Exists(path) && !Present(store, note));
        CHECK(Exists(outside + L"\\index.html") && ReadText(outside + L"\\index.html") == "<h1>le vrai fichier</h1>");   // the link's target is intact
        AP::CleanOrphans(dir, store); CHECK(!Exists(dir));
        std::printf("ok private window closed: its chat folder (conversations, snapshots) erased, a linked folder left intact\n");
    }
    // 7. A private window that crashed (or was killed): the next start erases its chat folder, not a running window's.
    {
        const std::wstring root = TempDir(L"private-chat-crash"); BA::Store store(root + L"\\annotations"); const std::wstring dir = root + L"\\annotations-private";
        std::wstring data, path;
        { AP::Ledger crashed; CHECK(crashed.Open(dir)); data = crashed.DataDir(); path = crashed.Path(); CHECK(fill(data)); crashed.Close(); }   // the process is gone: nothing erased
        CHECK(Exists(data + L"\\annotations-chat\\conversations\\c_1.json") && Exists(path));
        AP::Ledger running; CHECK(running.Open(dir)); const std::wstring live = running.DataDir(); CHECK(fill(live) && live != data);
        const AP::Report r = AP::CleanOrphans(dir, store);
        CHECK(r.ledgers == 1 && r.complete && r.failed == 0);
        CHECK(!Exists(data) && !Exists(path));
        CHECK(Exists(live + L"\\annotations-chat\\conversations\\c_1.json") && Exists(running.Path()));   // the window still open keeps its conversations
        CHECK(running.Erase(store).complete && !Exists(live));
        std::printf("ok crashed private window: its chat folder erased at the next start, a running window's kept\n");
    }
    // 8. A write still running when the window closes: the folder and the ledger stay, the next start finishes the job.
    {
        const std::wstring root = TempDir(L"private-chat-late"); BA::Store store(root + L"\\annotations"); const std::wstring dir = root + L"\\annotations-private";
        AP::Ledger ledger; CHECK(ledger.Open(dir)); const std::wstring data = ledger.DataDir(), path = ledger.Path(); CHECK(fill(data));
        const AP::Report r = ledger.Erase(store, true);
        CHECK(!r.complete && Exists(path) && Exists(data));
        CHECK(WriteText(data + L"\\annotations-chat\\conversations\\c_2.json", "{}"));   // the late write lands
        const AP::Report next = AP::CleanOrphans(dir, store);
        CHECK(next.ledgers == 1 && next.complete && !Exists(data) && !Exists(path) && !Exists(dir));
        std::printf("ok a conversation saved late is erased at the next start\n");
    }
    std::printf("%d checks, %d failures\n", Checks(), Failures());
    return Failures() ? 1 : 0;
}
