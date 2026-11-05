#pragma once

#include "PcbCanvas.h"
#include "PcbDocument.h"
#include <wx/dcmemory.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

struct PcbViewRegressionAccess
{
    static void Require(bool condition, const char* message)
    { if (!condition) throw std::runtime_error(std::string("PCB view: ") + message); }

    static std::shared_ptr<PcbDocument> Design()
    {
        auto document = std::make_shared<PcbDocument>();
        document->version = 20241229;
        document->layers = {{0, "F.Cu", "signal"}, {2, "In1.Cu", "signal"},
            {31, "B.Cu", "signal"}, {44, "Edge.Cuts", "user"}, {37, "B.SilkS", "user"}};
        document->nets = {{0, ""}, {1, "SIGNAL"}, {2, "GND"}};
        PcbFootprint footprint;
        footprint.reference = "J1"; footprint.value = "Back rotated";
        footprint.x = -10; footprint.y = -5; footprint.rotation = 90; footprint.layer = "B.Cu";
        PcbPad pad; pad.number = "1"; pad.type = "smd"; pad.shape = "oval";
        pad.position = {-10, -5}; pad.size = {4, 2}; pad.rotation = 90;
        pad.layers = {"B.Cu", "B.Mask", "B.Paste", "In8.Cu"}; pad.net = 1;
        footprint.pads.push_back(pad);
        pad.number = "2"; pad.type = "thru_hole"; pad.shape = "roundrect";
        pad.position = {8, 8}; pad.size = {4, 4}; pad.rotation = 0; pad.roundRectRatio = .25;
        pad.layers = {"*.Cu", "*.Mask"}; pad.drill = {1.4, 1.4}; pad.drillOffset = {.5, 0}; pad.net = 0;
        footprint.pads.push_back(pad);
        PcbGraphic text;
        text.kind = PcbGraphicKind::Text; text.points = {{-15, 0}}; text.text = "B12345";
        text.textSize = {1, 1}; text.rotation = 90; text.layer = "B.SilkS";
        footprint.graphics.push_back(text);
        document->footprints.push_back(footprint);
        document->tracks = {{{-5, 0}, {5, 0}, PcbPoint{0, -5}, .4, "F.Cu", 1},
            {{-5, 4}, {5, 4}, std::nullopt, .4, "In1.Cu", 2}};
        document->vias = {{{0, 8}, 2, .8, {"F.Cu", "B.Cu"}, 2}};
        PcbGraphic edge; edge.kind = PcbGraphicKind::Rect; edge.layer = "Edge.Cuts"; edge.width = .2;
        edge.points = {{-20, -15}, {20, -15}, {20, 15}, {-20, 15}};
        document->graphics.push_back(edge);
        document->bounds.Include({-20, -15}); document->bounds.Include({20, 15});
        return document;
    }

    static wxBitmap Render(PcbCanvas& canvas)
    {
        const auto size = canvas.GetClientSize();
        wxBitmap bitmap(size.x, size.y, 32);
        wxMemoryDC dc(bitmap);
        canvas.Draw(dc);
        dc.SelectObject(wxNullBitmap);
        return bitmap;
    }

    static void Run()
    {
        auto frame = std::make_unique<wxFrame>(nullptr, wxID_ANY, "PCB view regression");
        auto* canvas = new PcbCanvas(frame.get());
        canvas->SetSize(900, 650);
        auto document = Design();
        canvas->SetDocument(document);
        Require(canvas->GetDocument() == document && canvas->m_items.size() >= 10,
            "board document compiles to independent drawing primitives");
        const wxPoint2DDouble negative{-12.3, -8.4};
        const auto screen = canvas->WorldToScreen(negative);
        const auto restored = canvas->ScreenToWorld(screen);
        Require(std::fabs(negative.m_x - restored.m_x) <= .51 / canvas->m_scale &&
            std::fabs(negative.m_y - restored.m_y) <= .51 / canvas->m_scale,
            "negative world coordinates round-trip with fixed pixel precision");
        const wxPoint anchor(173, 211);
        const auto worldAtAnchor = canvas->ScreenToWorld(anchor);
        canvas->ZoomAt(2, anchor);
        const auto afterZoom = canvas->ScreenToWorld(anchor);
        Require(std::hypot(worldAtAnchor.m_x - afterZoom.m_x, worldAtAnchor.m_y - afterZoom.m_y) < 1e-10,
            "wheel zoom keeps the world point under the cursor");
        canvas->m_center = {0, 0}; canvas->m_scale = 20;
        auto hit = [&](double x, double y) { return canvas->HitTest(canvas->WorldToScreen({x, y})); };
        const auto pad = hit(-10, -6.6);
        Require(pad && pad->kind == PcbSelection::Kind::Pad && pad->index == 0 && pad->subIndex == 0,
            "absolute back-side pad rotation is used once");
        Require(!hit(-8.4, -5) || hit(-8.4, -5)->kind != PcbSelection::Kind::Pad,
            "rotated oval does not use its unrotated extent for picking");
        canvas->SetLayerVisible("B.Cu", false);
        Require(!hit(-10, -5) || hit(-10, -5)->kind != PcbSelection::Kind::Pad,
            "hidden copper pads cannot be picked through mask or paste entries");
        canvas->SetLayerVisible("B.Cu", true);
        const auto arc = hit(0, -5);
        Require(arc && arc->kind == PcbSelection::Kind::Track && arc->index == 0,
            "three-point arc follows its supplied midpoint");
        const auto inner = hit(0, 4);
        Require(inner && inner->kind == PcbSelection::Kind::Track && inner->index == 1,
            "inner copper geometry is imported into the same viewport");
        canvas->SetLayerVisible("In1.Cu", false);
        Require(!hit(0, 4) || hit(0, 4)->kind != PcbSelection::Kind::Track,
            "inner copper layer visibility affects picking");
        canvas->SetLayerVisible("In1.Cu", true);
        canvas->SetLayerVisible("F.Cu", false); canvas->SetLayerVisible("B.Cu", false);
        Require(canvas->IsLayerVisible("*.Cu") && hit(8, 8) && hit(8, 8)->kind == PcbSelection::Kind::Pad,
            "wildcard through-hole pads remain visible on a visible inner copper layer");
        Require(hit(0, 8) && hit(0, 8)->kind == PcbSelection::Kind::Via,
            "through-via layer spans include enabled inner copper layers");
        canvas->SetLayerVisible("In1.Cu", false);
        Require(!canvas->IsLayerVisible("*.Cu") && (!hit(8, 8) || hit(8, 8)->kind != PcbSelection::Kind::Pad),
            "wildcard pads become hidden when every copper layer is hidden");
        Require(!canvas->IsLayerVisible("In8.Cu"), "unused padstack copper layers cannot create visible board layers");
        canvas->ShowAllLayers(true);
        canvas->Select({PcbSelection::Kind::Pad, 0, 0});
        Require(canvas->m_highlightedNet == 1, "selecting connected pad highlights its electrical net");
        canvas->Select({PcbSelection::Kind::Pad, 0, 1});
        Require(!canvas->m_highlightedNet, "net zero is never treated as a shared network");
        canvas->SetHighlightedNet(0);
        Require(!canvas->m_highlightedNet, "explicit zero-network selection does not group unconnected objects");
        canvas->Select({});
        const auto text = hit(-15, 1.2);
        Require(text && text->kind == PcbSelection::Kind::Graphic && text->footprintGraphic,
            "rotated text picking uses a rotated world polygon");
        canvas->m_scale = 20; canvas->m_center = {0, 0};
        wxBitmap bitmap(900, 650, 32);
        wxMemoryDC dc(bitmap);
        canvas->Draw(dc);
        dc.SelectObject(wxNullBitmap);
        const auto image = bitmap.ConvertToImage();
        const auto hole = canvas->WorldToScreen({8.5, 8});
        const auto copper = canvas->WorldToScreen({7.1, 8});
        Require(std::abs(static_cast<int>(image.GetRed(hole.x, hole.y)) - 22) < 3 &&
            std::abs(static_cast<int>(image.GetBlue(hole.x, hole.y)) - 40) < 3,
            "offset drill hole is rendered at its normalized world position");
        Require(image.GetRed(copper.x, copper.y) > 150,
            "rounded through-hole land remains visible around the drill");
        for (const double scale : {5., 40., 100.})
        {
            canvas->m_scale = scale;
            canvas->m_center = {-3.25, 1.5};
            const auto target = canvas->WorldToScreen({2, 4});
            const auto picked = canvas->HitTest(target + wxPoint(0, canvas->FromDIP(5)));
            Require(picked && picked->kind == PcbSelection::Kind::Track && picked->index == 1,
                "picking tolerance stays in screen pixels after zoom and pan");
        }
        canvas->ShowAllLayers(false);
        Require(!canvas->HitTest(canvas->WorldToScreen({0, 4})), "all-hidden mode removes every geometry hit target");
        canvas->ShowAllLayers(true);
        canvas->SetLayerVisible("F.Cu", false);
        canvas->SetHighlightedNet(1);
        canvas->m_scale = 73; canvas->m_center = {-1.25, 3.75}; canvas->m_fitted = false;
        auto replacement = std::make_shared<PcbDocument>(*document);
        replacement->nets = {{0, ""}, {1, "ADDED"}, {2, "GND"}, {7, "SIGNAL"}};
        replacement->footprints[0].pads[0].net = 7; replacement->tracks[0].net = 7;
        canvas->SetDocument(replacement, true);
        Require(canvas->m_scale == 73 && canvas->m_center.m_x == -1.25 && canvas->m_center.m_y == 3.75 &&
            !canvas->IsLayerVisible("F.Cu") && canvas->m_highlightedNet == 7 &&
            canvas->GetSelection().kind == PcbSelection::Kind::None,
            "same-source refresh keeps viewport, layer filters and highlighted net by name instead of stale array IDs");
        canvas->SetDocument(nullptr);
        Require(canvas->m_items.empty() && canvas->GetSelection().kind == PcbSelection::Kind::None,
            "replacing a board clears previous cache and selection");
    }
};
