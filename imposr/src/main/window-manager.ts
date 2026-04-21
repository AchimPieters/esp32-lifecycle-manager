import { ImposrError } from '@utils/errors';
import { logger } from '@utils/logger';

export interface AppWindow {
  isDestroyed(): boolean;
  show(): void;
  hide(): void;
  loadURL(url: string): Promise<void>;
}

export interface WindowFactory {
  createMainWindow(): AppWindow;
}

/**
 * Manages creation and lifecycle of the application's main window.
 */
export class WindowManager {
  private mainWindow: AppWindow | null = null;

  constructor(private readonly factory: WindowFactory) {}

  /**
   * Creates the main window if needed and loads the requested URL.
   */
  public async initialize(startUrl: string): Promise<AppWindow> {
    try {
      if (!this.mainWindow || this.mainWindow.isDestroyed()) {
        this.mainWindow = this.factory.createMainWindow();
      }

      await this.mainWindow.loadURL(startUrl);
      return this.mainWindow;
    } catch (error) {
      logger.error('Failed to initialize main window', error as Error, { startUrl });
      throw new ImposrError('Window initialization failed', 'WINDOW_INIT_ERROR', {
        startUrl,
        cause: (error as Error).message
      });
    }
  }

  /**
   * Returns the active main window or null when not initialized.
   */
  public getMainWindow(): AppWindow | null {
    return this.mainWindow;
  }

  /**
   * Shows the main window when available.
   */
  public show(): void {
    try {
      this.mainWindow?.show();
    } catch (error) {
      logger.error('Failed to show main window', error as Error);
      throw new ImposrError('Unable to show main window', 'WINDOW_SHOW_ERROR', {
        cause: (error as Error).message
      });
    }
  }

  /**
   * Hides the main window when available.
   */
  public hide(): void {
    try {
      this.mainWindow?.hide();
    } catch (error) {
      logger.error('Failed to hide main window', error as Error);
      throw new ImposrError('Unable to hide main window', 'WINDOW_HIDE_ERROR', {
        cause: (error as Error).message
      });
    }
  }
}
