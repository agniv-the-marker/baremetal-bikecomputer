#!/usr/bin/env python3
"""
claude did this for me <3

osm2bin.py - fetch roads for a bounding box from OpenStreetMap (Overpass API) and
write a compact ROADS.BIN for the bare-metal bike computer.

ROADS.BIN format (little-endian):
    char   magic[4] = "ROAD"
    uint32 nseg
    nseg * { int32 lat1, lon1, lat2, lon2 }   # microdegrees (deg * 1e6)

Each road way is split into consecutive point-pairs (segments). Only internet +
python3 stdlib needed. Then copy ROADS.BIN to the SD card root.

Usage (defaults cover the Stanford -> Old La Honda -> Alice's -> 84 -> Portola ->
Sand Hill loop):
    python3 osm2bin.py                     # uses the default bbox
    python3 osm2bin.py S W N E [out.bin]   # custom bbox (south west north east)
"""
import json, struct, sys, urllib.request

# default bbox for the ride: south, west, north, east
S, W, N, E = 37.350, -122.270, 37.440, -122.130
OUT = "ROADS.BIN"

# road classes worth drawing (skip footways/steps to cut clutter; keep bike/paths).
# drivable/ridable roads worth showing. (service/track/footway excluded: they're
# driveways/parking aisles/dirt and just clutter a 128x64 screen.)
KEEP = {
    "motorway","trunk","primary","secondary","tertiary","unclassified",
    "residential","living_street","cycleway",
    "motorway_link","trunk_link","primary_link","secondary_link","tertiary_link",
}

def main():
    a = sys.argv[1:]
    s, w, n, e = (float(a[0]), float(a[1]), float(a[2]), float(a[3])) if len(a) >= 4 else (S, W, N, E)
    out = a[4] if len(a) >= 5 else OUT

    q = (f'[out:json][timeout:120];'
         f'way["highway"]({s},{w},{n},{e});out geom;')
    print(f"querying Overpass for highways in ({s},{w},{n},{e}) ...")
    req = urllib.request.Request("https://overpass-api.de/api/interpreter",
                                 data=("data=" + q).encode(),
                                 headers={"User-Agent": "pi-bikecomputer/1.0"})
    data = json.load(urllib.request.urlopen(req, timeout=180))

    segs = []
    for el in data.get("elements", []):
        if el.get("type") != "way":
            continue
        if el.get("tags", {}).get("highway") not in KEEP:
            continue
        g = el.get("geometry") or []
        for p, c in zip(g, g[1:]):
            segs.append((int(round(p["lat"]*1e6)), int(round(p["lon"]*1e6)),
                         int(round(c["lat"]*1e6)), int(round(c["lon"]*1e6))))

    print(f"got {len(segs)} road segments")
    with open(out, "wb") as f:
        f.write(b"ROAD")
        f.write(struct.pack("<I", len(segs)))
        for la1, lo1, la2, lo2 in segs:
            f.write(struct.pack("<iiii", la1, lo1, la2, lo2))
    sz = 8 + len(segs)*16
    print(f"wrote {out}: {sz} bytes ({sz//1024} KB). Copy it to the SD card root.")
    if len(segs) > 120000:
        print("WARNING: >120000 segments; the device caps at MAX_SEG=120000. "
              "Shrink the bbox or raise MAX_SEG/RAW in osm_map.c.")

if __name__ == "__main__":
    main()
