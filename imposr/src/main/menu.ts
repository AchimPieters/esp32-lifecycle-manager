import { ImposrError } from '@utils/errors';
import { logger } from '@utils/logger';

export interface MenuItem {
  readonly label: string;
  readonly action: string;
}

export interface MenuDefinition {
  readonly app: MenuItem[];
  readonly file: MenuItem[];
  readonly help: MenuItem[];
}

/**
 * Builds and validates the top-level application menu definition.
 */
export class AppMenuBuilder {
  /**
   * Creates a default menu definition.
   */
  public buildDefault(): MenuDefinition {
    try {
      return {
        app: [
          { label: 'Preferences', action: 'open-settings' },
          { label: 'Quit', action: 'quit-app' }
        ],
        file: [
          { label: 'Open PDF', action: 'open-pdf' },
          { label: 'Export', action: 'export-pdf' }
        ],
        help: [{ label: 'Documentation', action: 'open-docs' }]
      };
    } catch (error) {
      logger.error('Failed to build default menu', error as Error);
      throw new ImposrError('Failed to build app menu', 'MENU_BUILD_ERROR', {
        cause: (error as Error).message
      });
    }
  }

  /**
   * Validates menu structure and required actions.
   */
  public validate(menu: MenuDefinition): void {
    try {
      const allItems = [...menu.app, ...menu.file, ...menu.help];
      if (allItems.length === 0) {
        throw new ImposrError('Menu cannot be empty', 'MENU_VALIDATION_ERROR');
      }

      const invalidItem = allItems.find((item) => !item.label.trim() || !item.action.trim());
      if (invalidItem) {
        throw new ImposrError('Menu item contains empty label or action', 'MENU_VALIDATION_ERROR', {
          item: invalidItem
        });
      }
    } catch (error) {
      if (error instanceof ImposrError) {
        throw error;
      }

      logger.error('Unexpected menu validation error', error as Error);
      throw new ImposrError('Menu validation failed', 'MENU_VALIDATION_ERROR', {
        cause: (error as Error).message
      });
    }
  }
}
