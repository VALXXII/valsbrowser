#pragma once
// SVG path data to a Direct2D geometry (Paper / Phosphor glyph exports), shared by the toolbar
// (chrome.h: address-bar request chip) and the dialogs (modal.h DialogKit).
#include <d2d1.h>
#include <cmath>
#include <cstdlib>
namespace NativeChrome {
namespace DialogKit {
// SVG path data (M L H V C S Q T A Z, absolute and relative) to a Direct2D geometry in
// viewBox units. Nonzero fill like SVG. Returns nullptr on malformed data.
inline ID2D1PathGeometry* SvgGeometry(ID2D1Factory* factory, const char* d, bool evenOdd = false) {   // evenOdd: SVG fill-rule="evenodd"
    if (!factory || !d) return nullptr;
    ID2D1PathGeometry* path = nullptr; ID2D1GeometrySink* sink = nullptr;
    if (FAILED(factory->CreatePathGeometry(&path))) return nullptr;
    if (FAILED(path->Open(&sink))) { path->Release(); return nullptr; }
    sink->SetFillMode(evenOdd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);
    const char* p = d; char cmd = 0; bool open = false, ok = true;
    D2D1_POINT_2F cur{0, 0}, start{0, 0}, ctrl{0, 0}; char last = 0;
    auto skip = [&] { while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\t' || *p == '\r') ++p; };
    auto number = [&](float& out) { skip(); char* end = nullptr; out = std::strtof(p, &end); if (end == p) return false; p = end; return true; };
    auto flag = [&](bool& out) { skip(); if (*p != '0' && *p != '1') return false; out = *p == '1'; ++p; return true; };
    auto begin = [&] { if (!open) { sink->BeginFigure(cur, D2D1_FIGURE_BEGIN_FILLED); start = cur; open = true; } };
    while (ok) {
        skip(); if (!*p) break;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) cmd = *p++;
        else if (!cmd) { ok = false; break; }
        const bool rel = cmd >= 'a'; const char c = static_cast<char>(rel ? cmd - 32 : cmd);
        const D2D1_POINT_2F o = rel ? cur : D2D1_POINT_2F{0, 0};
        if (c == 'Z') { if (open) { sink->EndFigure(D2D1_FIGURE_END_CLOSED); open = false; } cur = start; last = 'Z'; cmd = 0; continue; }
        if (c == 'M') {
            float x, y; if (!number(x) || !number(y)) { ok = false; break; }
            if (open) { sink->EndFigure(D2D1_FIGURE_END_OPEN); open = false; }
            cur = {o.x + x, o.y + y}; begin(); cmd = rel ? 'l' : 'L'; last = 'M'; continue;
        }
        begin();
        if (c == 'L') { float x, y; if (!number(x) || !number(y)) { ok = false; break; } cur = {o.x + x, o.y + y}; sink->AddLine(cur); }
        else if (c == 'H') { float x; if (!number(x)) { ok = false; break; } cur.x = rel ? cur.x + x : x; sink->AddLine(cur); }
        else if (c == 'V') { float y; if (!number(y)) { ok = false; break; } cur.y = rel ? cur.y + y : y; sink->AddLine(cur); }
        else if (c == 'C' || c == 'S') {
            float x1 = 0, y1 = 0, x2, y2, x, y; D2D1_POINT_2F a;
            if (c == 'C') { if (!number(x1) || !number(y1)) { ok = false; break; } a = {o.x + x1, o.y + y1}; }
            else a = (last == 'C' || last == 'S') ? D2D1_POINT_2F{2 * cur.x - ctrl.x, 2 * cur.y - ctrl.y} : cur;
            if (!number(x2) || !number(y2) || !number(x) || !number(y)) { ok = false; break; }
            const D2D1_POINT_2F b{o.x + x2, o.y + y2}, e{o.x + x, o.y + y};
            sink->AddBezier(D2D1::BezierSegment(a, b, e)); ctrl = b; cur = e;
        } else if (c == 'Q' || c == 'T') {
            float x1, y1, x, y; D2D1_POINT_2F a;
            if (c == 'Q') { if (!number(x1) || !number(y1)) { ok = false; break; } a = {o.x + x1, o.y + y1}; }
            else a = (last == 'Q' || last == 'T') ? D2D1_POINT_2F{2 * cur.x - ctrl.x, 2 * cur.y - ctrl.y} : cur;
            if (!number(x) || !number(y)) { ok = false; break; }
            const D2D1_POINT_2F e{o.x + x, o.y + y};
            sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(a, e)); ctrl = a; cur = e;
        } else if (c == 'A') {
            float rx, ry, rot, x, y; bool large, sweep;
            if (!number(rx) || !number(ry) || !number(rot) || !flag(large) || !flag(sweep) || !number(x) || !number(y)) { ok = false; break; }
            const D2D1_POINT_2F e{o.x + x, o.y + y};
            sink->AddArc(D2D1::ArcSegment(e, D2D1::SizeF(std::fabs(rx), std::fabs(ry)), rot, sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
            cur = e;
        } else { ok = false; break; }
        last = c;
    }
    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
    const HRESULT closed = sink->Close(); sink->Release();
    if (!ok || FAILED(closed)) { path->Release(); return nullptr; }
    return path;
}
}
}
