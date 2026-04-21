import { mkdtemp, readFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import {
  buildPluginManifest,
  buildPluginPackageName,
  stagePluginPackage
} from '../../../scripts/deploy/package-acrobat-plugin';

describe('package-acrobat-plugin', () => {
  it('builds deterministic package names', () => {
    expect(buildPluginPackageName({ pluginId: 'imposr-pro', version: '1.0.0', target: 'win' })).toBe(
      'imposr-pro-1.0.0-win.zip'
    );
    expect(buildPluginPackageName({ pluginId: 'imposr-pro', version: '1.0.0-beta.1', target: 'mac' })).toBe(
      'imposr-pro-1.0.0-beta.1-mac.zip'
    );
  });

  it('throws when inputs are invalid', () => {
    expect(() => buildPluginPackageName({ pluginId: ' ', version: '1.0.0', target: 'win' })).toThrow(
      'pluginId and version are required'
    );
    expect(() => buildPluginPackageName({ pluginId: 'imposr', version: 'nope', target: 'mac' })).toThrow(
      'version must follow semver format'
    );
    expect(() =>
      buildPluginManifest({
        pluginId: 'imposr',
        version: '1.0.0',
        target: 'win',
        displayName: ' ',
        minAcrobatVersion: '2023',
        bridgeEntry: 'dist/main/acrobat.js'
      })
    ).toThrow('displayName is required');
  });

  it('creates manifest and payload index files', async () => {
    const manifest = buildPluginManifest(
      {
        pluginId: 'imposr-pro',
        version: '1.0.0',
        target: 'win',
        displayName: 'Imposr Pro',
        minAcrobatVersion: '2023',
        bridgeEntry: 'dist/main/acrobat.js'
      },
      '2026-04-21T00:00:00.000Z'
    );

    const outputRoot = await mkdtemp(join(tmpdir(), 'imposr-package-'));
    const packageDir = await stagePluginPackage(outputRoot, manifest, ['dist/main/acrobat.js', 'templates/standard']);

    const savedManifest = JSON.parse(await readFile(join(packageDir, 'manifest.json'), 'utf8')) as {
      id: string;
      generatedAt: string;
    };
    const payloadIndex = JSON.parse(await readFile(join(packageDir, 'payload-files.json'), 'utf8')) as string[];

    expect(savedManifest.id).toBe('imposr-pro');
    expect(savedManifest.generatedAt).toBe('2026-04-21T00:00:00.000Z');
    expect(payloadIndex).toEqual(['dist/main/acrobat.js', 'templates/standard']);
  });
});
