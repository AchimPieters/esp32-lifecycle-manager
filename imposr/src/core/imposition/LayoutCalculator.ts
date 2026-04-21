import { PDFValidationError } from '@utils/errors';

export interface PageSize {
  readonly width: number;
  readonly height: number;
}

export interface GridLayout {
  readonly columns: number;
  readonly rows: number;
  readonly cellWidth: number;
  readonly cellHeight: number;
}

/**
 * Calculates geometric layout values for imposition operations.
 */
export class LayoutCalculator {
  /**
   * Computes grid layout for placing `cells` on a target sheet.
   */
  public calculateGrid(sheet: PageSize, cells: number): GridLayout {
    if (cells <= 0) {
      throw new PDFValidationError('Cells must be greater than zero', { cells });
    }

    if (sheet.width <= 0 || sheet.height <= 0) {
      throw new PDFValidationError('Sheet dimensions must be positive', { sheet });
    }

    const columns = Math.ceil(Math.sqrt(cells));
    const rows = Math.ceil(cells / columns);

    return {
      columns,
      rows,
      cellWidth: sheet.width / columns,
      cellHeight: sheet.height / rows
    };
  }
}
