/**
 * Generate nova_craft.msh -- a new HoverRace craft actor.
 *
 * The .msh format is plain text and mirrors ResActor's structure:
 *
 *   SEQUENCE            (0 = motor off, 1 = motor on)
 *   FRAME               (seq 0 has one frame, seq 1 has two, for engine anim)
 *   PATCH
 *   <uRes> <vRes>       grid dimensions
 *   <x> <y> <z>         uRes*vRes vertices, row-major: vRes rows of uRes points
 *   <bitmap>            filename, or a numeric resource id
 *
 * Coordinates follow the original craft: +X is forward, +/-Y is lateral,
 * +Z is up, with the hull roughly within X -2500..1600, Y +/-1500, Z 0..1450.
 *
 * ---------------------------------------------------------------------------
 * Every FRAME stores a COMPLETE, independent set of vertices. The original
 * four craft spend all three frames only rescaling an exhaust cone -- but
 * nothing stops a frame from moving the airframe itself. So the Nova unfolds
 * under power: the wings sweep forward and spread, the nacelles flare outboard
 * and drop, and the exhaust blooms. Lift off the throttle and it folds back
 * into a dart.
 *
 * The whole airframe is driven by a single `thrust` parameter in 0..1, so the
 * folded and unfolded shapes can never drift out of sync.
 * ---------------------------------------------------------------------------
 *
 * Proportions are tuned for the chase camera, which sits behind the craft and
 * ~1700 above it: the wings ride above the hull centreline so they read as a
 * silhouette from that angle, and the nacelles sit low and outboard so they
 * frame the hull rather than hide it.
 *
 * Usage: bun run gen-nova.ts > nova_craft.msh
 */

type Vec = [number, number, number];

interface Patch {
	u: number;
	v: number;
	verts: Vec[];
	bitmap: string;
}

const r = (n: number) => Math.round(n);
const mix = (a: number, b: number, t: number) => a + (b - a) * t;

/** Smoothstep, so the unfold eases instead of snapping. */
const ease = (t: number) => t * t * (3 - 2 * t);

function patchText(p: Patch): string {
	if (p.verts.length !== p.u * p.v) {
		throw new Error(
			`patch ${p.bitmap}: expected ${p.u * p.v} verts, got ${p.verts.length}`);
	}
	const rows = p.verts.map((c) => `${r(c[0])} ${r(c[1])} ${r(c[2])}`);
	return ["PATCH", `${p.u} ${p.v}`, ...rows, p.bitmap, ""].join("\n");
}

interface Station {
	x: number;
	halfWidth: number;
	halfHeight: number;
	z: number;
}

/**
 * Closed tube: a ring of `u` segments at each station along X.
 *
 * The ring is emitted with u+1 points, the last repeating the first. The
 * renderer walks quads with `for (lU = 0; lU < lURes - 1; lU++)` and never
 * wraps around, so without that duplicate the surface is left with a slit
 * running its whole length -- which you see straight through in game.
 */
function sweep(u: number, stations: Station[], bitmap: string): Patch {
	const verts: Vec[] = [];
	for (const s of stations) {
		for (let i = 0; i <= u; i++) {
			const a = (2 * Math.PI * i) / u;
			verts.push([s.x, Math.cos(a) * s.halfWidth, s.z + Math.sin(a) * s.halfHeight]);
		}
	}
	return { u: u + 1, v: stations.length, verts, bitmap };
}

interface WingStation {
	y: number;
	xCenter: number;
	halfChord: number;
	z: number;
	halfThick: number;
}

/**
 * Wing as a closed volume, ringed around the chord and swept along the span.
 *
 * Every patch in the original craft is a closed surface -- there is not one
 * flat panel among them -- because the software renderer drops single-sided
 * geometry depending on which way it faces. A flat wing is invisible in game
 * even though it previews fine, so wings get real thickness here.
 */
function wingVolume(u: number, stations: WingStation[], bitmap: string): Patch {
	const verts: Vec[] = [];
	for (const s of stations) {
		for (let i = 0; i <= u; i++) {
			const a = (2 * Math.PI * i) / u;
			verts.push([
				s.xCenter + Math.cos(a) * s.halfChord,
				s.y,
				s.z + Math.sin(a) * s.halfThick,
			]);
		}
	}
	return { u: u + 1, v: stations.length, verts, bitmap };
}

/** Flat grid from four corners: root-fwd, tip-fwd, tip-aft, root-aft. */
function panel(u: number, v: number, c: [Vec, Vec, Vec, Vec], bitmap: string): Patch {
	const lerp = (a: Vec, b: Vec, t: number): Vec =>
		[mix(a[0], b[0], t), mix(a[1], b[1], t), mix(a[2], b[2], t)];
	const verts: Vec[] = [];
	for (let j = 0; j < v; j++) {
		const tv = v === 1 ? 0 : j / (v - 1);
		const inner = lerp(c[0], c[3], tv);
		const outer = lerp(c[1], c[2], tv);
		for (let i = 0; i < u; i++) {
			verts.push(lerp(inner, outer, u === 1 ? 0 : i / (u - 1)));
		}
	}
	return { u, v, verts, bitmap };
}

function translateY(p: Patch, dy: number): Patch {
	return { ...p, verts: p.verts.map((c): Vec => [c[0], c[1] + dy, c[2]]) };
}

/** Half-dome canopy, flattened along X into a teardrop. */
function dome(
	u: number, v: number,
	cx: number, cz: number,
	rx: number, ry: number, rz: number,
	bitmap: string,
): Patch {
	const verts: Vec[] = [];
	for (let j = 0; j < v; j++) {
		const phi = (Math.PI / 2) * (j / (v - 1));
		for (let i = 0; i <= u; i++) {
			const th = (2 * Math.PI * i) / u;
			verts.push([
				cx + Math.cos(phi) * Math.cos(th) * rx,
				Math.cos(phi) * Math.sin(th) * ry,
				cz + Math.sin(phi) * rz,
			]);
		}
	}
	return { u: u + 1, v, verts, bitmap };
}

/** Exhaust plume trailing from a nacelle. */
function flame(yOff: number, xBase: number, z: number, len: number, rad: number): Patch {
	const u = 6;
	const stations = [0, 0.45, 1];
	const verts: Vec[] = [];
	for (const t of stations) {
		const rr = rad * (1 - t) ** 0.7;
		for (let i = 0; i <= u; i++) {
			const a = (2 * Math.PI * i) / u;
			verts.push([xBase - len * t, yOff + Math.cos(a) * rr, z + Math.sin(a) * rr]);
		}
	}
	return { u: u + 1, v: stations.length, verts, bitmap: "basic_flame.bmp" };
}

/**
 * One complete frame of the airframe.
 * @param thrust 0 = folded and coasting, 1 = fully unfolded under power.
 */
function frame(thrust: number): Patch[] {
	const t = ease(thrust);

	// --- Hull: a flattened lifting body, wider than it is tall -------------
	// eon.bmp is MR_EON_COCKPIT, so this surface is swapped for the player's
	// colour at draw time -- the same trick the Eon uses. It's the broadest
	// thing the chase camera sees, which is what keeps players readable.
	const hull = sweep(
		12,
		[
			{ x: 1780, halfWidth: 40, halfHeight: 34, z: 515 },
			{ x: 1240, halfWidth: 190, halfHeight: 118, z: 518 },
			{ x: 520, halfWidth: 330, halfHeight: 180, z: 522 },
			{ x: -220, halfWidth: 350, halfHeight: 188, z: 522 },
			{ x: -940, halfWidth: 280, halfHeight: 152, z: 518 },
			{ x: -1520, halfWidth: 160, halfHeight: 100, z: 514 },
		],
		"eon.bmp",
	);

	// --- Wings: swept back when idle, forward and wide under power ---------
	// Held above the hull centreline so they stay in silhouette from behind.
	const tipX = mix(-880, 250, t);					  // sweeps forward
	const tipY = mix(1010, 1380, t);				  // and spreads outboard
	const tipZ = mix(650, 720, t);					  // rising slightly
	const wing = (side: 1 | -1): Patch =>
		wingVolume(
			8,
			[
				{ y: side * 300, xCenter: -85, halfChord: 690, z: 605, halfThick: 62 },
				{ y: side * mix(680, 830, t), xCenter: mix(-330, 40, t), halfChord: 520, z: mix(630, 665, t), halfThick: 50 },
				{ y: side * tipY, xCenter: tipX + 310, halfChord: 310, z: tipZ, halfThick: 34 },
			],
			// Bitmap 31 is what the Eon uses for its fins. It is dark, so the
			// wing silhouette still reads against the white track surface --
			// metal_plate is pale grey and simply disappeared against it.
			// Also deliberately NOT a cockpit bitmap, or the wings would be
			// recoloured per player along with the hull.
			"31",
		);

	// --- Nacelles: slung under the wingtips ---------------------------------
	// Anchored to the wing tip rather than placed independently, so they track
	// the sweep instead of drifting away from the airframe as it opens.
	const nacY = tipY;
	const nacZ = tipZ - 150;
	const nacNose = tipX + 560;
	const nacTail = tipX - 900;
	const nacelle = (side: 1 | -1): Patch =>
		translateY(
			sweep(
				8,
				[
					{ x: nacNose, halfWidth: 58, halfHeight: 58, z: nacZ },
					{ x: nacNose - 330, halfWidth: 122, halfHeight: 122, z: nacZ },
					{ x: nacTail + 340, halfWidth: 130, halfHeight: 130, z: nacZ },
					{ x: nacTail, halfWidth: 88, halfHeight: 88, z: nacZ },
				],
				"cx_engine.bmp",
			),
			side * nacY,
		);

	const canopy = dome(8, 4, 690, 520, 330, 235, 340, "helmet.bmp");

	const plume = mix(150, 900, thrust);
	const rad = mix(52, 108, thrust);

	return [
		hull,
		wing(1),
		wing(-1),
		nacelle(1),
		nacelle(-1),
		canopy,
		flame(nacY, nacTail, nacZ, plume, rad),
		flame(-nacY, nacTail, nacZ, plume, rad),
	];
}

const out: string[] = [];

// Sequence 0: coasting -- folded.
out.push("SEQUENCE", "FRAME");
for (const p of frame(0)) out.push(patchText(p));

// Sequence 1: under power -- unfolded, alternating frames so it pulses.
out.push("SEQUENCE", "FRAME");
for (const p of frame(0.82)) out.push(patchText(p));
out.push("FRAME");
for (const p of frame(1)) out.push(patchText(p));

console.log(out.join("\n"));
