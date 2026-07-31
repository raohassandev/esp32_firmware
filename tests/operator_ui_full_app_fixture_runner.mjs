#!/usr/bin/env node
import { readFileSync, unlinkSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';

const DIR = dirname(fileURLToPath(import.meta.url));
const sourcePath = join(DIR, 'operator_ui_full_app_fixture_test.mjs');
const runtimePath = join(DIR, '.operator_ui_full_app_fixture_runtime.mjs');
let source = readFileSync(sourcePath, 'utf8');

const oldOperator = `  const verdict = await evaluate(\`document.getElementById('plantVerdictValue')?.textContent || ''\`);
  assert(verdict === 'PLANT NORMAL', \`Full app verdict is \${verdict || '<missing>'}\`);
`;
const newOperator = `  const operatorVerdict = await evaluate(\`document.getElementById('plantVerdictValue')?.textContent || ''\`);
  assert(operatorVerdict === 'PLANT STATUS UNKNOWN', \`Signed-out operator verdict is \${operatorVerdict || '<missing>'}\`);
`;
if (source.includes(oldOperator)) source = source.replace(oldOperator, newOperator);
if (!source.includes(newOperator)) throw new Error('Could not apply signed-out source visibility expectation');

const oldEngineering = `  const access = await evaluate(\`document.documentElement.dataset.access\`);
  assert(access === 'engineering', \`Engineering login did not unlock the application (\${access})\`);
  await visit('control');
`;
const previousEngineering = `  const access = await evaluate(\`document.documentElement.dataset.access\`);
  assert(access === 'engineering', \`Engineering login did not unlock the application (\${access})\`);
  await visit('dashboard', 1440, 900, false);
  const engineeringVerdict = await evaluate(\`document.getElementById('plantVerdictValue')?.textContent || ''\`);
  assert(engineeringVerdict === 'PLANT NORMAL', \`Engineering dashboard verdict is \${engineeringVerdict || '<missing>'}\`);
  await visit('control');
`;
const newEngineering = `  const access = await evaluate(\`document.documentElement.dataset.access\`);
  assert(access === 'engineering', \`Engineering login did not unlock the application (\${access})\`);
  await visit('dashboard', 1440, 900, false);
  const engineeringDashboard = await evaluate(\`(() => ({
    verdictRailPresent: Boolean(document.getElementById('plantVerdictRail')),
    source: document.getElementById('sourceBannerLabel')?.textContent || ''
  }))()\`);
  assert(!engineeringDashboard.verdictRailPresent, 'Operator verdict rail remained in Engineering mode');
  assert(engineeringDashboard.source === 'GRID', \`Engineering source panel reports \${engineeringDashboard.source || '<missing>'}\`);
  await visit('control');
`;
if (source.includes(previousEngineering)) source = source.replace(previousEngineering, newEngineering);
else if (source.includes(oldEngineering)) source = source.replace(oldEngineering, newEngineering);
if (!source.includes(newEngineering)) throw new Error('Could not apply Engineering source visibility expectation');

const oldViewport = `        viewport: innerWidth
`;
const newViewport = `        viewport: innerWidth,
        topbarClipped: (() => {
          const topbar=document.querySelector('.topbar');
          const bounds=topbar?.getBoundingClientRect();
          if (!topbar || !bounds) return [];
          return [...topbar.querySelectorAll(':scope > *, .topbar-actions > *')]
            .filter((node) => {
              const style=getComputedStyle(node);
              if (style.display === 'none' || style.visibility === 'hidden') return false;
              const rect=node.getBoundingClientRect();
              const outside=rect.left < bounds.left - 1 || rect.right > bounds.right + 1;
              const cropped=node.scrollWidth > Math.ceil(rect.width) + 1;
              return outside || cropped;
            })
            .map((node) => node.id || node.className || node.tagName);
        })()
`;
if (source.includes(oldViewport)) source = source.replace(oldViewport, newViewport);
if (!source.includes(newViewport)) throw new Error('Could not add topbar clipping measurement');

const oldActiveAssertion = `    assert(state.active === route, \`${route} activated \${state.active || '<none>'}\`);
`;
const newActiveAssertion = `    assert(state.active === route, \`${route} activated \${state.active || '<none>'}\`);
    assert(state.topbarClipped.length === 0,
      \`${route} clips topbar controls at \${width}px: \${state.topbarClipped.join(', ')}\`);
`;
if (source.includes(oldActiveAssertion)) source = source.replace(oldActiveAssertion, newActiveAssertion);
if (!source.includes(newActiveAssertion)) throw new Error('Could not add topbar clipping assertion');

writeFileSync(runtimePath, source, 'utf8');
try {
  await import(`${pathToFileURL(runtimePath).href}?run=${Date.now()}`);
} finally {
  try { unlinkSync(runtimePath); } catch { /* best effort */ }
}
