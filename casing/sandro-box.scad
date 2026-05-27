// ============================================
// PARAMETRIC ELECTRONICS ENCLOSURE
// For: LiPo Battery + Arduino Feather Huzzah + BNO085
// ============================================

// --- USER PARAMETERS ---
// Outer dimensions of the box (mm)

bno_width = 25.6;
bno_depth = 22.7;
bno_height = 4.6;

// BNO085 mounting holes (detected from STL — all 2.50 mm from each edge, r=1.30 mm)
bno_stl_w    = 25.40;
bno_stl_d    = 22.86;
bno_hole_x1  = 2.50;
bno_hole_x2  = 22.90;
bno_hole_y1  = 2.50;
bno_hole_y2  = 20.36;
bno_hole_d   = 2.8;    // M2.5 clearance through lid

lipo_battery_width = 60.5;
lipo_battery_depth = 50;
lipo_battery_height = 7.8;
wiggle_room = 1;

HUZ_L = 57.1;   HUZ_W = 22.9;
HUZ_PCB_H  = 1.6;    // PCB thickness
HUZ_COMP_H = 9.0;    // tallest SMD component above PCB (USB-C port)
HUZ_PIN_H  = 12.0;   // male pin tail below PCB — measured from female jumper cable (~12 mm)
HUZ_FEM_H  = 12.0;   // female Dupont socket height above PCB — measured jumper cable
HDR_W      = 2.54;   // header strip width (one row of 2.54mm pitch)
HUZ_H = HUZ_PCB_H + max(HUZ_COMP_H, HUZ_PIN_H);


wall_thickness = 2;        // Wall thickness
floor_thickness = 2;       // Bottom floor thickness
lid_thickness = 3;         // Lid thickness

// Mounting posts for electronics
post_diameter = 4;
post_hole_diameter = 2;    // For M2 screws

screw_offset = wall_thickness + post_hole_diameter/2 + 0.5;  // post body merges into wall; screw hole stays clear

actual_post_diameter = post_diameter * 0.88;

box_width = lipo_battery_width + actual_post_diameter * 2 + wall_thickness * 2;
box_depth = lipo_battery_depth + wall_thickness * 2 + wiggle_room;

// BNO085 position on lid (depends on box_width / box_depth)
bno_lid_x = wall_thickness + actual_post_diameter + lipo_battery_depth/2 - bno_stl_w/2;  // mirrors place_bno() x
bno_y     = wall_thickness + HUZ_H + bno_width + 11;  // BNO y origin in box (mirrors place_bno())
bno_lid_y = box_depth - bno_y;                        // maps box y → lid local y (lid is y-flipped)

box_height = floor_thickness + lipo_battery_height + HUZ_W + lid_thickness;
post_height = box_height - floor_thickness;


// Lid parameters
lid_overlap = 2;           // How much lid overlaps the box walls
lip_height = 3;            // Height of the lip that fits inside
tolerance = 0.3;         // Gap between lid lip and box walls (printer tolerance)



// --- CALCULATED VALUES ---
inner_width = box_width - 2*wall_thickness;
inner_depth = box_depth - 2*wall_thickness;
inner_height = box_height - floor_thickness;




// --- MODULES ---

// Basic hollow box (the container)
module box_body() {
    difference() {
        // Solid outer block
        cube([box_width, box_depth, box_height]);
        
        // Hollow out the inside (leave floor)
        translate([wall_thickness, wall_thickness, floor_thickness])
            cube([inner_width, inner_depth, inner_height + 0.1]); // +0.1 to ensure cut through
        
        // Optional: Wire holes or vent holes can go here
        // Example: Small vent slot
        //translate([box_width/2, box_depth - wall_thickness - 1, box_height - 10])
        //    cube([10, wall_thickness+10, 5]);
        
        translate([0 -1, box_depth*0.2 - wall_thickness + 2, box_height - 19.5])
            cube([5, 5, 10]);
    }
}


module battery_body() {
    cube([lipo_battery_width, lipo_battery_depth, lipo_battery_height]);
}

// Mounting post with screw hole
module mounting_post(x, y) {
    difference() {
        // Post body
        translate([x, y, floor_thickness])
            cylinder(h = post_height, d = post_diameter, $fn=20);
        
        // Screw hole
        translate([x, y, floor_thickness])
            cylinder(h = post_height + 0.1, d = post_hole_diameter, $fn=20);
    }
}

module huzzah32() {
    // STL (offset corrected — model origin is at x=−0.56)
    color("ForestGreen", 0.8)
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

module lid() {

    screw_clearance = 2.4;

    // Diameter of screw head recess
    countersink_diameter = 5.5;

    // Depth of countersink
    countersink_depth = 1.4;

    difference() {

        // Flat lid plate
        cube([box_width, box_depth, lid_thickness]);

        // ---- FRONT LEFT ----
        translate([
            screw_offset,
            screw_offset,
            -0.1
        ])
        countersunk_hole();

        // ---- FRONT RIGHT ----
        translate([
            box_width - screw_offset,
            screw_offset,
            -0.1
        ])
        countersunk_hole();

        // ---- BACK LEFT ----
        translate([
            screw_offset,
            box_depth - screw_offset,
            -0.1
        ])
        countersunk_hole();

        // ---- BACK RIGHT ----
        translate([
            box_width - screw_offset,
            box_depth - screw_offset,
            -0.1
        ])
        countersunk_hole();

        // ---- BNO085 mounting holes (M2.5 countersunk) ----
        translate([bno_lid_x + bno_hole_x1, bno_lid_y + bno_hole_y1, -0.1])
            bno_countersunk_hole();
        translate([bno_lid_x + bno_hole_x2, bno_lid_y + bno_hole_y1, -0.1])
            bno_countersunk_hole();
        translate([bno_lid_x + bno_hole_x1, bno_lid_y + bno_hole_y2, -0.1])
            bno_countersunk_hole();
        translate([bno_lid_x + bno_hole_x2, bno_lid_y + bno_hole_y2, -0.1])
            bno_countersunk_hole();
    }

    // M2.5 countersunk hole for BNO085 board
    module bno_countersunk_hole() {
        union() {
            cylinder(h = lid_thickness + 0.2, d = bno_hole_d, $fn = 32);
            cylinder(h = countersink_depth, d1 = 5.0, d2 = bno_hole_d, $fn = 32);
        }
    }

    // Helper module
    module countersunk_hole() {

        union() {

            // Through-hole
            cylinder(
                h = lid_thickness + 0.2,
                d = screw_clearance,
                $fn = 40
            );

            // Countersink cone
            cylinder(
                h = countersink_depth,
                d1 = countersink_diameter,
                d2 = screw_clearance,
                $fn = 40
            );
        }
    }
}



module place_huzzah() {
    translate([wall_thickness + actual_post_diameter, wall_thickness + HUZ_H + wiggle_room/2, floor_thickness + lipo_battery_height])
    rotate([90, 0, 0])
        huzzah32();
}



module place_bno() {
    translate([wall_thickness + actual_post_diameter + lipo_battery_depth/2 - bno_width/2, wall_thickness + HUZ_H + bno_width + 11, floor_thickness + lipo_battery_height + HUZ_W])
        rotate([180,0,0])
            import("4754 BNO085 STEMMA QT.stl");
}

module place_battery() {
    color("blue")
    translate([wall_thickness + actual_post_diameter, wall_thickness + wiggle_room /2, floor_thickness])
        battery_body();
    
}

// --- ASSEMBLY ---

// Render the box (comment out lid to see inside)
//color("LightGray", 0.4)   // 0.4 = 40% opaque, 60% transparent
box_body();

// Lid on top for comparison — swap comments to switch back to print layout
// % = ghost mode: transparent grey, shows everything underneath
%translate([0, box_depth, box_height + lid_thickness])
    rotate([180, 0, 0])
        lid();

// Print layout (side by side):
//translate([0, 2 * box_depth + 10, lid_thickness])
//    rotate([180, 0, 0])
//        lid();

place_huzzah();
place_bno();
place_battery();

echo(str("box hoehe: ", box_height));
echo(str("box width: ", box_width));
echo(str("box depth: ", box_depth));



// Add mounting posts at corners (adjust positions for your components)
// Format: mounting_post(x, y)
mounting_post(screw_offset, screw_offset);                          // Front-left
mounting_post(box_width - screw_offset, screw_offset);              // Front-right
mounting_post(screw_offset, box_depth - screw_offset);              // Back-left
mounting_post(box_width - screw_offset, box_depth - screw_offset);  // Back-right

// Add a central post for the BNO085
//mounting_post(box_width/2, box_depth/2);

// --- LID (separate for printing) ---
// Uncomment this section to render the lid instead of the box
/*
translate([0, box_depth + 10, 0])
    lid();
*/

// ============================================
// INSTRUCTIONS:
// 1. Measure your components and adjust box dimensions
// 2. Position mounting posts where your boards have holes
// 3. Print box and lid separately (lid flipped upside down)
// 4. The lip provides a friction fit; add tape if too loose
// ============================================