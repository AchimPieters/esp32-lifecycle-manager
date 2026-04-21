import { LayoutCalculator } from '../../../src/core/imposition/LayoutCalculator';
import { PageArranger } from '../../../src/core/imposition/PageArranger';
import { ImpositionEngine } from '../../../src/core/imposition/ImpositionEngine';
import { PDFValidationError } from '../../../src/utils/errors';

describe('imposition core', () => {
  it('calculates valid grid layout', () => {
    const calc = new LayoutCalculator();
    const layout = calc.calculateGrid({ width: 100, height: 200 }, 4);

    expect(layout.columns).toBe(2);
    expect(layout.rows).toBe(2);
    expect(layout.cellWidth).toBe(50);
    expect(layout.cellHeight).toBe(100);
  });

  it('throws for invalid grid inputs', () => {
    const calc = new LayoutCalculator();
    expect(() => calc.calculateGrid({ width: 0, height: 100 }, 2)).toThrow(PDFValidationError);
    expect(() => calc.calculateGrid({ width: 100, height: 100 }, 0)).toThrow(PDFValidationError);
  });

  it('creates natural and booklet sequences', () => {
    const arranger = new PageArranger();
    expect(arranger.natural(3)).toEqual([1, 2, 3]);
    expect(arranger.booklet(8)).toEqual([8, 1, 2, 7, 6, 3, 4, 5]);
    expect(() => arranger.booklet(0)).toThrow(PDFValidationError);
  });

  it('creates an imposition plan', () => {
    const engine = new ImpositionEngine();
    const plan = engine.createPlan(8, 'booklet', { width: 595, height: 842 });

    expect(plan.mode).toBe('booklet');
    expect(plan.order.length).toBe(8);
    expect(plan.layout.columns).toBeGreaterThan(0);
  });
});
