import { ImposrError } from '@utils/errors';
import { logger } from '@utils/logger';

export type IpcHandler<TInput, TOutput> = (payload: TInput) => Promise<TOutput>;

export interface IpcRegistry {
  register<TInput, TOutput>(channel: string, handler: IpcHandler<TInput, TOutput>): void;
}

/**
 * Registers strongly-typed IPC handlers used by the renderer.
 */
export class IpcHandlers {
  constructor(private readonly registry: IpcRegistry) {}

  /**
   * Registers all default IPC channels.
   */
  public registerDefaults(): void {
    try {
      this.registry.register<{ path: string }, { accepted: boolean }>('pdf:open', async (payload) => {
        if (!payload.path.trim()) {
          throw new ImposrError('Path is required', 'IPC_INVALID_PAYLOAD');
        }

        return { accepted: true };
      });

      this.registry.register<void, { status: 'ok' }>('health:check', async () => ({
        status: 'ok'
      }));
    } catch (error) {
      logger.error('Failed to register IPC handlers', error as Error);
      throw new ImposrError('IPC registration failed', 'IPC_REGISTER_ERROR', {
        cause: (error as Error).message
      });
    }
  }
}
