import { AcrobatSDKBridge } from '../../../src/main/acrobat/AcrobatSDKBridge';
import { ImposrError } from '../../../src/utils/errors';

describe('AcrobatSDKBridge', () => {
  const createBridge = (overrides: Partial<{ pluginId: string; pluginVersion: string; minAcrobatVersion: string }> = {}) =>
    new AcrobatSDKBridge({
      pluginId: overrides.pluginId ?? 'imposr.plugin',
      pluginVersion: overrides.pluginVersion ?? '1.2.3',
      minAcrobatVersion: overrides.minAcrobatVersion ?? '2024.0'
    });

  it('initializes and registers menu when config is valid', () => {
    const bridge = createBridge();

    bridge.initialize();

    expect(bridge.isInitialized()).toBe(true);
    expect(bridge.registerMenu()).toBe('menu:imposr.plugin:registered');
  });

  it('rejects invalid bridge config', () => {
    const bridge = createBridge({ pluginId: '   ' });

    expect(() => bridge.initialize()).toThrow(ImposrError);
    expect(() => bridge.initialize()).toThrow('Invalid Acrobat bridge config');
  });

  it('rejects operations before initialization', () => {
    const bridge = createBridge();

    expect(() => bridge.registerMenu()).toThrow('Bridge is not initialized');
    expect(() =>
      bridge.executeImposition({ inputPath: 'in.pdf', outputPath: 'out.pdf', templateId: '2up-a4-a3' })
    ).toThrow('Bridge is not initialized');
  });

  it('validates imposition request payload', () => {
    const bridge = createBridge();
    bridge.initialize();

    expect(() =>
      bridge.executeImposition({ inputPath: '  ', outputPath: 'out.pdf', templateId: 'template' })
    ).toThrow('Invalid imposition request payload');
  });

  it('executes imposition and supports shutdown', () => {
    const bridge = createBridge();
    bridge.initialize();

    expect(
      bridge.executeImposition({
        inputPath: '/tmp/input.pdf',
        templateId: 'booklet-16page',
        outputPath: '/tmp/output.pdf'
      })
    ).toBe('imposed:/tmp/input.pdf:booklet-16page:/tmp/output.pdf');

    bridge.shutdown();

    expect(bridge.isInitialized()).toBe(false);
  });
});
