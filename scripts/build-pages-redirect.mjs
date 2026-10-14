import { copyFile, cp, mkdir, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

// Pages preserves old artifact URLs, but hosts no updater implementation.
const site = fileURLToPath(new URL('../site/', import.meta.url));
const output = `${site}dist`;
await rm(output, { recursive: true, force: true });
await mkdir(output);
await cp(`${site}public`, output, { recursive: true });
await copyFile(`${site}index.html`, `${output}/index.html`);
console.log('Built redirect to https://launchlab.beywatch.gg/update with preserved static artifacts.');
