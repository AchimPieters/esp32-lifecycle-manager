import { PDFValidationError } from '@utils/errors';

export interface BleedBox {
  readonly x: number;
  readonly y: number;
  readonly width: number;
  readonly height: number;
}

/**
 * Calculates bleed box expansions for a trim box.
 */
export class Bleed {
  /**
   * Expands dimensions by bleed amount on all sides.
   */
  public apply(trimWidth: number, trimHeight: number, bleedSize: number): BleedBox {
    if (trimWidth <= 0 || trimHeight <= 0 || bleedSize < 0) {
      throw new PDFValidationError('Invalid bleed parameters', {
        trimWidth,
        trimHeight,
        bleedSize
      });
    }

    return {
      x: -bleedSize,
      y: -bleedSize,
      width: trimWidth + bleedSize * 2,
      height: trimHeight + bleedSize * 2
    };
  }
}
