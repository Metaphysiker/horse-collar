// ── Horse Collar Sensor Casing (heat-set insert version) ───────────────
// Components: LiPo Battery + HUZZAH32 (no headers) + BNO085
// Two-part print: box (battery) + flat lid (boards screwed in), joined by 4 corner M2 screws
// HUZZAH32 and BNO085 screwed face-down to lid via M2 heat-set inserts in boss posts

// ── Tuneables ──────────────────────────────────────────────────────────
lid_assembled = false;  // true = lid ghosted on top for alignment check; false = lid flat beside box for printing
wall        = 2.0;   // wall and floor thickness
lid_depth   = 4.0;   // lid thickness — 4 mm minimum for M2 heat-set inserts
gap         = 3;   // clearance between battery top and board components
boards_gap  = 2.0;   // gap between HUZZAH32 and BNO085 in width direction
$fn         = 40;

// ── Component dimensions ───────────────────────────────────────────────
bat_length = 60.5;   bat_width = 50.0;   bat_depth = 7.8;

huz_length     = 51.0;    // official Adafruit + STL confirmed
huz_width      = 22.86;   // STL measured
huz_pcb_depth  = 1.6;     // PCB thickness
huz_comp_depth = 5.7;     // official total 7.3mm - PCB 1.6mm
huz_depth      = huz_pcb_depth + huz_comp_depth;

bno_length = 25.4;   bno_width = 22.86;   bno_depth = 4.53;

// USB-C cutout
usb_w = 9.5;
usb_h = 4.0;

// ── Mounting holes ─────────────────────────────────────────────────────
// BNO085 (from Adafruit Eagle PCB — 2.54 mm inset from each edge)
bno_holes  = [[2.54, 2.54], [22.86, 2.54], [2.54, 20.32], [22.86, 20.32]];
bno_hole_d = 2.2;   // M2 clearance

// HUZZAH32 mounting holes
// Eagle PCB positions + 0.56 mm STL X origin offset (STL starts at x=-0.56, corrected in huzzah32())
huz_stl_x = 0.56;
huz_holes = [
    [2.54  + huz_stl_x,  2.54],
    [48.26 + huz_stl_x,  1.84],
    [2.54  + huz_stl_x, 20.32],
    [48.26 + huz_stl_x, 20.96],
];
huz_hole_d = 2.2;   // M2 clearance

// ── Corner screw posts (M2) ───────────────────────────────────────────
post_od     = 6.0;   // outer diameter — sized for M2 heat-set insert
csink_d     = 4.5;
csink_depth = 1.4;
screw_clr_d = 2.2;   // M2 clearance through lid

// ── Corner heat-set inserts (M2) ──────────────────────────────────────
// M2 insert: 3.0 mm hole, 3.0 mm long — same spec as board inserts
// Use M2 × 8 mm screws
corner_ins_depth  = 3.0;
corner_ins_hole_d = 3.0;

// ── Board heat-set inserts (M2) ────────────────────────────────────────
// M2 insert: 3.2 mm OD, 3.0 mm long — boss hangs from lid interior
ins_depth  = 3.0;   // M2 insert length
ins_hole_d = 3.0;   // hole diameter (slightly under insert OD for press fit)

// ── Box dimensions ─────────────────────────────────────────────────────
bat_clear_x   = 0.5;   // clearance between battery and post inner edge (x, each side)
bat_clear_x_y = 0.5;   // clearance between battery and wall (y, each side)
// Length: battery + two end zones derived from post position (wall + post_od - 1 = screw_off + post_od/2)
box_length = bat_length + (wall + post_od - 1 + bat_clear_x) * 2;
box_width  = bat_width  + wall * 2 + bat_clear_x_y * 2;
// Depth: floor + battery + gap + boards hanging from lid
box_depth  = wall + bat_depth + gap + huz_depth;

inner_length = box_length - 2 * wall;
inner_width  = box_width  - 2 * wall;
inner_depth  = box_depth  - wall;

post_depth = box_depth - wall;
screw_off  = wall + post_od / 2 - 1;  // 1 mm into wall — insert hole still clears outer face

// ── Board layout (assembly / box coordinates) ──────────────────────────
// HUZZAH32: USB-C end flush with right wall; both boards centred in width
boards_total_width = huz_width + boards_gap + bno_width;
boards_start_y = wall + (inner_width - boards_total_width) / 2;

huz_off_x = 7;    // USB-C channel 9 mm deep
bno_off_y = boards_start_y;                                // BNO at low Y side
huz_off_y = boards_start_y + bno_width + boards_gap;       // HUZZAH at high Y side
bno_off_x = huz_off_x + huz_length - bno_length;           // BNO right-aligned under HUZZAH, away from JST port

// USB-C cutout position on right wall
usb_y = huz_off_y + huz_width / 2 - usb_w / 2;
usb_z = box_depth - huz_pcb_depth - usb_h;

// ── Modules ────────────────────────────────────────────────────────────

module box_body() {
    difference() {
        cube([box_length, box_width, box_depth]);
        // Hollow inside
        translate([wall, wall, wall])
            cube([inner_length, inner_width, inner_depth + 0.1]);
        // USB-C channel: cuts through left wall and into interior to reach port
        translate([-0.1, usb_y, usb_z])
            cube([huz_off_x + wall + 0.2, usb_w, usb_h]);
    }
}

module corner_post(x, y) {
    difference() {
        translate([x, y, wall])
            cylinder(h = post_depth, d = post_od);
        translate([x, y, wall + post_depth - corner_ins_depth])
            cylinder(h = corner_ins_depth + 0.1, d = corner_ins_hole_d);
    }
}

module corner_screw_hole() {
    cylinder(h = lid_depth + 0.2, d = screw_clr_d);
    cylinder(h = csink_depth, d1 = csink_d, d2 = screw_clr_d);
}

module lid() {
    difference() {
        cube([box_length, box_width, lid_depth]);
        // Corner screws
        translate([screw_off,              screw_off,            -0.1]) corner_screw_hole();
        translate([box_length - screw_off, screw_off,            -0.1]) corner_screw_hole();
        translate([screw_off,              box_width - screw_off,-0.1]) corner_screw_hole();
        translate([box_length - screw_off, box_width - screw_off,-0.1]) corner_screw_hole();
        // Heat-set insert holes — blind holes from interior face (z=lid_depth) going inward
        // PCB sits against interior face; screw enters from below through PCB hole into insert
        for (h = huz_holes)
            translate([huz_off_x + h[0], huz_off_y + h[1], lid_depth - ins_depth])
                cylinder(h = ins_depth + 0.1, d = ins_hole_d, $fn = 32);
        for (h = bno_holes)
            translate([bno_off_x + h[0], bno_off_y + h[1], lid_depth - ins_depth])
                cylinder(h = ins_depth + 0.1, d = ins_hole_d, $fn = 32);
    }
}

module huzzah32() {
    // STL origin is at x=-0.56 — shift so PCB left edge = x=0
    color("ForestGreen", 0.15) translate([0.56, 0, 0])
        import("3405 Adafruit HUZZAH32 ESP32 Feather.stl");
}

// Both boards face down from lid: PCB at z=box_depth, components hang into box
module place_huzzah() {
    translate([huz_off_x, huz_off_y + huz_width, box_depth])
        rotate([180, 0, 0])
            huzzah32();
}

module place_bno() {
    translate([bno_off_x, bno_off_y + bno_width, box_depth])
        rotate([180, 0, 0])
            import("4754 BNO085 STEMMA QT.stl");
}

module place_battery() {
    color("SkyBlue")
        translate([wall + post_od - 1 + bat_clear_x, wall + bat_clear_x_y, wall])
            cube([bat_length, bat_width, bat_depth]);
}

// ── Assembly ───────────────────────────────────────────────────────────
if (lid_assembled) { % box_body(); } else { box_body(); }

corner_post(screw_off,              screw_off);
corner_post(box_length - screw_off, screw_off);
corner_post(screw_off,              box_width - screw_off);
corner_post(box_length - screw_off, box_width - screw_off);

if (lid_assembled) {
    // Lid ghosted on top — interior face down, countersinks up, holes align with boards
    % translate([0, 0, box_depth + lid_depth])
        mirror([0, 0, 1])
            lid();
} else {
    // Lid flat beside box — interior face down on print bed, countersinks up
    translate([0, box_width + 10, lid_depth])
        mirror([0, 0, 1])
            lid();
}

if (lid_assembled) {
    place_huzzah();
    color("DarkGrey") place_bno();
    place_battery();
    color("Red")  for (h = huz_holes)
        translate([huz_off_x + h[0], huz_off_y + h[1], box_depth - 0.5])
            cylinder(h = 2, d = 1.5, $fn = 16);
    color("Blue") for (h = bno_holes)
        translate([bno_off_x + h[0], bno_off_y + h[1], box_depth - 0.5])
            cylinder(h = 2, d = 1.5, $fn = 16);
} else {
    % place_huzzah();
    % color("DarkGrey", 0.15) place_bno();
    % place_battery();
}

echo(str("Box: ", box_length, " × ", box_width, " × ", box_depth, " mm"));
echo(str("Total assembled: ", box_length, " × ", box_width, " × ", box_depth + lid_depth, " mm"));
