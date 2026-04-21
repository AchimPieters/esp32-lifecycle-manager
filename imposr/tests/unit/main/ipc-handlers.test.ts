import { IpcHandlers, type IpcHandler, type IpcRegistry } from '../../../src/main/ipc-handlers';

describe('IpcHandlers', () => {
  it('registers default handlers', async () => {
    const callbacks: Record<string, IpcHandler<unknown, unknown>> = {};

    const registry: IpcRegistry = {
      register<TInput, TOutput>(channel: string, handler: IpcHandler<TInput, TOutput>): void {
        callbacks[channel] = handler as IpcHandler<unknown, unknown>;
      }
    };

    const handlers = new IpcHandlers(registry);
    handlers.registerDefaults();

    await expect(callbacks['pdf:open']({ path: 'file.pdf' })).resolves.toEqual({ accepted: true });
    await expect(callbacks['health:check'](undefined)).resolves.toEqual({ status: 'ok' });
    await expect(callbacks['pdf:open']({ path: '' })).rejects.toThrow('Path is required');
  });
});
