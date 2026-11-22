#pragma once
// Chat intégré — « Traiter les annotations » (design/chat-integration.md; contract B 1.4 §15; Paper 186K-0).
// Pure C++17 (no Win32, no I/O, no interface text): what a line of the card « Traiter N annotations » becomes when the
// agent uses one of our annotation tools, or when the annotation store says so, and the instruction the agent gets.
//
//   pending  En attente            nothing happened yet
//   working  Claude s'en occupe    browser_annotation_claim succeeded
//   done     Terminée + résumé     browser_annotation_resolve, outcome « resolved »
//   skipped  n'a pas pu la traiter browser_annotation_release (with its reason), or resolve with another outcome
//
// A line only moves forward (pending -> working -> done | skipped): a late or repeated event never takes it back.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "chat-tools.h"

namespace ChatBatch {

struct Change {
    std::string status, text;   // text: the agent's one-sentence summary (done) or its reason (skipped)
    bool Any() const { return !status.empty(); }
};
inline int Rank(std::string_view status) { return status == "working" ? 1 : (status == "done" || status == "skipped") ? 2 : 0; }
inline bool Advances(std::string_view from, std::string_view to) { return Rank(to) > Rank(from); }

// What a COMPLETED call of one of our tools means for the annotation it names (list and get change nothing).
inline Change FromTool(ChatTools::Ours tool, std::string_view rawInputJson) {
    switch (tool) {
    case ChatTools::Ours::Claim: return {"working", ""};
    case ChatTools::Ours::Resolve: {
        const std::string outcome = ChatTools::TextArg(rawInputJson, "outcome", 40);
        return {outcome.empty() || outcome == "resolved" ? "done" : "skipped", ChatTools::TextArg(rawInputJson, "summary", 2000)}; }
    case ChatTools::Ours::Release: return {"skipped", ChatTools::TextArg(rawInputJson, "reason", 600)};
    default: return {};
    }
}

// What the annotation store says (the bridge writes it when the agent's call succeeds): the same answer by another
// road, for an event that was missed. `since`: when the batch was sent (store seconds); older facts say nothing.
struct StoreFacts {
    std::string status;                       // "open" | "claimed" | "resolved" | "needs_info" | "dismissed"
    std::string summary;                      // the resolution's summary
    std::int64_t updatedAt = 0;
    std::string lastTo, lastSummary;          // the last event: where it led, and its text (a release keeps its reason there)
    std::int64_t lastAt = 0;
};
inline Change FromStore(const StoreFacts& f, std::int64_t since) {
    if (f.updatedAt < since) return {};
    if (f.status == "claimed") return {"working", ""};
    if (f.status == "resolved") return {"done", f.summary};
    if (f.status == "needs_info" || f.status == "dismissed") return {"skipped", f.summary};
    if (f.status == "open" && f.lastAt >= since && f.lastTo == "open" && !f.lastSummary.empty()) return {"skipped", f.lastSummary};   // given back, with its reason
    return {};
}

// The instruction sent to the agent, shown as it is under « Voir la consigne envoyée à … ».
//   lead        the catalog's sentence (what to do with each annotation, in the user's language)
//   lines       the annotations: id and note (one line each; a note is the user's own words, quoted as data)
//   precision   what the user added (optional), under its catalog label
struct Line { std::string id, note; };
inline std::string OneLine(std::string_view text, std::size_t max) {
    std::string out; bool space = false;
    for (char c : text) {
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') { space = !out.empty(); continue; }
        if (static_cast<unsigned char>(c) < 0x20) continue;
        if (space) { out += ' '; space = false; }
        out += c;
    }
    if (out.size() > max) { std::size_t cut = max; while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut; out.resize(cut); out += "\xE2\x80\xA6"; }
    return out;
}
inline std::string Instruction(const std::string& lead, const std::vector<Line>& lines, const std::string& precisionLabel, const std::string& precision) {
    std::string out = lead;
    for (const auto& l : lines) {
        std::string note = OneLine(l.note, 300); for (auto& c : note) if (c == '"') c = '\'';   // one quoted line: the note cannot close its own quotes
        out += "\n- " + l.id + (note.empty() ? "" : " : \"" + note + "\"");
    }
    if (precision.find_first_not_of(" \t\r\n") != std::string::npos) out += "\n\n" + precisionLabel + "\n" + precision;
    return out;
}

} // namespace ChatBatch
