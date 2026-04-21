import { PDFValidationError } from '@utils/errors';

/**
 * Builds page ordering arrays for supported imposition strategies.
 */
export class PageArranger {
  /**
   * Returns natural ascending sequence from 1..pageCount.
   */
  public natural(pageCount: number): number[] {
    if (pageCount <= 0) {
      throw new PDFValidationError('Page count must be greater than zero', { pageCount });
    }

    return Array.from({ length: pageCount }, (_, index) => index + 1);
  }

  /**
   * Returns booklet order pairs, padded to multiples of 4.
   */
  public booklet(pageCount: number): number[] {
    if (pageCount <= 0) {
      throw new PDFValidationError('Page count must be greater than zero', { pageCount });
    }

    const padded = Math.ceil(pageCount / 4) * 4;
    const result: number[] = [];
    let low = 1;
    let high = padded;

    while (low < high) {
      result.push(high, low, low + 1, high - 1);
      low += 2;
      high -= 2;
    }

    return result;
  }
}
