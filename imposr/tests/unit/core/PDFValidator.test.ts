import { PDFDocument } from 'pdf-lib';
import { PDFValidator } from '../../../src/core/pdf/PDFValidator';

describe('PDFValidator', () => {
  it('validates page ranges', async () => {
    const doc = await PDFDocument.create();
    doc.addPage();
    const validator = new PDFValidator();

    expect(() => validator.validate(doc, { minPages: 1, maxPages: 2 })).not.toThrow();
    expect(() => validator.validate(doc, { minPages: 2 })).toThrow();
  });
});
