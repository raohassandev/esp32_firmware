// Automatrix ESP32 PV-DG Controller — Rev-A prototype DIN-rail enclosure
// Parametric source. Exact connector windows remain provisional until H2 placement freeze.
// Coordinate convention matches PCB generator: relay edge = PCB y=0 side,
// service/power/RS485/HMI edge = PCB y=max side, RJ45/USB = PCB x=max side.
// Units: mm. Designed for OpenSCAD preview/3D-print prototype, not injection tooling release.

$fn = 48;

// ---------- Master envelope ----------
outer_x = 158;
outer_y = 108;
outer_z = 48;
corner_r = 3;
wall = 2.2;
floor_t = 2.5;
lid_t = 2.2;
lid_overlap = 5;

// ---------- Working PCB ----------
pcb_x = 145;
pcb_y = 95;
pcb_z = 1.6;
pcb_origin_x = (outer_x - pcb_x)/2;
pcb_origin_y = (outer_y - pcb_y)/2;
standoff_h = 5;
standoff_od = 7;
standoff_id = 2.6;
pcb_hole_inset = 5; // KiCad H1..H4: (5,5),(140,5),(5,90),(140,90)

// ---------- Functional mechanical zones ----------
relay_zone_depth_pcb = 38;
service_window_x0 = 12;
service_window_x1 = 142;
relay_window_x0 = 10;
relay_window_x1 = 148;
window_z0 = 12;
window_z1 = 35;
network_window_y0 = 18;
network_window_y1 = 90;

// ---------- DIN clip mounting ----------
din_hole_spacing = 40;
din_hole_d = 3.4;

part = "assembly";
show_pcb = true;

module rounded_box(size=[10,10,10], r=2) {
    x=size[0]; y=size[1]; z=size[2];
    linear_extrude(height=z) offset(r=r) square([x-2*r,y-2*r], center=false);
}

module base_shell() {
    difference() {
        rounded_box([outer_x,outer_y,outer_z-lid_t], corner_r);
        translate([wall,wall,floor_t]) rounded_box([outer_x-2*wall, outer_y-2*wall, outer_z], max(0.8,corner_r-wall/2));
        // Relay/contact side = PCB y=0.
        translate([relay_window_x0,-0.5,window_z0]) cube([relay_window_x1-relay_window_x0, wall+1, window_z1-window_z0]);
        // SELV/service side = PCB y=max.
        translate([service_window_x0,outer_y-wall-0.5,window_z0]) cube([service_window_x1-service_window_x0, wall+1, window_z1-window_z0]);
        // RJ45 + USB-C + optional microSD = PCB x=max.
        translate([outer_x-wall-0.5,network_window_y0,10]) cube([wall+1, network_window_y1-network_window_y0, 27]);
    }
}

module standoff(x,y) {
    difference() {
        translate([x,y,floor_t]) cylinder(h=standoff_h, d=standoff_od);
        translate([x,y,floor_t-0.1]) cylinder(h=standoff_h+0.2, d=standoff_id);
    }
}

module pcb_standoffs() {
    hx = pcb_origin_x + pcb_hole_inset;
    hy = pcb_origin_y + pcb_hole_inset;
    hx2 = pcb_origin_x + pcb_x - pcb_hole_inset;
    hy2 = pcb_origin_y + pcb_y - pcb_hole_inset;
    standoff(hx,hy); standoff(hx2,hy); standoff(hx,hy2); standoff(hx2,hy2);
}

module isolation_rib() {
    rib_y = pcb_origin_y + relay_zone_depth_pcb + 2;
    translate([wall+4,rib_y,floor_t]) cube([outer_x-2*(wall+4),2,18]);
}

module din_clip_holes() {
    cx=outer_x/2; cy=outer_y/2;
    for(dx=[-din_hole_spacing/2,din_hole_spacing/2]) translate([cx+dx,cy,-0.1]) cylinder(h=floor_t+0.2,d=din_hole_d);
}

module base() {
    difference() {
        union() {
            base_shell(); pcb_standoffs(); isolation_rib();
            for(p=[[7,7],[outer_x-7,7],[7,outer_y-7],[outer_x-7,outer_y-7]])
                difference() {
                    translate([p[0],p[1],floor_t]) cylinder(h=outer_z-lid_t-floor_t-3,d=8);
                    translate([p[0],p[1],floor_t-0.1]) cylinder(h=outer_z,d=2.7);
                }
        }
        din_clip_holes();
    }
}

module lid() {
    lid_z = lid_t + lid_overlap;
    difference() {
        rounded_box([outer_x,outer_y,lid_z],corner_r);
        translate([wall,wall,-0.1]) rounded_box([outer_x-2*wall,outer_y-2*wall,lid_overlap+0.2],max(0.8,corner_r-wall/2));
        for(p=[[7,7],[outer_x-7,7],[7,outer_y-7],[outer_x-7,outer_y-7]]) translate([p[0],p[1],-0.1]) cylinder(h=lid_z+0.2,d=3.4);
        translate([35,38,lid_t-0.1]) cube([88,4,lid_t+0.2]);
    }
}

module pcb_placeholder() {
    if(show_pcb) color([0.05,0.45,0.12,0.65]) translate([pcb_origin_x,pcb_origin_y,floor_t+standoff_h]) cube([pcb_x,pcb_y,pcb_z]);
}
module assembly() {
    color([0.75,0.75,0.78,0.55]) base(); pcb_placeholder();
    color([0.82,0.82,0.85,0.35]) translate([0,0,outer_z-lid_t]) lid();
}
if(part=="base") base(); else if(part=="lid") lid(); else assembly();
