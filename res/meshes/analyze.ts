/**
 * Report the structure and coordinate ranges of a HoverRace .msh actor mesh.
 *
 * Format is plain text:
 *   SEQUENCE / FRAME / PATCH / "<uRes> <vRes>" / uRes*vRes "<x> <y> <z>" / "<bitmap>"
 *
 * Usage: bun run analyze.ts <file.msh>
 */
const path = process.argv[2];
if (!path) {
	console.error("usage: bun run analyze.ts <file.msh>");
	process.exit(1);
}

const lines = (await Bun.file(path).text()).split("\n").map((l) => l.trim());

let sequences = 0;
let frames = 0;
const patches: { u: number; v: number; bitmap: string; count: number }[] = [];

const min = [Infinity, Infinity, Infinity];
const max = [-Infinity, -Infinity, -Infinity];

for (let i = 0; i < lines.length; i++) {
	const line = lines[i];
	if (line === "SEQUENCE") sequences++;
	else if (line === "FRAME") frames++;
	else if (line === "PATCH") {
		const [u, v] = lines[++i].split(/\s+/).map(Number);
		const count = u * v;
		for (let k = 0; k < count; k++) {
			const parts = lines[++i].split(/\s+/).map(Number);
			for (let a = 0; a < 3; a++) {
				if (parts[a] < min[a]) min[a] = parts[a];
				if (parts[a] > max[a]) max[a] = parts[a];
			}
		}
		patches.push({ u, v, bitmap: lines[++i], count });
	}
}

console.log(`file      : ${path}`);
console.log(`sequences : ${sequences}`);
console.log(`frames    : ${frames}`);
console.log(`patches   : ${patches.length} (${patches.length / frames} per frame)`);
console.log(`X range   : ${min[0]} .. ${max[0]}`);
console.log(`Y range   : ${min[1]} .. ${max[1]}`);
console.log(`Z range   : ${min[2]} .. ${max[2]}`);
console.log("\nper-frame patches:");
for (const p of patches.slice(0, patches.length / frames)) {
	console.log(`  ${String(p.u).padStart(3)} x ${String(p.v).padStart(2)}  ${p.bitmap}`);
}
