import { mkdtemp, rm } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { existsSync } from 'node:fs';
import { PDFDocument } from 'pdf-lib';
import { PDFExporter } from '../../../src/core/pdf/PDFExporter';

describe('PDFExporter', () => {
  it('exports document to disk', async () => {
    const folder = await mkdtemp(join(tmpdir(), 'exporter-'));
    const path = join(folder, 'out.pdf');
    const doc = await PDFDocument.create();
    doc.addPage();

    await new PDFExporter().export(doc, path);

    expect(existsSync(path)).toBe(true);
    await rm(folder, { recursive: true, force: true });
  });

  it('throws on invalid output path', async () => {
    const doc = await PDFDocument.create();
    await expect(new PDFExporter().export(doc, '')).rejects.toThrow();
  });
});
