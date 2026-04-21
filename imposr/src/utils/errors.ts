export type ErrorDetails = Record<string, unknown>;

/**
 * Base error class for the Imposr application domain.
 */
export class ImposrError extends Error {
  public code: string;

  public details: ErrorDetails;

  public isRecoverable: boolean;

  constructor(
    message: string,
    code: string,
    details: ErrorDetails = {},
    isRecoverable = false
  ) {
    super(message);
    this.name = 'ImposrError';
    this.code = code;
    this.details = details;
    this.isRecoverable = isRecoverable;
    Error.captureStackTrace(this, this.constructor);
  }

  /**
   * Creates a serializable error payload for logging and IPC.
   */
  public toJSON(): Record<string, unknown> {
    return {
      name: this.name,
      message: this.message,
      code: this.code,
      details: this.details,
      isRecoverable: this.isRecoverable,
      stack: this.stack
    };
  }
}

/**
 * General PDF domain error.
 */
export class PDFError extends ImposrError {
  constructor(message: string, details: ErrorDetails = {}) {
    super(message, 'PDF_ERROR', details);
    this.name = 'PDFError';
  }
}

/**
 * Error raised when loading a PDF file fails.
 */
export class PDFLoadError extends PDFError {
  constructor(message: string, filePath: string, details: ErrorDetails = {}) {
    super(message, { filePath, ...details });
    this.code = 'PDF_LOAD_ERROR';
    this.name = 'PDFLoadError';
  }
}

/**
 * Error raised when PDF content does not pass validation.
 */
export class PDFValidationError extends PDFError {
  constructor(message: string, details: ErrorDetails = {}) {
    super(message, details);
    this.code = 'PDF_VALIDATION_ERROR';
    this.name = 'PDFValidationError';
  }
}

/**
 * Recoverable error for PDF processing failures.
 */
export class PDFProcessingError extends PDFError {
  constructor(message: string, details: ErrorDetails = {}) {
    super(message, details);
    this.code = 'PDF_PROCESSING_ERROR';
    this.name = 'PDFProcessingError';
    this.isRecoverable = true;
  }
}

export class TemplateError extends ImposrError {
  constructor(message: string, details: ErrorDetails = {}) {
    super(message, 'TEMPLATE_ERROR', details);
    this.name = 'TemplateError';
  }
}

export class LicenseError extends ImposrError {
  constructor(message: string, details: ErrorDetails = {}) {
    super(message, 'LICENSE_ERROR', details);
    this.name = 'LicenseError';
  }
}

export class FeatureNotAvailableError extends LicenseError {
  constructor(feature: string, requiredTier: string) {
    super(`Feature "${feature}" requires ${requiredTier} license`, {
      feature,
      requiredTier
    });
    this.code = 'FEATURE_NOT_AVAILABLE';
    this.name = 'FeatureNotAvailableError';
  }
}

export class BatchProcessingError extends ImposrError {
  constructor(message: string, jobId: string, details: ErrorDetails = {}) {
    super(message, 'BATCH_PROCESSING_ERROR', { jobId, ...details }, true);
    this.name = 'BatchProcessingError';
  }
}

export class NetworkError extends ImposrError {
  constructor(message: string, details: ErrorDetails = {}) {
    super(message, 'NETWORK_ERROR', details, true);
    this.name = 'NetworkError';
  }
}
