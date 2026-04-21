import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { PDFDocument } from 'pdf-lib';
import { PDFProcessor } from '../../src/core/pdf/PDFProcessor';
import { TemplateLibrary } from '../../src/core/templates/TemplateLibrary';
import { TemplateEngine } from '../../src/core/templates/TemplateEngine';

describe('integration: pdf workflow', () => {
  it('processes a pdf and applies template plan', async () => {
    const folder = await mkdtemp(join(tmpdir(), 'imposr-int-'));
    const path = join(folder, 'sample.pdf');

    const doc = await PDFDocument.create();
    doc.addPage();
    doc.addPage();
    await writeFile(path, await doc.save());

    const processor = new PDFProcessor();
    const meta = await processor.processFile(path);

    const library = new TemplateLibrary();
    library.add({ id: 'tpl', name: 'Tpl', mode: 'natural', sheet: { width: 100, height: 100 } });
    const plan = new TemplateEngine(library).apply('tpl', meta.pageCount);

    expect(meta.pageCount).toBe(2);
    expect(plan.order).toEqual([1, 2]);

    await rm(folder, { recursive: true, force: true });
  });
});
