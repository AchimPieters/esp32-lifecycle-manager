const debugMock = jest.fn();
const infoMock = jest.fn();
const warnMock = jest.fn();
const errorMock = jest.fn();
const captureMessageMock = jest.fn();
const captureExceptionMock = jest.fn();
const sentryInitMock = jest.fn();

jest.mock('electron-log', () => ({
  __esModule: true,
  default: {
    debug: debugMock,
    info: infoMock,
    warn: warnMock,
    error: errorMock,
    transports: {
      file: { level: 'info' },
      console: { level: 'debug' }
    }
  }
}));

jest.mock('@sentry/electron', () => ({
  init: sentryInitMock,
  captureMessage: captureMessageMock,
  captureException: captureExceptionMock
}));

describe('logger', () => {
  beforeEach(() => {
    jest.resetModules();
    jest.clearAllMocks();
    process.env.NODE_ENV = 'test';
  });

  it('logs based on configured level', async () => {
    const { logger, LogLevel } = await import('../../../src/utils/logger');

    logger.setLevel(LogLevel.WARN);
    logger.debug('d');
    logger.info('i');
    logger.warn('w');

    expect(debugMock).not.toHaveBeenCalled();
    expect(infoMock).not.toHaveBeenCalled();
    expect(warnMock).toHaveBeenCalledWith('w', undefined);
  });

  it('logs debug/info when threshold is debug', async () => {
    const { logger, LogLevel } = await import('../../../src/utils/logger');

    logger.setLevel(LogLevel.DEBUG);
    logger.debug('debug-message', { v: 1 });
    logger.info('info-message', { v: 2 });

    expect(debugMock).toHaveBeenCalledWith('debug-message', { v: 1 });
    expect(infoMock).toHaveBeenCalledWith('info-message', { v: 2 });
  });

  it('captures sentry warning/error in production', async () => {
    process.env.NODE_ENV = 'production';

    const { logger } = await import('../../../src/utils/logger');

    logger.warn('w', { k: 1 });
    logger.error('e', new Error('boom'), { k: 1 });

    expect(sentryInitMock).toHaveBeenCalled();
    expect(captureMessageMock).toHaveBeenCalled();
    expect(captureExceptionMock).toHaveBeenCalled();
  });

  it('does not capture exception when no error is provided', async () => {
    process.env.NODE_ENV = 'production';
    const { logger } = await import('../../../src/utils/logger');

    logger.error('only-message');

    expect(captureExceptionMock).not.toHaveBeenCalled();
  });

  it('falls back to electron-log error when writer throws', async () => {
    const { logger, LogLevel } = await import('../../../src/utils/logger');
    logger.setLevel(LogLevel.DEBUG);

    debugMock.mockImplementationOnce(() => {
      throw new Error('debug failed');
    });

    logger.debug('boom');

    expect(errorMock).toHaveBeenCalled();
  });
});
