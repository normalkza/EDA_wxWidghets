#pragma once

#include <wx/string.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Imported geometry is normalized to board/world coordinates, in millimetres.
// Positive X points right, positive Y points down. Angles follow KiCad's
// counter-clockwise convention (a +90 degree rotation maps +X to -Y).
struct PcbPoint
{
    double x = 0;
    double y = 0;
    bool operator==(const PcbPoint&) const = default;
};

struct PcbBounds
{
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool valid = false;
    void Include(PcbPoint p, double margin = 0)
    {
        if (!valid) { minX = p.x - margin; maxX = p.x + margin; minY = p.y - margin; maxY = p.y + margin; valid = true; }
        else { minX = std::min(minX, p.x - margin); maxX = std::max(maxX, p.x + margin);
            minY = std::min(minY, p.y - margin); maxY = std::max(maxY, p.y + margin); }
    }
    double Width() const { return valid ? maxX - minX : 0; }
    double Height() const { return valid ? maxY - minY : 0; }
};

struct PcbLayer { int id = 0; std::string name; std::string type; std::string userName; };
enum class PcbGraphicKind { Line, Arc, Circle, Rect, Polygon, Text };

struct PcbGraphic
{
    PcbGraphicKind kind = PcbGraphicKind::Line;
    // Line: start,end; Arc: start,mid,end; Circle: centre,rim;
    // Rect/Polygon: ordered corners; Text: anchor. All world coordinates.
    std::vector<PcbPoint> points;
    std::string layer;
    double width = 0.15;
    bool filled = false;
    bool hidden = false;
    std::string text;
    PcbPoint textSize{1, 1};
    double rotation = 0;
};

struct PcbPad
{
    std::string number;
    std::string type;
    std::string shape;
    PcbPoint position;
    PcbPoint size;
    // Absolute world angle. Do not add the footprint rotation again.
    double rotation = 0;
    PcbPoint drill;
    // World-space vector from the pad centre to its drill centre.
    PcbPoint drillOffset;
    double roundRectRatio = 0;
    std::vector<std::string> layers;
    int net = 0;
};

struct PcbFootprint
{
    std::string reference;
    std::string value;
    std::string libraryId;
    double x = 0, y = 0, rotation = 0;
    std::string layer;
    std::vector<PcbPad> pads;
    std::vector<PcbGraphic> graphics;
};

struct PcbTrack
{
    PcbPoint start, end;
    std::optional<PcbPoint> mid;
    double width = 0;
    std::string layer;
    int net = 0;
};
struct PcbVia
{
    PcbPoint position;
    double diameter = 0, drill = 0;
    std::vector<std::string> layers;
    int net = 0;
};
struct PcbZonePolygon
{
    std::string layer;
    std::vector<PcbPoint> points;
    bool filled = false;
};
struct PcbZone
{
    int net = 0;
    std::vector<std::string> layers;
    bool keepout = false;
    std::vector<PcbZonePolygon> polygons;
};

struct PcbDocument
{
    wxString sourcePath;
    // Retain the exact input, including unsupported sections, for lossless copy.
    std::string originalSource;
    int version = 0;
    std::vector<PcbLayer> layers;
    std::map<int, std::string> nets;
    std::vector<PcbFootprint> footprints;
    std::vector<PcbTrack> tracks;
    std::vector<PcbVia> vias;
    std::vector<PcbGraphic> graphics;
    std::vector<PcbZone> zones;
    std::vector<std::string> warnings;
    PcbBounds bounds;
};

// Footprint coordinates in a .kicad_pcb already contain the stored back-side
// reflection. Applying a second B.Cu mirror would corrupt imported geometry.
inline PcbPoint PcbTransformFootprintPoint(const PcbFootprint& footprint, PcbPoint local)
{
    const double radians = footprint.rotation * 3.14159265358979323846 / 180.0;
    return {footprint.x + local.x * std::cos(radians) + local.y * std::sin(radians),
        footprint.y - local.x * std::sin(radians) + local.y * std::cos(radians)};
}
