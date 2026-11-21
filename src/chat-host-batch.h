#pragma once
// Chat intégré — « Traiter les annotations » on the host side (contract B 1.4 §15, Paper 186K-0).
// Included at the end of chat-host.h (after chat-host-ui.h).
//
//   PoseBatch          the site's pending annotations (the store, oldest first, 50 at most) -> `annotations.batch`:
//                      the block « N annotations à traiter » in the composer, every line ticked.
//   StartBatchTurn     `send` with `annotations {batchId, ids}`: the ticked ones become a turn. The user's message
//                      carries them (`message.user.annotations`: the card « Traiter N annotations ») and the exact
//                      instruction the agent gets (ChatBatch::Instruction: « Voir la consigne »).
//   BatchToolDone      the agent's own calls of our annotation tools move the card's lines (`annotations.status`):
//                      claim -> working, resolve -> done + its summary, release -> skipped + its reason. Those calls
//                      are allowed without a card (chat-host-turn.h, OnPermission).
//   ReconcileBatch     the store says the same thing by another road (the bridge writes it): read after each call and
//                      at the end of the turn, where what is left is said plainly — an annotation still held is given
//                      back (it would stay « en cours » for 30 minutes), one never touched stays waiting.
//   ShowAnnotation     `annotation.show`: a frame on the element in the page, for a moment.
// The statuses live in the conversation (the user's message), so a reopened conversation shows the card as it ended.
namespace Chat {

SJ::Value BatchItemJson(const BatchItem& i, bool withStatus) {
    SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(i.id)); o.Set("note", Str(i.note));
    if (!i.element.empty()) o.Set("element", Str(i.element));
    if (!i.source.empty()) o.Set("source", Str(i.source));
    if (!i.thumb.empty()) o.Set("thumb", Str(i.thumb));
    if (withStatus) { o.Set("status", Str(i.status)); if (!i.text.empty()) o.Set(i.status == "done" ? "summary" : "reason", Str(i.text)); }
    return o;
}
void SendComposerBatch() {
    SJ::Value m = SJ::Value::Obj(); m.Set("batchId", Str(composerBatch.id.empty() ? std::string("b_none") : composerBatch.id)); m.Set("siteLabel", Str(site.label));
    SJ::Value items = SJ::Value::Arr(); for (const auto& i : composerBatch.items) items.Push(BatchItemJson(i, false));
    m.Set("items", std::move(items)); Send("annotations.batch", std::move(m));
}
// Off the UI thread: the pending annotations of a site as lines of the block (note, element, file:line, thumbnail).
std::vector<BatchItem> ReadPending(const std::string& origin, const std::string& projectDir) {
    std::vector<BatchItem> out;
    BA::ListQuery q; q.site = origin; q.status = BA::Status::Open; q.limit = 50;
    for (const auto& e : Ann::Store().List(q).entries) {
        BatchItem i; const auto& a = e.annotation; const auto& t = a.target;
        i.id = a.id; i.siteKey = a.siteKey; i.note = ClipUtf8(a.note, 2000); i.element = ClipUtf8(t.path.empty() ? t.tag : t.path.back(), 120);
        ChatPrompt::PageSource ps;   // the element's own Astro attributes, as the annotation kept them
        for (const auto& s : t.sourceAttributes) { if (s.name == "data-astro-source-file") ps.astroFile = s.value; else if (s.name == "data-astro-source-loc") ps.astroLoc = s.value; }
        if (!ps.astroFile.empty() && !projectDir.empty()) {
            const auto ref = ChatPrompt::ResolveSource(ps, projectDir, [](const std::string& abs) { const DWORD at = GetFileAttributesW(W(abs).c_str()); return at != INVALID_FILE_ATTRIBUTES && !(at & FILE_ATTRIBUTE_DIRECTORY); });
            if (ref.known) i.source = ref.Display();
        }
        if (a.screenshot.present) { std::string png; if (Ann::Store().ReadShot(a.siteKey, a.id, png).ok() && !png.empty()) i.thumb = ThumbDataUrl(png, 132, 96); }
        out.push_back(std::move(i));
    }
    return out;
}
void PoseBatch(const wchar_t* from) {
    if (!site.local || site.file || site.origin.empty() || !panelReady) return;
    auto out = std::make_shared<std::vector<BatchItem>>(); const std::string origin = site.origin, dir = U8(folder); const std::wstring where = from;
    Ann::Run([out, origin, dir] { *out = ReadPending(origin, dir); },
             [out, origin, where] {
                 if (origin != site.origin) return;
                 composerBatch = Batch{}; composerBatch.items = std::move(*out);
                 if (int(composerBatch.items.size()) != pendingAnnotations) { pendingAnnotations = int(composerBatch.items.size()); SendState(); }
                 if (composerBatch.items.empty()) { SendComposerBatch(); Strip("batch", "neutral", TR(chat_batch_none)); Diag(L"batch asked (" + where + L"): no pending annotation on this site"); return; }
                 composerBatch.id = NewId("b_"); ClearStrip("batch"); SendComposerBatch();
                 std::wstring ids; for (const auto& i : composerBatch.items) ids += L" " + W(i.id);
                 Diag(L"batch " + W(composerBatch.id) + L" posed (" + where + L"): " + std::to_wstring(composerBatch.items.size()) + L" annotation(s):" + ids);
                 if (Open()) { SJ::Value f = SJ::Value::Obj(); f.Set("target", Str(std::string("composer"))); Send("focus", std::move(f)); view.Focus(); }
             });
}
// « Traiter avec l'agent » from outside the panel (the native history): the chat opens, then the batch is posed.
void ProcessAnnotations() {
    batchOwed = true;
    if (!Open()) OpenPanel();
    if (Open() && panelReady) { batchOwed = false; PoseBatch(L"asked from the browser"); }   // i18n-exempt: log detail
}

void SendBatchStatus(const BatchItem& i) {
    if (convo.data.head.id != activeBatch.conversation) return;   // another conversation is shown: its thread has no such card
    SJ::Value m = SJ::Value::Obj(); m.Set("batchId", Str(activeBatch.id)); m.Set("id", Str(i.id)); m.Set("status", Str(i.status));
    if (!i.text.empty()) m.Set(i.status == "done" ? "summary" : "reason", Str(i.text));
    Send("annotations.status", std::move(m));
}
// The card lives in the user's message of the conversation: kept in step, so a reopened conversation shows it as it is.
void StoreBatch() {
    if (convo.data.head.id != activeBatch.conversation || activeBatch.item >= convo.data.items.size()) return;
    auto& item = convo.data.items[activeBatch.item]; const auto* id = item.Get("id");
    if (!id || !id->IsString() || id->string != activeBatch.userId) return;
    SJ::Value an = SJ::Value::Obj(); an.Set("batchId", Str(activeBatch.id));
    SJ::Value items = SJ::Value::Arr(); for (const auto& i : activeBatch.items) items.Push(BatchItemJson(i, true));
    an.Set("items", std::move(items)); an.Set("instruction", Str(activeBatch.instruction)); item.Set("annotations", std::move(an));
}
void SetBatchStatus(const std::string& annotation, const ChatBatch::Change& c, const wchar_t* from) {
    if (!c.Any() || annotation.empty()) return;
    for (auto& i : activeBatch.items) {
        if (i.id != annotation) continue;
        if (!ChatBatch::Advances(i.status, c.status)) return;   // a line only moves forward
        i.status = c.status; i.text = ClipUtf8(ChatBatch::OneLine(c.text, 600), 700);
        if (c.status == "working" && std::wstring(from) == L"tool") i.claimedHere = true;
        SendBatchStatus(i); StoreBatch();
        Diag(L"batch status " + W(i.id) + L": " + W(i.status) + L" (" + from + L")" + (i.text.empty() ? L"" : L" — " + W(ClipUtf8(i.text, 120))));
        return;
    }
}
// `stop`: empty during the turn; the turn's stop reason at its end (what is left is then said, and given back).
void ReconcileBatch(const std::string& stop) {
    if (activeBatch.items.empty()) return;
    struct Row { std::string id, siteKey; ChatBatch::StoreFacts facts; bool found = false, released = false, ours = false; };
    auto rows = std::make_shared<std::vector<Row>>(); for (const auto& i : activeBatch.items) { Row r; r.id = i.id; r.siteKey = i.siteKey; r.ours = i.claimedHere; rows->push_back(std::move(r)); }
    const std::string batch = activeBatch.id, origin = site.origin; const std::int64_t since = activeBatch.sentAt; const bool final = !stop.empty();
    Ann::Run([rows, origin, since, final] {
            for (auto& r : *rows) {
                BA::Entry e; if (!Ann::Store().Find(r.id, e, origin).ok()) continue;
                r.found = true; r.facts.status = BA::StatusName(e.state.status); r.facts.updatedAt = e.state.updatedAt;
                if (e.state.resolution) r.facts.summary = e.state.resolution->summary;
                if (!e.state.events.empty()) { const auto& last = e.state.events.back(); r.facts.lastTo = last.to; r.facts.lastSummary = last.summary; r.facts.lastAt = last.at; }
                // the turn is over and ITS agent still holds it (its own claim, during this batch): given back now.
                // A claim by anyone else (another agent connected to the annotations) is never touched.
                if (final && r.ours && e.state.status == BA::Status::Claimed && e.state.updatedAt >= since) { std::lock_guard<std::mutex> g(Ann::writeLock); r.released = Ann::Store().ForceRelease(e.annotation.siteKey, r.id).ok(); }
            }
        },
        [rows, batch, since, final, stop] {
            if (activeBatch.id != batch) return;
            for (const auto& r : *rows) if (r.found && !r.released) SetBatchStatus(r.id, ChatBatch::FromStore(r.facts, since), L"store");
            if (final) {
                for (const auto& i : std::vector<BatchItem>(activeBatch.items)) {
                    // i18n-exempt-begin: log details
                    if (i.status == "working") SetBatchStatus(i.id, {"skipped", T8(i.claimedHere ? I18n::Id::chat_batch_left_working : I18n::Id::chat_batch_left_elsewhere)}, L"turn over");
                    else if (i.status == "pending") SetBatchStatus(i.id, {"skipped", stop == "cancelled" ? T8(I18n::Id::chat_batch_left_stopped) : U8(TRF(chat_batch_left_pending, AgentShort(agentId)))}, L"turn over");
                    // i18n-exempt-end
                }
                int done = 0, skipped = 0; for (const auto& i : activeBatch.items) (i.status == "done" ? done : skipped)++;
                Diag(L"batch " + W(batch) + L" over: " + std::to_wstring(done) + L" done, " + std::to_wstring(skipped) + L" not handled");
                SaveConvo();
            }
            RefreshPending(); Ann::RefreshStatus(); if (Ann::launcher.HistoryVisible()) Ann::LoadHistory(false, false);
        });
}
void BatchToolDone(ChatTools::Ours tool, const std::string& annotation, const std::string& rawInput) {
    if (!turn.active || !turn.batch) return;
    SetBatchStatus(annotation, ChatBatch::FromTool(tool, rawInput), L"tool");
    ReconcileBatch("");
}
void BatchTurnEnded(const std::string& stopReason) { ReconcileBatch(stopReason.empty() ? std::string("end_turn") : stopReason); }

// `ids`: the ticked annotations of the batch posed in the composer; `precision`: what the user added (may be empty).
void StartBatchTurn(const std::string& precision, const std::string& batchId, const std::vector<std::string>& ids) {
    TakeShownAside();   // C2
    if (turn.active || !Usable()) return;
    if (batchId != composerBatch.id || composerBatch.items.empty()) { Diag(L"send with a batch that is not the one in the composer: ignored"); return; }
    Batch b; b.id = composerBatch.id;
    for (const auto& id : ids) for (const auto& i : composerBatch.items) if (i.id == id && std::none_of(b.items.begin(), b.items.end(), [&](const BatchItem& x) { return x.id == id; })) b.items.push_back(i);
    if (b.items.empty()) { Diag(L"send with a batch and no ticked annotation: ignored"); return; }
    std::vector<ChatBatch::Line> lines; for (const auto& i : b.items) { lines.push_back({i.id, i.note}); annotationNotes[i.id] = ClipUtf8(i.note, 400); }
    b.instruction = ChatBatch::Instruction(T8(I18n::Id::chat_batch_instruction), lines, T8(I18n::Id::chat_batch_precision), precision);
    b.sentAt = BA::SystemNow();
    Diag(L"batch " + W(b.id) + L" sent: " + std::to_wstring(b.items.size()) + L" of " + std::to_wstring(composerBatch.items.size()) + L" annotation(s)");
    b.conversation = convo.data.head.id;
    activeBatch = std::move(b); composerBatch = Batch{};
    StartTurn(precision, {}, {}, true);
}
// A frame on the annotation's element, for a moment (the page's own script: found by its selector, or nothing).
void ShowAnnotation(const std::string& id) {
    if (id.empty()) return;
    struct Out { bool ok = false, unique = false; std::string selector; };
    auto out = std::make_shared<Out>(); const std::string origin = site.origin;
    Ann::Run([out, id, origin] { BA::Entry e; if (Ann::Store().Find(id, e, origin).ok()) { out->ok = true; out->selector = e.annotation.target.selector; out->unique = e.annotation.target.selectorUnique && e.annotation.target.shadowPath.empty(); } },
             [out, id] {
                 if (!out->ok || !out->unique || out->selector.empty()) { Diag(L"annotation " + W(id) + L" cannot be shown on the page (no unique selector)"); return; }
                 ApplyMark("show", out->selector);
             });
}

} // namespace Chat
