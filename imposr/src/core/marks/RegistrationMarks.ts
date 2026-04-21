import { PDFValidationError } from '@utils/errors';

export interface RegistrationMark {
  readonly x: number;
  readonly y: number;
  readonly radius: number;
}

/**
 * Computes registration mark positions around the sheet center lines.
 */
export class RegistrationMarks {
  /**
   * Generates four standard registration marks.
   */
  public generate(width: number, height: number, margin = 10): RegistrationMark[] {
    if (width <= 0 || height <= 0) {
      throw new PDFValidationError('Invalid page dimensions for registration marks', {
        width,
        height
      });
    }

    return [
      { x: width / 2, y: -margin, radius: 4 },
      { x: width / 2, y: height + margin, radius: 4 },
      { x: -margin, y: height / 2, radius: 4 },
      { x: width + margin, y: height / 2, radius: 4 }
    ];
  }
}
