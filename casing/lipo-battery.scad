// PKCell LP-785060-J LiPo holder
// Battery size: 60.5 x 50 x 7.8 mm

battery_x = 60.5;
battery_y = 50;
battery_z = 7.8;

clearance = 0.6;
wall = 2.0;
floor = 2.0;

holder_x = battery_x + clearance*2 + wall*2;
holder_y = battery_y + clearance*2 + wall*2;
holder_z = battery_z + floor + 3;

difference() {

    // Outer shell
    cube([holder_x, holder_y, holder_z], center=false);

    // Battery cavity
    translate([wall, wall, floor])
        cube([
            battery_x + clearance*2,
            battery_y + clearance*2,
            battery_z + 0.5
        ], center=false);

    // Cable exit notch
    translate([
        holder_x/2 - 5,
        -0.1,
        floor + battery_z - 2
    ])
    cube([10, wall + 0.2, 4], center=false);
}