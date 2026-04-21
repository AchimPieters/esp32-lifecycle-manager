import { TemplateValidator } from '../../../src/core/templates/TemplateValidator';
import { TemplateLibrary } from '../../../src/core/templates/TemplateLibrary';
import { TemplateEngine } from '../../../src/core/templates/TemplateEngine';
import { TemplateError } from '../../../src/utils/errors';

const sample = {
  id: 'tpl-1',
  name: 'Template 1',
  mode: 'natural' as const,
  sheet: { width: 100, height: 100 }
};

describe('template modules', () => {
  it('validates template payload', () => {
    const validator = new TemplateValidator();
    expect(() => validator.validate(sample)).not.toThrow();
    expect(() => validator.validate({ ...sample, id: '' })).toThrow(TemplateError);
  });

  it('stores and resolves templates in library', () => {
    const library = new TemplateLibrary();
    library.add(sample);

    expect(library.get('tpl-1')).toEqual(sample);
    expect(library.list()).toHaveLength(1);
    expect(() => library.add(sample)).toThrow(TemplateError);
    expect(() => library.get('missing')).toThrow(TemplateError);
  });

  it('applies template through template engine', () => {
    const library = new TemplateLibrary();
    library.add(sample);

    const engine = new TemplateEngine(library);
    const plan = engine.apply('tpl-1', 4);

    expect(plan.mode).toBe('natural');
    expect(plan.order).toEqual([1, 2, 3, 4]);
  });
});
