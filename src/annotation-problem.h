// What the browser UI says when the annotation store refuses an operation, in the interface language.
// The store's own `Outcome::message` is French text written for agents (MCP) and is logged, never shown:
// the UI maps the structured `reason` (when a person can act on it) or else the `code`.
#pragma once
#include <string>
#include "annotations.h"
#include "i18n.h"

namespace AnnotationProblem {

inline std::wstring Text(const BrowserAnnotations::Outcome& o) {
    namespace BA = BrowserAnnotations;
    switch (o.reason) {
    case BA::Reason::SiteFull: return TR(annotation_problem_site_full);
    case BA::Reason::StoreFull: return TR(annotation_problem_store_full);
    case BA::Reason::Migrating: return TR(annotation_problem_migrating);
    case BA::Reason::MovedToInstalled: return TR(annotation_problem_moved);
    case BA::Reason::CaptureRefused: return TR(annotation_problem_capture);
    case BA::Reason::None: break;
    }
    switch (o.code) {
    case BA::Code::NotFound: return TR(annotation_problem_not_found);
    case BA::Code::Busy: return TR(annotation_problem_busy);
    case BA::Code::LimitReached: return TR(annotation_problem_limit);
    case BA::Code::Unsafe: return TR(annotation_problem_unsafe);
    case BA::Code::Invalid: return TR(annotation_problem_invalid);
    case BA::Code::Conflict: return TR(annotation_problem_conflict);
    case BA::Code::WrongToken: case BA::Code::NotClaimable: return TR(annotation_problem_changed);
    case BA::Code::Io: case BA::Code::Ok: break;
    }
    return TR(annotation_problem_io);
}

}  // namespace AnnotationProblem
