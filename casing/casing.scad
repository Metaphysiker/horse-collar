// ── Horse Collar Sensor Casing ───────────────────────────────────────
//
// Components:
//   Battery  PKCell LP-785060-J   7.8 × 50 × 60 mm  (lying flat, bottom layer)
//   HUZZAH32 Adafruit #3619      22.9 × 57.1 mm PCB (middle layer, centred)
//   BNO085   Adafruit #4754      25.6 × 22.7 × 4.6 mm (top layer, one end)
//
// Two-part print: bottom shell (battery) + lid (HUZZAH32 + BNO085)
// Joined by snap rim: rim on bottom shell slots into groove in lid.

// ── Tuneables ────────────────────────────────────────────────────────
show_shell      = true;   // show the printed shells
show_components = true;   // show battery / HUZZAH32 / BNO085

wall  = 2.0;   // shell wall / floor / ceiling thickness
tol   = 0.5;   // clearance added to each component cavity
cr    = 3.0;   // outer corner radius
$fn   = 40;

// Corner screws joining lid to bottom shell (M2.5, 4 corners)
screw_clr_d   = 2.7;   // M2.5 clearance hole through lid
screw_head_d  = 5.0;   // M2.5 pan head recess diameter
screw_head_h  = 1.8;   // pan head recess depth
screw_pilot_d = 2.0;   // M2.5 pilot hole in bottom shell (self-tapping)
screw_pilot_h = 8.0;   // pilot hole depth

// HUZZAH32 PCB locating pegs (press-fit into Adafruit Feather mounting holes)
pcb_peg_d      = 2.0;   // peg diameter — tight in 2.2 mm Feather holes; sand if needed
pcb_peg_h      = 3.5;   // peg height
HUZ_HOLE_IN_L  = 3.2;   // hole inset from short edge  (Adafruit Feather standard)
HUZ_HOLE_IN_W  = 1.7;   // hole inset from long edge

// BNO085 boss posts (M2.0 self-tapping screws inserted from inside case, up through PCB hole)
bno_boss_d     = 4.0;   // outer boss diameter
bno_screw_d    = 1.6;   // pilot hole diameter for M2 self-tapping
bno_pilot_dep  = 8.0;   // pilot hole depth into boss

// ── Component dimensions ─────────────────────────────────────────────
BAT_L = 60.0;   BAT_W = 50.0;   BAT_H = 7.8;

HUZ_L = 57.1;   HUZ_W = 22.9;
HUZ_PCB_H  = 1.6;    // PCB thickness
HUZ_COMP_H = 9.0;    // tallest SMD component above PCB (USB-C port)
HUZ_PIN_H  = 12.0;   // male pin tail below PCB — measured from female jumper cable (~12 mm)
HUZ_FEM_H  = 12.0;   // female Dupont socket height above PCB — measured jumper cable
HDR_W      = 2.54;   // header strip width (one row of 2.54mm pitch)
HUZ_H = HUZ_PCB_H + max(HUZ_COMP_H, HUZ_PIN_H);

BNO_L = 25.4;   BNO_W = 22.86;  BNO_H = 4.53;  // measured from STL bounding box

// ── Layout ───────────────────────────────────────────────────────────
// HUZZAH32 and BNO085 sit side-by-side in the width direction, centred together.
// Combined width: 22.9 + 2mm gap + 22.86 = 47.76mm — fits in 50mm inner width.

IN_L = BAT_L;                          // inner length = 60 mm
IN_W = BAT_W;                          // inner width  = 50 mm

OUT_L = IN_L + 2 * wall;
OUT_W = IN_W + 2 * wall;

BOT_H = wall + BAT_H + wall;           // bottom shell total height
LID_H = wall + HUZ_W + wall;           // lid total height — HUZZAH32 stands upright, height = HUZ_W

BNO_GAP   = 2.0;
BOARDS_W  = HUZ_W + BNO_GAP + BNO_W;

HUZ_OFF_L = (IN_L - HUZ_L) / 2;
HUZ_OFF_W = (IN_W - BOARDS_W) / 2;

BNO_OFF_L = (IN_L - BNO_L) / 2;
BNO_OFF_W = IN_W - BNO_W;

// HUZZAH32 mounting hole positions relative to PCB corner
huz_holes = [
  [HUZ_HOLE_IN_L,         HUZ_HOLE_IN_W        ],
  [HUZ_L - HUZ_HOLE_IN_L, HUZ_HOLE_IN_W        ],
  [HUZ_HOLE_IN_L,         HUZ_W - HUZ_HOLE_IN_W],
  [HUZ_L - HUZ_HOLE_IN_L, HUZ_W - HUZ_HOLE_IN_W],
];

// BNO085 mounting hole positions relative to board corner (from Adafruit Eagle PCB file)
bno_holes = [
  [ 2.54,  2.54],
  [22.86,  2.54],
  [ 2.54, 20.32],
  [22.86, 20.32],
];

// USB-C cutout
USB_W = 9.5;
USB_H = 4.0;
usb_y = wall + HUZ_OFF_W + HUZ_W / 2 - USB_W / 2;
usb_z = wall + HUZ_PCB_H;

// ── Component models ─────────────────────────────────────────────────
module huzzah32() {
    // STL (offset corrected — model origin is at x=−0.56)
    color("ForestGreen", 0.8) translate([0.56, 0, 0])
        import("3405 Adafruit HUZZAH32 ESP32 Feather.stl");
    // PCB slab
    color("ForestGreen", 0.4) cube([HUZ_L, HUZ_W, HUZ_PCB_H]);
    // Female headers — both long sides, above PCB
    color("Gold", 0.5) translate([0, 0, HUZ_PCB_H])
        cube([HUZ_L, HDR_W, HUZ_FEM_H]);
    color("Gold", 0.5) translate([0, HUZ_W - HDR_W, HUZ_PCB_H])
        cube([HUZ_L, HDR_W, HUZ_FEM_H]);
    // Male pins — both long sides, below PCB (sticking out from belly)
    color("CornflowerBlue", 0.8) translate([0, 0, -HUZ_PIN_H])
        cube([HUZ_L, HDR_W, HUZ_PIN_H]);
    color("CornflowerBlue", 0.8) translate([0, HUZ_W - HDR_W, -HUZ_PIN_H])
        cube([HUZ_L, HDR_W, HUZ_PIN_H]);
}

// ── Primitives ───────────────────────────────────────────────────────
module rbox(l, w, h) {
    hull()
        for (x = [cr, l - cr], y = [cr, w - cr])
            translate([x, y, 0]) cylinder(r = cr, h = h);
}

// Rectangular frame (solid outer minus hollow inner)
module rim_frame(l, w, h, t) {
    difference() {
        cube([l, w, h]);
        translate([t, t, -0.1])
            cube([l - 2*t, w - 2*t, h + 0.2]);
    }
}

// ── Bottom shell ─────────────────────────────────────────────────────
module bottom() {
    difference() {
        rbox(OUT_L, OUT_W, BOT_H);

        // Battery cavity (open top)
        translate([wall, wall, wall])
            cube([BAT_L + tol, BAT_W + tol, BOT_H]);

        // Groove clearance so rim doesn't hit the lid groove shoulder
        // (nothing to subtract here — open top handles it)
    }

    // Snap rim — rises from battery tray walls, slots into lid groove
    translate([wall, wall, BOT_H])
        rim_frame(IN_L, IN_W, snap_rim_h, snap_rim_t);
}

// ── Lid ──────────────────────────────────────────────────────────────
module lid() {
    difference() {
        union() {
            rbox(OUT_L, OUT_W, LID_H);

            // HUZZAH32 PCB locating pegs — hang from lid ceiling
            for (h = huz_holes)
                translate([wall + HUZ_OFF_L + h[0], wall + HUZ_OFF_W + h[1], LID_H - wall - pcb_peg_h])
                    cylinder(d = pcb_peg_d, h = pcb_peg_h);

            // BNO085 boss posts — hang from ceiling; PCB rests against boss tip, screw from inside
            for (h = bno_holes)
                translate([wall + BNO_OFF_L + h[0], wall + BNO_OFF_W + h[1], BNO_H + tol])
                    cylinder(d = bno_boss_d, h = LID_H - wall - BNO_H - tol);
        }

        // HUZZAH32 cavity (open bottom) — upright: depth=HUZ_W, Y-width=full header stack
        translate([wall + HUZ_OFF_L, wall + HUZ_OFF_W, -1])
            cube([HUZ_L + tol, HUZ_PIN_H + HUZ_PCB_H + HUZ_FEM_H + tol, HUZ_W + tol + 1]);

        // BNO085 cavity (open bottom)
        translate([wall + BNO_OFF_L, wall + BNO_OFF_W, -1])
            cube([BNO_L + tol, BNO_W + tol, BNO_H + tol + 1]);

        // Snap groove — frame-shaped recess at open bottom, accepts bottom shell rim
        translate([wall - snap_tol, wall - snap_tol, -1])
            rim_frame(
                IN_L + 2*snap_tol,
                IN_W + 2*snap_tol,
                snap_rim_h + 1,
                snap_rim_t + snap_tol
            );

        // USB-C cutout on right short face
        translate([OUT_L - wall - 0.1, usb_y, usb_z])
            cube([wall + 0.2, USB_W, USB_H]);

        // LED window on long face
        translate([wall + HUZ_OFF_L + 3, OUT_W - wall - 0.1, usb_z])
            cube([5, wall + 0.2, 3]);

        // BNO085 pilot holes — blind, from boss bottom upward (M2 screws from inside case)
        for (h = bno_holes)
            translate([wall + BNO_OFF_L + h[0], wall + BNO_OFF_W + h[1], BNO_H + tol - 0.1])
                cylinder(d = bno_screw_d, h = bno_pilot_dep + 0.1);
    }
}

// ── Dimensions ───────────────────────────────────────────────────────
echo(str("Outer footprint: ", OUT_L, " × ", OUT_W, " mm"));
echo(str("Bottom shell height: ", BOT_H, " mm"));
echo(str("Lid height:          ", LID_H, " mm"));
echo(str("Total assembled:     ", OUT_L, " × ", OUT_W, " × ", BOT_H + LID_H, " mm"));

// ── Render ───────────────────────────────────────────────────────────
if (show_shell) {
    % bottom();
    % translate([0, 0, BOT_H + 5]) lid();
}

if (show_components) {
    color("OrangeRed",    0.5) translate([wall, wall, wall])
        cube([BAT_L, BAT_W, BAT_H]);
    translate([wall + HUZ_OFF_L, wall + HUZ_OFF_W + HUZ_PCB_H + HUZ_FEM_H, wall + BAT_H + 1])
        rotate([90, 0, 0]) huzzah32();
    color("#1a1a1a") translate([wall + BNO_OFF_L, wall + BNO_OFF_W, BOT_H + 5 + wall + BNO_H])
        mirror([0, 0, 1])
            import("4754 BNO085 STEMMA QT.stl");
    // BNO085 boss posts — shown in component view for layout checking
    color("SteelBlue", 0.8)
        for (h = bno_holes)
            translate([wall + BNO_OFF_L + h[0], wall + BNO_OFF_W + h[1], BOT_H + 5 + wall + BNO_H + tol])
                cylinder(d = bno_boss_d, h = LID_H - wall - BNO_H - tol);
}
