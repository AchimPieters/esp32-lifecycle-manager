import { PluginLifecycleManager } from '../../../src/main/acrobat/PluginLifecycleManager';
import type { AcrobatSDKBridge, ImpositionRequest } from '../../../src/main/acrobat/AcrobatSDKBridge';

describe('PluginLifecycleManager', () => {
  it('delegates lifecycle operations to the bridge', () => {
    const request: ImpositionRequest = {
      inputPath: 'input.pdf',
      templateId: '2up-a4-a3',
      outputPath: 'output.pdf'
    };

    const bridge: Pick<AcrobatSDKBridge, 'initialize' | 'registerMenu' | 'executeImposition' | 'shutdown'> = {
      initialize: jest.fn(),
      registerMenu: jest.fn(() => 'menu:imposr.plugin:registered'),
      executeImposition: jest.fn(() => 'imposed:input.pdf:2up-a4-a3:output.pdf'),
      shutdown: jest.fn()
    };

    const manager = new PluginLifecycleManager(bridge as AcrobatSDKBridge);

    expect(manager.start()).toBe('menu:imposr.plugin:registered');
    expect(manager.process(request)).toBe('imposed:input.pdf:2up-a4-a3:output.pdf');

    manager.stop();

    expect(bridge.initialize).toHaveBeenCalledTimes(1);
    expect(bridge.registerMenu).toHaveBeenCalledTimes(1);
    expect(bridge.executeImposition).toHaveBeenCalledWith(request);
    expect(bridge.shutdown).toHaveBeenCalledTimes(1);
  });
});
