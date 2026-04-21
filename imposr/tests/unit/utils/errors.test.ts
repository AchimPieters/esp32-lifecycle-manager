import {
  BatchProcessingError,
  FeatureNotAvailableError,
  ImposrError,
  NetworkError,
  PDFLoadError,
  PDFProcessingError,
  PDFValidationError
} from '../../../src/utils/errors';

describe('errors', () => {
  it('serializes base error', () => {
    const err = new ImposrError('Oops', 'BASE', { a: 1 }, true);
    const json = err.toJSON();

    expect(json).toMatchObject({
      name: 'ImposrError',
      message: 'Oops',
      code: 'BASE',
      details: { a: 1 },
      isRecoverable: true
    });
  });

  it('creates pdf-related error hierarchy', () => {
    const loadErr = new PDFLoadError('failed', '/tmp/a.pdf');
    const validationErr = new PDFValidationError('bad');
    const processingErr = new PDFProcessingError('retryable');

    expect(loadErr.code).toBe('PDF_LOAD_ERROR');
    expect(loadErr.details).toMatchObject({ filePath: '/tmp/a.pdf' });
    expect(validationErr.code).toBe('PDF_VALIDATION_ERROR');
    expect(processingErr.code).toBe('PDF_PROCESSING_ERROR');
    expect(processingErr.isRecoverable).toBe(true);
  });

  it('creates licensing, batch and network errors', () => {
    const featErr = new FeatureNotAvailableError('batch', 'pro');
    const batchErr = new BatchProcessingError('broken', 'job-1');
    const networkErr = new NetworkError('timeout');

    expect(featErr.code).toBe('FEATURE_NOT_AVAILABLE');
    expect(batchErr.details).toMatchObject({ jobId: 'job-1' });
    expect(batchErr.isRecoverable).toBe(true);
    expect(networkErr.code).toBe('NETWORK_ERROR');
  });
});
