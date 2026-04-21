import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { PDFDocument } from 'pdf-lib';
import { PDFProcessor } from '../../../src/core/pdf/PDFProcessor';
import { PDFLoadError, PDFValidationError } from '../../../src/utils/errors';

async function createPdf(pageCount: number): Promise<Uint8Array> {
  const doc = await PDFDocument.create();
  for (let i = 0; i < pageCount; i += 1) {
    doc.addPage();
  }
  return doc.save();
}

describe('PDFProcessor', () => {
  let folder: string;

  beforeEach(async () => {
    folder = await mkdtemp(join(tmpdir(), 'imposr-pdf-'));
  });

  afterEach(async () => {
    await rm(folder, { recursive: true, force: true });
  });

  it('processes a valid pdf file', async () => {
    const path = join(folder, 'sample.pdf');
    const bytes = await createPdf(2);
    await writeFile(path, bytes);

    const processor = new PDFProcessor();
    const result = await processor.processFile(path);

    expect(result.pageCount).toBe(2);
    expect(result.pdfSizeBytes).toBeGreaterThan(0);
  });

  it('throws on missing file', async () => {
    const processor = new PDFProcessor();

    await expect(processor.processFile(join(folder, 'missing.pdf'))).rejects.toBeInstanceOf(
      PDFLoadError
    );
  });


  it('throws on empty file path', async () => {
    const processor = new PDFProcessor();

    await expect(processor.processFile('   ')).rejects.toBeInstanceOf(PDFLoadError);
  });

  it('throws when page count is below minPages', async () => {
    const path = join(folder, 'min-pages.pdf');
    const bytes = await createPdf(1);
    await writeFile(path, bytes);

    const processor = new PDFProcessor();

    await expect(processor.processFile(path, { minPages: 2 })).rejects.toBeInstanceOf(
      PDFValidationError
    );
  });

  it('throws on invalid pdf bytes', async () => {
    const path = join(folder, 'invalid.pdf');
    await writeFile(path, 'not a pdf');

    const processor = new PDFProcessor();

    await expect(processor.processFile(path)).rejects.toBeInstanceOf(PDFValidationError);
  });

  it('throws when page count exceeds max', async () => {
    const path = join(folder, 'many.pdf');
    const bytes = await createPdf(3);
    await writeFile(path, bytes);

    const processor = new PDFProcessor();

    await expect(processor.processFile(path, { maxPages: 2 })).rejects.toBeInstanceOf(
      PDFValidationError
    );
  });
});
