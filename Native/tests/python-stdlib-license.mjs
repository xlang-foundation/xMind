import fs from 'node:fs/promises';
import path from 'node:path';

export async function findPythonStdlibLicense(stdlibSource) {
  const root = path.dirname(stdlibSource);
  const candidates = [
    path.join(root, 'LICENSE'),
    path.join(root, 'LICENSE.txt'),
    path.join(root, 'licenses', 'Python-STDLIB-LICENSE'),
  ];

  for (const candidate of candidates) {
    try {
      await fs.access(candidate);
      return candidate;
    } catch (error) {
      if (error?.code !== 'ENOENT') throw error;
    }
  }

  throw new Error('Python standard-library license was not found beside the pure source tree.');
}
