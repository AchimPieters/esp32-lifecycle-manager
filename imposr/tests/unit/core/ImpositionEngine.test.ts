import { ImpositionEngine } from '../../../src/core/imposition/ImpositionEngine';

describe('ImpositionEngine', () => {
  const engine = new ImpositionEngine();

  it('creates a natural plan', () => {
    const plan = engine.createPlan(4, 'natural', { width: 1000, height: 700 });

    expect(plan.mode).toBe('natural');
    expect(plan.order).toEqual([1, 2, 3, 4]);
    expect(plan.layout.columns).toBe(2);
    expect(plan.layout.rows).toBe(2);
  });

  it('creates a booklet plan', () => {
    const plan = engine.createPlan(6, 'booklet', { width: 1000, height: 700 });

    expect(plan.mode).toBe('booklet');
    expect(plan.order).toEqual([8, 1, 2, 7, 6, 3, 4, 5]);
    expect(plan.layout.columns).toBe(2);
  });

  it('throws when page count is invalid', () => {
    expect(() => engine.createPlan(0, 'natural', { width: 1000, height: 700 })).toThrow(
      'Page count must be greater than zero'
    );
  });
});
