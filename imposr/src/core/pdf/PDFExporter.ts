import { writeFile } from 'node:fs/promises';
import { PDFDocument } from 'pdf-lib';
import { PDFProcessingError } from '@utils/errors';

/**
 * Exports processed PDF documents to disk.
 */
export class PDFExporter {
  /**
   * Saves the provided PDF to destination path.
   */
  public async export(document: PDFDocument, outputPath: string): Promise<void> {
    if (!outputPath.trim()) {
      throw new PDFProcessingError('Output path is required');
    }

    try {
      const bytes = await document.save();
      await writeFile(outputPath, bytes);
    } catch (error) {
      throw new PDFProcessingError('Failed to export PDF', {
        outputPath,
        cause: (error as Error).message
      });
    }
  }
}
