import { readFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { PDFDocument } from 'pdf-lib';
import { PDFLoadError, PDFValidationError } from '@utils/errors';

/**
 * Loads and parses PDF documents from disk.
 */
export class PDFLoader {
  /**
   * Reads a PDF file and returns a parsed `PDFDocument`.
   */
  public async load(filePath: string): Promise<PDFDocument> {
    if (!filePath.trim()) {
      throw new PDFLoadError('File path is required', filePath);
    }

    if (!existsSync(filePath)) {
      throw new PDFLoadError('PDF file does not exist', filePath);
    }

    try {
      const bytes = await readFile(filePath);
      return await PDFDocument.load(bytes);
    } catch (error) {
      throw new PDFValidationError('Failed to parse PDF document', {
        filePath,
        cause: (error as Error).message
      });
    }
  }
}
