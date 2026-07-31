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
const newEngineering = `  const access = await evaluate(\`document.documentElement.dataset.access\`);
  assert(access === 'engineering', \`Engineering login did not unlock the application (\${access})\`);
  await visit('dashboard', 1440, 900, false);
  const engineeringVerdict = await evaluate(\`document.getElementById('plantVerdictValue')?.textContent || ''\`);
  assert(engineeringVerdict === 'PLANT NORMAL', \`Engineering dashboard verdict is \${engineeringVerdict || '<missing>'}\`);
  await visit('control');
`;
if (source.includes(oldEngineering)) source = source.replace(oldEngineering, newEngineering);
if (!source.includes(newEngineering)) throw new Error('Could not apply Engineering source visibility expectation');

writeFileSync(runtimePath, source, 'utf8');
try {
  await import(`${pathToFileURL(runtimePath).href}?run=${Date.now()}`);
} finally {
  try { unlinkSync(runtimePath); } catch { /* best effort */ }
}
