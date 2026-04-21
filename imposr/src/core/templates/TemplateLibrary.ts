import { TemplateError } from '@utils/errors';
import { TemplateValidator, type TemplateDefinition } from './TemplateValidator';

/**
 * In-memory library for template retrieval and registration.
 */
export class TemplateLibrary {
  private readonly items = new Map<string, TemplateDefinition>();

  private readonly validator = new TemplateValidator();

  /**
   * Adds a template to the library after validation.
   */
  public add(template: TemplateDefinition): void {
    this.validator.validate(template);

    if (this.items.has(template.id)) {
      throw new TemplateError('Template already exists', { id: template.id });
    }

    this.items.set(template.id, template);
  }

  /**
   * Looks up a template by id.
   */
  public get(id: string): TemplateDefinition {
    const template = this.items.get(id);
    if (!template) {
      throw new TemplateError('Template not found', { id });
    }

    return template;
  }

  /**
   * Returns all registered templates.
   */
  public list(): TemplateDefinition[] {
    return [...this.items.values()];
  }
}
