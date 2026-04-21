import { LayoutCalculator, type GridLayout, type PageSize } from './LayoutCalculator';
import { PageArranger } from './PageArranger';

export type ImpositionMode = 'natural' | 'booklet';

export interface ImpositionPlan {
  readonly mode: ImpositionMode;
  readonly order: number[];
  readonly layout: GridLayout;
}

/**
 * Coordinates layout and arrangement algorithms into executable plans.
 */
export class ImpositionEngine {
  private readonly layoutCalculator = new LayoutCalculator();

  private readonly arranger = new PageArranger();

  /**
   * Creates an imposition plan for the provided mode.
   */
  public createPlan(pageCount: number, mode: ImpositionMode, sheet: PageSize): ImpositionPlan {
    const order = mode === 'booklet' ? this.arranger.booklet(pageCount) : this.arranger.natural(pageCount);
    const layout = this.layoutCalculator.calculateGrid(sheet, Math.min(order.length, 4));

    return { mode, order, layout };
  }
}
