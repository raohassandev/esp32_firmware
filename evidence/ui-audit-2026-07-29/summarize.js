const fs = require('fs');
const path = require('path');
const r = JSON.parse(fs.readFileSync(path.join(__dirname, 'out', 'report.json'), 'utf8'));

const byType = new Map();
const errs = [];
let runsOk = 0;

for (const run of r.runs) {
    if (run.error) { errs.push(`${run.viewport}/${run.theme}/${run.auth}/${run.route}: ${run.error}`); continue; }
    runsOk++;
    for (const i of run.issues || []) {
        if (!byType.has(i.type)) byType.set(i.type, []);
        byType.get(i.type).push({ ...i, where: `${run.viewport}/${run.theme}/${run.auth}/${run.route}` });
    }
    for (const c of run.console || []) {
        const t = `console-${c.type}`;
        if (!byType.has(t)) byType.set(t, []);
        byType.get(t).push({ detail: c.text, where: `${run.viewport}/${run.theme}/${run.auth}/${run.route}` });
    }
    for (const f of run.failed || []) {
        if (!byType.has('failed-request')) byType.set('failed-request', []);
        byType.get('failed-request').push({ detail: `${f.status || f.error} ${f.url}`, where: `${run.viewport}/${run.theme}/${run.auth}/${run.route}` });
    }
}

console.log(`runs completed: ${runsOk} / ${r.runs.length}`);
if (errs.length) { console.log(`\nRUN ERRORS (${errs.length}):`); errs.slice(0, 10).forEach((e) => console.log('  ' + e)); }

const sorted = [...byType.entries()].sort((a, b) => b[1].length - a[1].length);
console.log('\n===== ISSUE TYPES =====');
for (const [type, items] of sorted) console.log(`${String(items.length).padStart(5)}  ${type}`);

console.log('\n===== DETAIL (deduplicated) =====');
for (const [type, items] of sorted) {
    console.log(`\n### ${type}  (${items.length} occurrences)`);
    const seen = new Map();
    for (const i of items) {
        const key = `${i.selector || ''}|${i.detail || ''}`.slice(0, 160);
        if (!seen.has(key)) seen.set(key, { ...i, count: 0, wheres: new Set() });
        seen.get(key).count++;
        seen.get(key).wheres.add(i.where);
    }
    const uniq = [...seen.values()].sort((a, b) => b.count - a.count);
    for (const u of uniq.slice(0, 12)) {
        const w = [...u.wheres];
        console.log(`  [x${u.count}] ${u.selector || ''} ${u.detail || ''}`.trim().slice(0, 200));
        if (u.text) console.log(`         text: "${u.text}"`);
        console.log(`         seen: ${w.slice(0, 3).join(', ')}${w.length > 3 ? ` +${w.length - 3} more` : ''}`);
    }
    if (uniq.length > 12) console.log(`  ... and ${uniq.length - 12} more distinct`);
}
