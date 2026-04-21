import { PDFValidationError } from '@utils/errors';

export interface MarkLine {
  readonly x1: number;
  readonly y1: number;
  readonly x2: number;
  readonly y2: number;
}

/**
 * Generates crop mark vectors around a rectangular page.
 */
export class CropMarks {
  /**
   * Creates corner crop marks with fixed offset and length.
   */
  public generate(width: number, height: number, offset = 6, length = 12): MarkLine[] {
    if (width <= 0 || height <= 0) {
      throw new PDFValidationError('Invalid page dimensions for crop marks', { width, height });
    }

    return [
      { x1: -offset, y1: 0, x2: -offset - length, y2: 0 },
      { x1: 0, y1: -offset, x2: 0, y2: -offset - length },
      { x1: width + offset, y1: 0, x2: width + offset + length, y2: 0 },
      { x1: width, y1: -offset, x2: width, y2: -offset - length },
      { x1: -offset, y1: height, x2: -offset - length, y2: height },
      { x1: 0, y1: height + offset, x2: 0, y2: height + offset + length },
      { x1: width + offset, y1: height, x2: width + offset + length, y2: height },
      { x1: width, y1: height + offset, x2: width, y2: height + offset + length }
    ];
  }
}
