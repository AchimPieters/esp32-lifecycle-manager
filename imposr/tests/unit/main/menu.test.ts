import { AppMenuBuilder } from '../../../src/main/menu';
import { ImposrError } from '../../../src/utils/errors';

describe('AppMenuBuilder', () => {
  it('builds a valid default menu', () => {
    const builder = new AppMenuBuilder();
    const menu = builder.buildDefault();

    expect(menu.app.length).toBeGreaterThan(0);
    expect(() => builder.validate(menu)).not.toThrow();
  });

  it('throws for invalid menu items', () => {
    const builder = new AppMenuBuilder();
    expect(() =>
      builder.validate({
        app: [{ label: '', action: 'x' }],
        file: [],
        help: []
      })
    ).toThrow(ImposrError);
  });
});
