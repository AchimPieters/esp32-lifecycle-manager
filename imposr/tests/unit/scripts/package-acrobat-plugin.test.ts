import { buildPluginPackageName } from '../../../scripts/deploy/package-acrobat-plugin';

describe('buildPluginPackageName', () => {
  it('builds deterministic package names', () => {
    expect(buildPluginPackageName({ pluginId: 'imposr-pro', version: '1.0.0', target: 'win' })).toBe(
      'imposr-pro-1.0.0-win.zip'
    );
    expect(buildPluginPackageName({ pluginId: 'imposr-pro', version: '1.0.0', target: 'mac' })).toBe(
      'imposr-pro-1.0.0-mac.zip'
    );
  });

  it('throws when required inputs are missing', () => {
    expect(() => buildPluginPackageName({ pluginId: ' ', version: '1.0.0', target: 'win' })).toThrow(
      'pluginId and version are required'
    );
    expect(() => buildPluginPackageName({ pluginId: 'imposr', version: ' ', target: 'mac' })).toThrow(
      'pluginId and version are required'
    );
  });
});
