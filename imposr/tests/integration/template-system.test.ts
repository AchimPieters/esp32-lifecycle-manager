import { TemplateLibrary } from '../../src/core/templates/TemplateLibrary';
import { TemplateEngine } from '../../src/core/templates/TemplateEngine';

describe('integration: template system', () => {
  it('adds, lists and applies templates', () => {
    const library = new TemplateLibrary();
    library.add({ id: 'booklet', name: 'Booklet', mode: 'booklet', sheet: { width: 595, height: 842 } });

    const items = library.list();
    const plan = new TemplateEngine(library).apply('booklet', 8);

    expect(items).toHaveLength(1);
    expect(plan.mode).toBe('booklet');
  });
});
