// Generates the 3D-printable frame for the e-ink clock as STL files in ../frame/.
//
//   frame_front.stl   bezel that holds the panel (print front face down)
//   frame_back.stl    back cover with mounts for the electronics (print floor down)
// The case stands on its own on any flat edge, so it works in landscape or portrait.
//   layout.json       part positions, used by the 3D preview
//
// Usage (from this folder):  npm install   then   node make-frame.cjs
// All sizes are in millimetres. Change a value below and re-run to regenerate.

const fs = require('fs');
const path = require('path');
const { primitives, booleans, transforms, extrusions, measurements } = require('@jscad/modeling');
const stl = require('@jscad/stl-serializer');

const { cuboid, cylinder, roundedRectangle, polygon } = primitives;
const { union, subtract } = booleans;
const { translate, rotateX, mirrorX } = transforms;

// ---------------------------------------------------------------------------
// Parts (Waveshare 7.5" (B) V2 datasheet, checked against photos)
// ---------------------------------------------------------------------------
const PANEL = { w: 170.2, h: 111.2, t: 1.2 };
// Visible area and its border on each side. The ribbon (FPC) side is the bottom.
const ACTIVE = { w: 163.2, h: 97.9, left: 3.5, bottom: 9.5 };
// HAT mounted connectors-down, so the 40-pin socket (and the PWR jumper in it) faces the panel
const HAT = { w: 65, h: 30, holeDx: 58, holeDy: 23, holeInset: 3.5, standoff: 8 };
// lift leaves room under the board for the jumper plugs on its pins
const FIREBEETLE = { w: 60, h: 25.4, t: 1.6, lift: 24, usbH: 3.2 };
const BATTERY = { w: 92, h: 61, fence: 6 };   // bay for a ~6 x 60 x 90 mm flat LiPo

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
const SIDE = 12;        // frame width left/right of the panel (holds the LED channels)
const TB = 10;          // frame width above/below the panel
const R = 4;            // corner radius
const WALL = 2.4;       // outer wall thickness
const FRONT_PLATE = 1.6;
const LED_GAP = 5.5;    // space in front of the panel for the side LED strips (5 mm COB strip)
const LED_GROOVE = 2.5; // how far the LED channel cuts into the side walls
const FOAM = 1.0;       // foam gasket between panel back and the back cover's pressure posts
const TOL = 0.3;        // clearance around the panel
const BACK_DEPTH = 34;  // inside depth: fits plug-on jumper wires and the PWR jumper, no soldering
const FLOOR = 2.4;
const SCREW_INSET = 6;  // M3 screws, inset from the sides
const INSERT = { hole: 4.2, depth: 7 };  // for M3 x 6 heat-set inserts in the bezel
const OW = PANEL.w + 2 * SIDE;
const OH = PANEL.h + 2 * TB;
const PX0 = SIDE, PY0 = TB, PX1 = SIDE + PANEL.w, PY1 = TB + PANEL.h;
const AX0 = PX0 + ACTIVE.left, AY0 = PY0 + ACTIVE.bottom;
const AX1 = AX0 + ACTIVE.w, AY1 = AY0 + ACTIVE.h;
const PANEL_Z = FRONT_PLATE + LED_GAP;              // front of the panel
const FRONT_DEPTH = PANEL_Z + PANEL.t + FOAM + 0.7; // bezel depth
const SCREWS = [[SCREW_INSET, SCREW_INSET], [OW - SCREW_INSET, SCREW_INSET],
                [SCREW_INSET, OH - SCREW_INSET], [OW - SCREW_INSET, OH - SCREW_INSET]];
const SEG = 48;
// Box from corner coordinates.
const box = (x0, y0, z0, x1, y1, z1) =>
  cuboid({ size: [x1 - x0, y1 - y0, z1 - z0], center: [(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2] });
// Vertical cylinder (along z).
const cyl = (x, y, z0, z1, d) =>
  cylinder({ radius: d / 2, height: z1 - z0, center: [x, y, (z0 + z1) / 2], segments: SEG });
// Rounded-corner slab covering the whole frame outline.
const slab = (inset, z0, z1) =>
  translate([OW / 2, OH / 2, z0], extrusions.extrudeLinear({ height: z1 - z0 },
    roundedRectangle({ size: [OW - 2 * inset, OH - 2 * inset], roundRadius: Math.max(0.5, R - inset), segments: SEG })));

// ---------------------------------------------------------------------------
// Front bezel. z = 0 is the front face (print it face down).
// ---------------------------------------------------------------------------
function makeFront() {
  const cuts = [
    // Window onto the visible screen area
    box(AX0 - 0.3, AY0 - 0.3, -1, AX1 + 0.3, AY1 + 0.3, FRONT_PLATE + 0.01),
    // Light gap in front of the panel. Full panel width so side LEDs shine across it;
    // top and bottom stay narrow so the panel rests on those ledges.
    box(PX0 - TOL, AY0 - 0.3, FRONT_PLATE, PX1 + TOL, AY1 + 0.3, PANEL_Z + 0.01),
    // LED channels cut into the left and right walls
    box(PX0 - TOL - LED_GROOVE, AY0 - 0.3, FRONT_PLATE, PX0 - TOL + 0.01, AY1 + 0.3, PANEL_Z),
    box(PX1 + TOL - 0.01, AY0 - 0.3, FRONT_PLATE, PX1 + TOL + LED_GROOVE, AY1 + 0.3, PANEL_Z),
    // Panel pocket, open at the back
    box(PX0 - TOL, PY0 - TOL, PANEL_Z, PX1 + TOL, PY1 + TOL, FRONT_DEPTH + 1),
    // Notch where the panel's ribbon folds behind it (bottom centre)
    box((PX0 + PX1) / 2 - 22, PY0 - 6, PANEL_Z, (PX0 + PX1) / 2 + 22, PY0, FRONT_DEPTH + 1),
    // LED wire holes from the bottom of each channel to the back
    cyl(PX0 - TOL - 1.3, AY0 + 3, FRONT_PLATE, FRONT_DEPTH + 1, 3),
    cyl(PX1 + TOL + 1.3, AY0 + 7, FRONT_PLATE, FRONT_DEPTH + 1, 3),
    // Holes for M3 x 6 heat-set inserts (fitted by the print shop), screws go in from the back
    ...SCREWS.map(([x, y]) => cyl(x, y, FRONT_DEPTH - INSERT.depth, FRONT_DEPTH + 1, INSERT.hole)),
  ];
  return subtract(slab(0, 0, FRONT_DEPTH), ...cuts);
}

// ---------------------------------------------------------------------------
// Back cover. Modelled with z = 0 at the face that meets the bezel and the floor at
// z = BACK_DEPTH..BACK_DEPTH + FLOOR; flipped floor-down for printing at the end.
// ---------------------------------------------------------------------------
const FLOOR_Z = BACK_DEPTH;      // inside surface of the floor
const up = (h) => FLOOR_Z - h;   // z for a height h above the floor

const FB_X0 = 30, FB_Y0 = 21;                          // FireBeetle; room on its left for the USB-C plug
const FB_BOTTOM = up(FIREBEETLE.lift);                // underside of the board
// USB-C: a hole low on the back, next to the FireBeetle's USB end. The short L-shaped
// extension cable (~10 cm) runs out through it and its female end hangs outside.
// Big enough for the female end to pass through.
const PORT = { x: 20, y: 36, w: 15, h: 8 };
const HAT_X0 = 12, HAT_Y0 = 95;                       // HAT, top left
const HAT_HOLES = [[0, 0], [1, 0], [0, 1], [1, 1]].map(([i, j]) =>
  [HAT_X0 + HAT.holeInset + i * HAT.holeDx, HAT_Y0 + HAT.holeInset + j * HAT.holeDy]);
const BAT_X0 = 88, BAT_Y0 = 34;                       // battery bay, right of centre
const ADAPTER = { x0: 95, y0: 8, w: 25, h: 18 };      // ribbon adapter board (taped)
const SENSOR = { x0: OW - WALL - 22, y0: 99, w: 16, h: 20 }; // future SHT31, by the vents
const RTC = { x0: 118, y0: 99, w: 24, h: 20 };        // future DS3231 mini RTC (offline clock)
// Posts that press the panel into the bezel through the foam
const POSTS = [[20, 80], [55, 58], [55, 86], [130, 25], [176, 25], [97, 114], [152, 114]];
const POST_TIP = (PANEL_Z + PANEL.t + FOAM) - FRONT_DEPTH; // negative: reaches into the bezel

// A 3 mm fence around a small board, open on one side for its wires.
const pocket = (p) => subtract(
  box(p.x0 - 1.2, p.y0 - 1.2, up(3), p.x0 + p.w + 1.2, p.y0 + p.h + 1.2, FLOOR_Z),
  box(p.x0, p.y0, up(3) - 1, p.x0 + p.w, p.y0 + p.h, FLOOR_Z + 1),
  box(p.x0 - 2, p.y0 + p.h / 2 - 4, up(3) - 1, p.x0 + 1, p.y0 + p.h / 2 + 4, FLOOR_Z + 1));

function makeBack() {
  const shell = subtract(slab(0, 0, BACK_DEPTH + FLOOR), slab(WALL, -1, BACK_DEPTH));

  const adds = [
    // Screw bosses
    ...SCREWS.map(([x, y]) => cyl(x, y, 0, FLOOR_Z, 8)),
    // Pressure posts
    ...POSTS.map(([x, y]) => box(x - 5, y - 5, POST_TIP, x + 5, y + 5, FLOOR_Z)),
    // FireBeetle: support block under the USB end, with side cheeks to locate the board
    box(FB_X0, FB_Y0, FB_BOTTOM, FB_X0 + 10, FB_Y0 + FIREBEETLE.h, FLOOR_Z),
    box(FB_X0, FB_Y0 - 2.3, FB_BOTTOM - FIREBEETLE.t - 1.5, FB_X0 + 10, FB_Y0 - 0.3, FLOOR_Z),
    box(FB_X0, FB_Y0 + FIREBEETLE.h + 0.3, FB_BOTTOM - FIREBEETLE.t - 1.5, FB_X0 + 10,
        FB_Y0 + FIREBEETLE.h + 2.3, FLOOR_Z),
    // HAT standoffs
    ...HAT_HOLES.map(([x, y]) => cyl(x, y, up(HAT.standoff), FLOOR_Z, 5.5)),
    // Battery fence, with a gap on the left for the wires
    subtract(
      box(BAT_X0 - 1.6, BAT_Y0 - 1.6, up(BATTERY.fence), BAT_X0 + BATTERY.w + 1.6, BAT_Y0 + BATTERY.h + 1.6, FLOOR_Z),
      box(BAT_X0, BAT_Y0, up(BATTERY.fence) - 1, BAT_X0 + BATTERY.w, BAT_Y0 + BATTERY.h, FLOOR_Z + 1),
      box(BAT_X0 - 3, BAT_Y0 + BATTERY.h / 2 - 6, up(BATTERY.fence) - 1, BAT_X0 + 1, BAT_Y0 + BATTERY.h / 2 + 6, FLOOR_Z + 1),
    ),
    // Low pockets locating the RTC and sensor boards (stick them in with foam tape)
    pocket(RTC), pocket(SENSOR),
  ];

  const cuts = [
    // Screw holes with deep counterbores, so short M3 x 12-14 screws reach the bezel
    ...SCREWS.map(([x, y]) => cyl(x, y, -1, FLOOR_Z + FLOOR + 1, 3.4)),
    ...SCREWS.map(([x, y]) => cyl(x, y, 6, FLOOR_Z + FLOOR + 1, 6.4)),
    // HAT pilot holes (M2.5 self-tapping)
    ...HAT_HOLES.map(([x, y]) => cyl(x, y, up(HAT.standoff) - 1, FLOOR_Z - 1, 2.2)),
    // USB-C cable exit (slot with rounded ends)
    box(PORT.x - PORT.w / 2 + PORT.h / 2, PORT.y - PORT.h / 2, FLOOR_Z - 1, PORT.x + PORT.w / 2 - PORT.h / 2, PORT.y + PORT.h / 2, FLOOR_Z + FLOOR + 1),
    cyl(PORT.x - PORT.w / 2 + PORT.h / 2, PORT.y, FLOOR_Z - 1, FLOOR_Z + FLOOR + 1, PORT.h),
    cyl(PORT.x + PORT.w / 2 - PORT.h / 2, PORT.y, FLOOR_Z - 1, FLOOR_Z + FLOOR + 1, PORT.h),
    // Vents for the future indoor sensor, through the back so no standing edge blocks them
    ...[0, 1, 2, 3].map(i =>
      box(SENSOR.x0 + 2 + i * 3.5, SENSOR.y0 + 2, FLOOR_Z - 1, SENSOR.x0 + 3.8 + i * 3.5, SENSOR.y0 + SENSOR.h - 2, FLOOR_Z + FLOOR + 1)),
  ];

  return subtract(union(shell, ...adds), ...cuts);
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------
const outDir = path.join(__dirname, '..', 'frame');
fs.mkdirSync(outDir, { recursive: true });

function save(name, geom) {
  const data = stl.serialize({ binary: true }, geom);
  fs.writeFileSync(path.join(outDir, name), Buffer.concat(data.map(d => Buffer.from(d))));
  const cm3 = measurements.measureVolume(geom) / 1000;
  const [[x0, y0, z0], [x1, y1, z1]] = measurements.measureBoundingBox(geom);
  // FDM prints are partly hollow: ~50% of solid volume is a fair estimate with 3 walls, 20% infill
  console.log(`${name.padEnd(18)} ${(x1 - x0).toFixed(1)} x ${(y1 - y0).toFixed(1)} x ${(z1 - z0).toFixed(1)} mm, ` +
              `solid ${cm3.toFixed(0)} cm3, ~${(cm3 * 1.24 * 0.5).toFixed(0)} g printed`);
}

const front = makeFront();
const back = makeBack();


save('frame_front.stl', front);
// Flip so the floor is on the print bed
save('frame_back.stl', translate([0, OH, BACK_DEPTH + FLOOR], rotateX(Math.PI, back)));


// Assembled positions for the preview (same coordinates as the bezel, z towards the back)
const layout = {
  outer: { w: OW, h: OH, frontDepth: FRONT_DEPTH, backDepth: BACK_DEPTH + FLOOR },
  panel: { x0: PX0, y0: PY0, w: PANEL.w, h: PANEL.h, z: PANEL_Z, t: PANEL.t },
  active: { x0: AX0, y0: AY0, w: ACTIVE.w, h: ACTIVE.h },
  parts: [
    { name: 'FireBeetle', x0: FB_X0, y0: FB_Y0, w: FIREBEETLE.w, h: FIREBEETLE.h,
      z0: FRONT_DEPTH + FB_BOTTOM - FIREBEETLE.t, z1: FRONT_DEPTH + FB_BOTTOM, color: '#2b2b2b' },
    { name: 'HAT', x0: HAT_X0, y0: HAT_Y0, w: HAT.w, h: HAT.h,
      z0: FRONT_DEPTH + up(HAT.standoff) - 1.6, z1: FRONT_DEPTH + up(HAT.standoff), color: '#1d5fa8' },
    { name: 'Battery (future)', x0: BAT_X0 + 1, y0: BAT_Y0 + 1, w: BATTERY.w - 2, h: BATTERY.h - 2,
      z0: FRONT_DEPTH + FLOOR_Z - 6, z1: FRONT_DEPTH + FLOOR_Z, color: '#8a8f96' },
    { name: 'Ribbon adapter', x0: ADAPTER.x0, y0: ADAPTER.y0 + WALL, w: ADAPTER.w, h: ADAPTER.h,
      z0: FRONT_DEPTH + 2, z1: FRONT_DEPTH + 3.6, color: '#1d5fa8' },
    { name: 'Sensor (future)', x0: SENSOR.x0, y0: SENSOR.y0, w: SENSOR.w, h: SENSOR.h,
      z0: FRONT_DEPTH + FLOOR_Z - 4, z1: FRONT_DEPTH + FLOOR_Z - 2.4, color: '#6b3fa0' },
    { name: 'RTC (future)', x0: RTC.x0 + 2, y0: RTC.y0 + 2, w: RTC.w - 4, h: RTC.h - 4,
      z0: FRONT_DEPTH + FLOOR_Z - 9, z1: FRONT_DEPTH + FLOOR_Z, color: '#2f7d4f' },
    { name: 'USB-C cable', x0: PORT.x - 5, y0: PORT.y - 1.2, w: 10, h: 2.4,
      z0: FRONT_DEPTH + FLOOR_Z - 22, z1: FRONT_DEPTH + FLOOR_Z + FLOOR + 30, color: '#1e1f21' },
  ],

};
fs.writeFileSync(path.join(outDir, 'layout.json'), JSON.stringify(layout, null, 2));
console.log(`Frame outside: ${OW.toFixed(1)} x ${OH.toFixed(1)} mm, ` +
            `${(FRONT_DEPTH + BACK_DEPTH + FLOOR).toFixed(1)} mm deep`);
