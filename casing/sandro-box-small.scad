// ── Horse Collar Sensor Casing ─────────────────────────────────────────
// Components: LiPo Battery + HUZZAH32 (no headers) + BNO085
// Two-part print: box (battery) + flat lid (boards screwed in), joined by 4 corner M2.5 screws
// HUZZAH32 and BNO085 are both screwed face-down to the lid interior

// ── Tuneables ──────────────────────────────────────────────────────────
wall        = 2.0;   // wall and floor thickness
lid_depth   = 3.0;   // lid thickness
gap         = 1.0;   // clearance between battery top and board components
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

// ── Corner screw posts (M2.5) ──────────────────────────────────────────
post_od      = 4.0;
post_pilot_d = 2.0;
post_od_act  = post_od * 0.88;
csink_d      = 5.5;
csink_depth  = 1.4;
screw_clr_d  = 2.4;   // M2.5 clearance through lid

// ── Box dimensions ─────────────────────────────────────────────────────
// Width driven by battery; length adds corner post material
box_length = bat_length + post_od_act * 2 + wall * 2;
box_width  = bat_width  + wall * 2;
// Depth: floor + battery + gap + boards hanging from lid
box_depth  = wall + bat_depth + gap + huz_depth;

inner_length = box_length - 2 * wall;
inner_width  = box_width  - 2 * wall;
inner_depth  = box_depth  - wall;

post_depth = box_depth - wall;
screw_off  = wall + post_pilot_d / 2 + 0.5;

// ── Board layout (assembly / box coordinates) ──────────────────────────
// HUZZAH32: USB-C end flush with right wall; both boards centred in width
boards_total_width = huz_width + boards_gap + bno_width;
boards_start_y = wall + (bat_width - boards_total_width) / 2;

huz_off_x = 10;   // centred between corner posts; USB-C channel ~12 mm deep
bno_off_y = boards_start_y;                                // BNO at low Y side
huz_off_y = boards_start_y + bno_width + boards_gap;       // HUZZAH at high Y side
bno_off_x = huz_off_x + (huz_length - bno_length) / 2;    // BNO centred in length under HUZZAH

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
        translate([x, y, wall])   cylinder(h = post_depth,       d = post_od_act);
        translate([x, y, wall])   cylinder(h = post_depth + 0.1, d = post_pilot_d);
    }
}

module corner_screw_hole() {
    cylinder(h = lid_depth + 0.2, d = screw_clr_d);
    cylinder(h = csink_depth, d1 = csink_d, d2 = screw_clr_d);
}

module board_screw_hole(hole_d) {
    cylinder(h = lid_depth + 0.2, d = hole_d, $fn = 32);
    cylinder(h = csink_depth, d1 = hole_d + 2.0, d2 = hole_d, $fn = 32);
}

module lid() {
    difference() {
        cube([box_length, box_width, lid_depth]);
        // Corner screws
        translate([screw_off,              screw_off,            -0.1]) corner_screw_hole();
        translate([box_length - screw_off, screw_off,            -0.1]) corner_screw_hole();
        translate([screw_off,              box_width - screw_off,-0.1]) corner_screw_hole();
        translate([box_length - screw_off, box_width - screw_off,-0.1]) corner_screw_hole();
        // HUZZAH32 mounting holes
        for (h = huz_holes)
            translate([huz_off_x + h[0], huz_off_y + h[1], -0.1])
                board_screw_hole(huz_hole_d);
        // BNO085 mounting holes
        for (h = bno_holes)
            translate([bno_off_x + h[0], bno_off_y + h[1], -0.1])
                board_screw_hole(bno_hole_d);
    }
}

module huzzah32() {
    // STL origin is at x=-0.56 — shift so PCB left edge = x=0
    color("ForestGreen", 0.8) translate([0.56, 0, 0])
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
        translate([wall + post_od_act, wall, wall])
            cube([bat_length, bat_width, bat_depth]);
}

// ── Assembly ───────────────────────────────────────────────────────────
//box_body();

corner_post(screw_off,              screw_off);
corner_post(box_length - screw_off, screw_off);
corner_post(screw_off,              box_width - screw_off);
corner_post(box_length - screw_off, box_width - screw_off);

// Lid on top, ghost so components inside remain visible
// mirror([0,0,1]) flips Z only — countersinks face up, Y unchanged so holes align correctly
% translate([0, 0, box_depth + lid_depth])
    mirror([0, 0, 1])
        lid();

// Lid beside box for printing (uncomment when exporting):
//translate([0, 2 * box_width + 10, lid_depth])
//    rotate([180, 0, 0])
//        lid();

place_huzzah();
place_bno();
place_battery();

echo(str("Box: ", box_length, " × ", box_width, " × ", box_depth, " mm"));
echo(str("Total assembled: ", box_length, " × ", box_width, " × ", box_depth + lid_depth, " mm"));
