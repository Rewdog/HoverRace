/**
 * Offline preview renderer for HoverRace .msh actor meshes.
 *
 * Rasterises a mesh from roughly the angle the in-game chase camera uses, so
 * proportions can be judged without recompiling the resource file and the
 * game. Writes a binary PPM; pipe it through ImageMagick for a PNG.
 *
 * Usage: bun run preview.ts <file.msh> [frameIndex] > out.ppm
 */

type Vec = [number, number, number];

interface Patch {
	u: number;
	v: number;
	verts: Vec[];
	bitmap: string;
}

// --- Parse ------------------------------------------------------------------

const file = process.argv[2];
const wantFrame = Number(process.argv[3] ?? 0);
if (!file) {
	console.error("usage: bun run preview.ts <file.msh> [frameIndex]");
	process.exit(1);
}

const lines = (await Bun.file(file).text()).split("\n").map((l) => l.trim());
const frames: Patch[][] = [];
let current: Patch[] | null = null;

for (let i = 0; i < lines.length; i++) {
	const line = lines[i];
	if (line === "FRAME") {
		current = [];
		frames.push(current);
	} else if (line === "PATCH" && current) {
		const [u, v] = lines[++i].split(/\s+/).map(Number);
		const verts: Vec[] = [];
		for (let k = 0; k < u * v; k++) {
			const [x, y, z] = lines[++i].split(/\s+/).map(Number);
			verts.push([x, y, z]);
		}
		current.push({ u, v, verts, bitmap: lines[++i] });
	}
}

const patches = frames[wantFrame] ?? frames[0];

// --- Camera -----------------------------------------------------------------

const W = 900;
const H = 560;

// Behind, above, slightly off-axis -- close to the in-game chase view.
const EYE: Vec = [-4200, -1500, 2400];
const TARGET: Vec = [-100, 0, 480];

const sub = (a: Vec, b: Vec): Vec => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const cross = (a: Vec, b: Vec): Vec =>
	[a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const dot = (a: Vec, b: Vec) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const norm = (a: Vec): Vec => {
	const l = Math.hypot(...a) || 1;
	return [a[0] / l, a[1] / l, a[2] / l];
};

const fwd = norm(sub(TARGET, EYE));
const right = norm(cross(fwd, [0, 0, 1]));
const up = cross(right, fwd);
const FOCAL = 900;

/** World -> screen. Returns null behind the camera. */
function project(p: Vec): { x: number; y: number; d: number } | null {
	const rel = sub(p, EYE);
	const d = dot(rel, fwd);
	if (d <= 1) return null;
	return {
		x: W / 2 + (dot(rel, right) / d) * FOCAL,
		y: H / 2 - (dot(rel, up) / d) * FOCAL,
		d,
	};
}

// --- Rasterise --------------------------------------------------------------

const buf = new Uint8Array(W * H * 3);
// Background gradient, so the silhouette is easy to read.
for (let y = 0; y < H; y++) {
	const t = y / H;
	for (let x = 0; x < W; x++) {
		const i = (y * W + x) * 3;
		buf[i] = 24 + 40 * t;
		buf[i + 1] = 30 + 46 * t;
		buf[i + 2] = 42 + 58 * t;
	}
}

/** Flat base colour per source bitmap, purely to tell parts apart. */
const palette: Record<string, [number, number, number]> = {
	"eon.bmp": [222, 48, 96],
	"metal_plate.bmp": [176, 186, 202],
	"cx_engine.bmp": [122, 132, 150],
	"helmet.bmp": [150, 214, 255],
	"basic_flame.bmp": [255, 176, 64],
	"31": [214, 132, 40],
};

interface Tri { p: { x: number; y: number }[]; d: number; c: [number, number, number]; }
const tris: Tri[] = [];

for (const patch of patches) {
	const base = palette[patch.bitmap] ?? [190, 190, 190];
	for (let j = 0; j < patch.v - 1; j++) {
		// Match the game exactly: it walks `lU < lURes - 1` and never wraps
		// around, so a ring only closes if the mesh repeats its first point.
		// Wrapping here would hide precisely the seams we need to catch.
		for (let i = 0; i < patch.u - 1; i++) {
			const i2 = i + 1;
			const a = patch.verts[j * patch.u + i];
			const b = patch.verts[j * patch.u + i2];
			const c = patch.verts[(j + 1) * patch.u + i2];
			const d = patch.verts[(j + 1) * patch.u + i];
			if (!a || !b || !c || !d) continue;

			const n = norm(cross(sub(b, a), sub(d, a)));
			// Simple headlamp shading plus ambient.
			const lit = 0.35 + 0.65 * Math.abs(dot(n, norm([0.4, -0.5, 0.75])));

			for (const quad of [[a, b, c], [a, c, d]]) {
				const pr = quad.map(project);
				if (pr.some((p) => p === null)) continue;
				const pts = pr as { x: number; y: number; d: number }[];
				tris.push({
					p: pts,
					d: (pts[0].d + pts[1].d + pts[2].d) / 3,
					c: [base[0] * lit, base[1] * lit, base[2] * lit],
				});
			}
		}
	}
}

// Painter's algorithm: far to near.
tris.sort((a, b) => b.d - a.d);

for (const t of tris) {
	const xs = t.p.map((p) => p.x);
	const ys = t.p.map((p) => p.y);
	const minX = Math.max(0, Math.floor(Math.min(...xs)));
	const maxX = Math.min(W - 1, Math.ceil(Math.max(...xs)));
	const minY = Math.max(0, Math.floor(Math.min(...ys)));
	const maxY = Math.min(H - 1, Math.ceil(Math.max(...ys)));

	const [p0, p1, p2] = t.p;
	const area = (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
	if (Math.abs(area) < 1e-6) continue;

	for (let y = minY; y <= maxY; y++) {
		for (let x = minX; x <= maxX; x++) {
			const w0 = ((p1.x - p0.x) * (y - p0.y) - (x - p0.x) * (p1.y - p0.y)) / area;
			const w1 = ((x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (y - p0.y)) / area;
			if (w0 < 0 || w1 < 0 || w0 + w1 > 1) continue;
			const i = (y * W + x) * 3;
			buf[i] = t.c[0];
			buf[i + 1] = t.c[1];
			buf[i + 2] = t.c[2];
		}
	}
}

process.stdout.write(`P6\n${W} ${H}\n255\n`);
process.stdout.write(buf);
