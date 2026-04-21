import { TemplateEngine } from '../../../src/core/templates/TemplateEngine';
import { TemplateLibrary } from '../../../src/core/templates/TemplateLibrary';

describe('TemplateEngine', () => {
  const createLibrary = (): TemplateLibrary => {
    const library = new TemplateLibrary();
    library.add({
      id: '2up-a4-a3',
      name: '2-Up A4 to A3',
      mode: 'natural',
      sheet: { width: 420, height: 297 }
    });
    return library;
  };

  it('applies a template and creates a plan', () => {
    const engine = new TemplateEngine(createLibrary());

    const plan = engine.apply('2up-a4-a3', 2);
    expect(plan.mode).toBe('natural');
    expect(plan.order).toEqual([1, 2]);
    expect(plan.layout.columns).toBe(2);
  });

  it('throws for missing template', () => {
    const engine = new TemplateEngine(createLibrary());

    expect(() => engine.apply('missing-template', 8)).toThrow('Template not found');
  });
});
