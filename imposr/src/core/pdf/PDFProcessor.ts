import { existsSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { PDFDocument } from 'pdf-lib';
import { logger } from '@utils/logger';
import { PDFLoadError, PDFProcessingError, PDFValidationError } from '@utils/errors';

export interface ProcessOptions {
  readonly strictValidation?: boolean;
  readonly minPages?: number;
  readonly maxPages?: number;
}

export interface ProcessResult {
  readonly pageCount: number;
  readonly title?: string;
  readonly author?: string;
  readonly pdfSizeBytes: number;
}

/**
 * Service for loading, validating and collecting metadata from PDF files.
 */
export class PDFProcessor {
  /**
   * Processes a PDF from disk and returns normalized metadata.
   * @throws {PDFLoadError}
   * @throws {PDFValidationError}
   * @throws {PDFProcessingError}
   */
  public async processFile(filePath: string, options: ProcessOptions = {}): Promise<ProcessResult> {
    try {
      const bytes = await this.loadFile(filePath);
      const pdfDocument = await this.parseDocument(bytes, filePath);

      this.validateDocument(pdfDocument, {
        strictValidation: options.strictValidation ?? true,
        minPages: options.minPages ?? 1,
        maxPages: options.maxPages ?? 10000
      });

      const pageCount = pdfDocument.getPageCount();
      return {
        pageCount,
        title: pdfDocument.getTitle() ?? undefined,
        author: pdfDocument.getAuthor() ?? undefined,
        pdfSizeBytes: bytes.length
      };
    } catch (error) {
      if (
        error instanceof PDFLoadError ||
        error instanceof PDFValidationError ||
        error instanceof PDFProcessingError
      ) {
        throw error;
      }

      logger.error('Unexpected error in PDFProcessor.processFile', error as Error, {
        filePath,
        options
      });
      throw new PDFProcessingError('Unexpected error during PDF processing', {
        filePath,
        cause: (error as Error).message
      });
    }
  }

  private async loadFile(filePath: string): Promise<Uint8Array> {
    if (!filePath.trim()) {
      throw new PDFLoadError('PDF path cannot be empty', filePath);
    }

    if (!existsSync(filePath)) {
      throw new PDFLoadError('PDF file does not exist', filePath);
    }

    try {
      return await readFile(filePath);
    } catch (error) {
      logger.error('Unable to read PDF file', error as Error, { filePath });
      throw new PDFLoadError('Failed to read PDF file from disk', filePath, {
        cause: (error as Error).message
      });
    }
  }

  private async parseDocument(bytes: Uint8Array, filePath: string): Promise<PDFDocument> {
    try {
      return await PDFDocument.load(bytes, {
        ignoreEncryption: false,
        updateMetadata: false
      });
    } catch (error) {
      logger.error('PDF parsing failed', error as Error, { filePath });
      throw new PDFValidationError('Invalid or corrupted PDF content', {
        filePath,
        cause: (error as Error).message
      });
    }
  }

  private validateDocument(pdfDocument: PDFDocument, options: Required<ProcessOptions>): void {
    const pageCount = pdfDocument.getPageCount();

    if (pageCount < options.minPages) {
      throw new PDFValidationError('PDF has fewer pages than required minimum', {
        pageCount,
        minPages: options.minPages
      });
    }

    if (pageCount > options.maxPages) {
      throw new PDFValidationError('PDF exceeds supported page limit', {
        pageCount,
        maxPages: options.maxPages
      });
    }

    if (options.strictValidation && pageCount === 0) {
      throw new PDFValidationError('PDF must contain at least one page', {
        pageCount
      });
    }
  }
}
