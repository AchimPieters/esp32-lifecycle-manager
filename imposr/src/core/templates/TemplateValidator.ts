import { TemplateError } from '@utils/errors';

export interface TemplateDefinition {
  readonly id: string;
  readonly name: string;
  readonly mode: 'natural' | 'booklet';
  readonly sheet: { width: number; height: number };
}

/**
 * Validates template payloads before persistence or execution.
 */
export class TemplateValidator {
  /**
   * Ensures template has required identifiers and valid sheet size.
   */
  public validate(template: TemplateDefinition): void {
    if (!template.id.trim()) {
      throw new TemplateError('Template id is required');
    }

    if (!template.name.trim()) {
      throw new TemplateError('Template name is required', { id: template.id });
    }

    if (template.sheet.width <= 0 || template.sheet.height <= 0) {
      throw new TemplateError('Template sheet dimensions must be positive', {
        id: template.id,
        sheet: template.sheet
      });
    }
  }
}
