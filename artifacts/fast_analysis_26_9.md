# Pitch-Axis Counterweight Balancing Mechanism (Cycle 26)

This is a parametric OpenSCAD module for a camera-cradle pitch counterweight. It has three main parts:

- **Pitch hub**: a clamping hub on the pitch shaft.
- **Graduated arm**: a slotted counterweight arm with engraved scale ticks.
- **Slotted lead weights**: stackable weights you can add or remove without taking the clamp bolt out.

## How it works

1. **Hub.** A split clamp hub grips the pitch shaft. A pinch bolt closes the split.
2. **Arm.** The arm runs rearward (toward −X), opposite the camera cradle stub. A long through-slot runs along it, and tick marks every 5 mm (longer every 10 mm) help you reproduce a balance setting.
3. **Bottom weight (saddle).** Each weight station starts with a saddle weight. Its underside channel straddles the arm, so it cannot spin around the bolt.
4. **Upper weights.** Flat slotted weights stack on top. Each one has an open U-slot, so you can slide it on or off the clamp bolt after loosening the thumb nut.
5. **Clamp bolt.** A carriage-style bolt rides in the arm slot. A thumb nut on top locks the stack at any position along the slot.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle Counterweight Mechanism  —  Cycle 26
//  Slotted, sliding, stackable lead weights on a graduated arm
// =====================================================================
$fn = 48;

// ---------------------------------------------------------------------
// TOP-LEVEL MODULE
// ---------------------------------------------------------------------
module pitch_counterweight(
    pitch_angle  = 0,        // deg, rotation about pitch (Y) axis
    // pitch shaft / hub
    axis_d       = 8,        // pitch shaft diameter
    hub_d        = 28,
    hub_len      = 22,
    clamp_gap    = 1.2,      // split-clamp gap
    // counterweight arm
    arm_len      = 130,      // from axis center to arm tip
    arm_w        = 24,
    arm_t        = 6,
    slot_w       = 6.4,      // M6 clearance
    slot_margin  = 10,
    // weights
    wt_len       = 20,       // along arm (X)
    wt_w         = 36,       // across arm (Y)
    wt_h         = 10,       // each weight plate thickness
    skirt        = 3,        // saddle channel depth
    wt_pos       = [70, 105],// station distances from axis (mm)
    wt_stack     = [2, 3],   // number of plates at each station
    bolt_d       = 6,
    // camera side
    show_cradle_stub = true,
    show_weights     = true,
    explode          = 0     // vertical explode spacing for weights
) {
    slot_start = hub_d/2 + slot_margin;
    slot_end   = arm_len - slot_margin;

    // ---- sanity checks ----
    assert(len(wt_pos) == len(wt_stack), "wt_pos and wt_stack length mismatch");
    assert(wt_w > arm_w + 4, "weights must be wider than arm for saddle skirts");
    assert(slot_w > bolt_d, "slot must clear bolt");
    for (p = wt_pos)
        assert(p - wt_len/2 >= slot_start - wt_len/2 && p <= slot_end,
               str("weight station ", p, " outside slot travel"));

    rotate([0, pitch_angle, 0]) {
        color("SteelBlue") pitch_hub(axis_d, hub_d, hub_len, clamp_gap);
        color("LightSteelBlue")
            cw_arm(hub_d, arm_len, arm_w, arm_t, slot_w, slot_start, slot_end);

        if (show_cradle_stub)
            color("DimGray") cradle_stub(hub_d, hub_len, arm_w);

        if (show_weights)
            for (i = [0 : len(wt_pos)-1])
                translate([-wt_pos[i], 0, 0])
                    weight_station(wt_stack[i], wt_len, wt_w, wt_h, skirt,
                                   arm_w, arm_t, bolt_d, explode);
    }
}

// ---------------------------------------------------------------------
// PITCH HUB  (split clamp on the pitch shaft, axis along Y)
// ---------------------------------------------------------------------
module pitch_hub(axis_d, hub_d, hub_len, gap) {
    difference() {
        union() {
            rotate([90, 0, 0]) cylinder(d = hub_d, h = hub_len, center = true);
            // clamp ears
            translate([hub_d/2 - 2, 0, 0])
                cube([12, hub_len, 14], center = true);
        }
        // shaft bore
        rotate([90, 0, 0]) cylinder(d = axis_d + 0.2, h = hub_len + 2, center = true);
        // split
        translate([hub_d/4 + 4, 0, 0])
            cube([hub_d/2 + 12, hub_len + 2, gap], center = true);
        // pinch bolt (M3, vertical through ears)
        translate([hub_d/2 + 1, 0, 0])
            cylinder(d = 3.4, h = 20, center = true);
    }
}

// ---------------------------------------------------------------------
// COUNTERWEIGHT ARM  (extends toward -X, slotted, graduated)
// ---------------------------------------------------------------------
module cw_arm(hub_d, arm_len, arm_w, arm_t, slot_w, s0, s1) {
    difference() {
        hull() {
            translate([-hub_d/4, 0, 0]) cube([1, arm_w, arm_t], center = true);
            translate([-arm_len, 0, 0]) cylinder(d = arm_w, h = arm_t, center = true);
        }
        // sliding slot
        hull() {
            translate([-s0, 0, 0]) cylinder(d = slot_w, h = arm_t + 2, center = true);
            translate([-s1, 0, 0]) cylinder(d = slot_w, h = arm_t + 2, center = true);
        }
        // graduation ticks on top face (every 5 mm, long every 10 mm)
        for (x = [ceil(s0/5)*5 : 5 : s1]) {
            L = (x % 10 == 0) ? 5 : 2.5;
            translate([-x, arm_w/2 - L/2 + 0.01, arm_t/2 - 0.4])
                cube([0