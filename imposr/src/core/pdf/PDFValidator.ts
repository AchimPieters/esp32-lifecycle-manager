import { PDFDocument } from 'pdf-lib';
import { PDFValidationError } from '@utils/errors';

export interface ValidationOptions {
  readonly minPages?: number;
  readonly maxPages?: number;
}

/**
 * Validates PDF document constraints.
 */
export class PDFValidator {
  /**
   * Validates page count boundaries.
   */
  public validate(document: PDFDocument, options: ValidationOptions = {}): void {
    const pageCount = document.getPageCount();
    const minPages = options.minPages ?? 1;
    const maxPages = options.maxPages ?? 10000;

    if (pageCount < minPages) {
      throw new PDFValidationError('PDF has fewer pages than minimum', { pageCount, minPages });
    }

    if (pageCount > maxPages) {
      throw new PDFValidationError('PDF exceeds maximum page limit', { pageCount, maxPages });
    }
  }
}
