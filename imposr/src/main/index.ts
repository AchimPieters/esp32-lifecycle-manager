import { AppMenuBuilder } from './menu';
import { IpcHandlers, type IpcRegistry } from './ipc-handlers';
import { WindowManager, type WindowFactory } from './window-manager';
import { logger } from '@utils/logger';

export interface BootstrapDependencies {
  readonly windowFactory: WindowFactory;
  readonly ipcRegistry: IpcRegistry;
  readonly startUrl: string;
}

/**
 * Bootstraps the main-process dependencies for the desktop application.
 */
export async function bootstrapMainProcess(dependencies: BootstrapDependencies): Promise<void> {
  const menuBuilder = new AppMenuBuilder();
  const ipcHandlers = new IpcHandlers(dependencies.ipcRegistry);
  const windowManager = new WindowManager(dependencies.windowFactory);

  const menu = menuBuilder.buildDefault();
  menuBuilder.validate(menu);
  ipcHandlers.registerDefaults();

  await windowManager.initialize(dependencies.startUrl);
  windowManager.show();

  logger.info('Main process bootstrapped', { startUrl: dependencies.startUrl });
}
