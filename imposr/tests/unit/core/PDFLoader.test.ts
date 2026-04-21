import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { PDFDocument } from 'pdf-lib';
import { PDFLoader } from '../../../src/core/pdf/PDFLoader';

describe('PDFLoader', () => {
  it('loads valid pdf', async () => {
    const folder = await mkdtemp(join(tmpdir(), 'loader-'));
    const path = join(folder, 'sample.pdf');
    const doc = await PDFDocument.create();
    doc.addPage();
    await writeFile(path, await doc.save());

    await expect(new PDFLoader().load(path)).resolves.toBeDefined();
    await rm(folder, { recursive: true, force: true });
  });

  it('throws on invalid path', async () => {
    await expect(new PDFLoader().load('')).rejects.toThrow();
    await expect(new PDFLoader().load('/missing.pdf')).rejects.toThrow();
  });
});
