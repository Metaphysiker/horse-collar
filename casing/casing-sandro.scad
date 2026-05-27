module lid(){

    LID_SKIN = 2.5;   // top plate thickness (critical rigidity parameter)

    difference(){

        // ── Solid lid blank (structural cap) ───────────────
        rbox(OUT_L, OUT_W, LID_H);

        // ── Main internal clearance (BUT not full height) ──
        // Leaves a solid top skin
        translate([wall, wall, LID_SKIN])
            cube([
                IN_L,
                IN_W,
                LID_H - LID_SKIN + 1
            ]);

        // ── HUZZAH32 pocket (only where needed) ────────────
        translate([wall + HUZ_OFF_L,
                   wall + HUZ_OFF_W,
                   LID_SKIN])
            cube([
                HUZ_L + tol,
                HUZ_CAV_H,
                HUZ_W + tol
            ]);

        // ── BNO085 pocket ──────────────────────────────────
        translate([wall + BNO_OFF_L,
                   wall + BNO_OFF_W,
                   LID_SKIN])
            cube([
                BNO_L + tol,
                BNO_W + tol,
                BNO_H + tol
            ]);

        // ── Alignment post holes ───────────────────────────
        for (x=[10, OUT_L-10], y=[10, OUT_W-10])
            translate([x,y,-1])
                cylinder(d=post_fit, h=LID_SKIN + 20);

        // ── Screw clearance holes ──────────────────────────
        for (x=[10, OUT_L-10], y=[10, OUT_W-10])
            translate([x,y,-1])
                cylinder(d=screw_d, h=LID_H + 2);

        // ── USB-C cutout ───────────────────────────────────
        translate([OUT_L - wall - 0.1,
                   wall + HUZ_OFF_W + 8,
                   LID_SKIN + 2])
            cube([wall + 0.2, 10, 5]);

        // ── LED window ──────────────────────────────────────
        translate([wall + HUZ_OFF_L + 5,
                   OUT_W - wall - 0.1,
                   LID_SKIN + 2])
            cube([6, wall + 0.2, 3]);
    }
}