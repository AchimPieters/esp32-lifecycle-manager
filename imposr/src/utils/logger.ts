import log from 'electron-log';
import * as Sentry from '@sentry/electron';

export enum LogLevel {
  DEBUG = 'debug',
  INFO = 'info',
  WARN = 'warn',
  ERROR = 'error'
}

export interface LogMeta {
  [key: string]: unknown;
}

/**
 * Application logger wrapper around electron-log and optional Sentry reporting.
 */
class Logger {
  private static instance: Logger;

  private level: LogLevel = LogLevel.INFO;

  private readonly levelPriority: Record<LogLevel, number> = {
    [LogLevel.DEBUG]: 10,
    [LogLevel.INFO]: 20,
    [LogLevel.WARN]: 30,
    [LogLevel.ERROR]: 40
  };

  private constructor() {
    this.configureTransports();
    this.configureSentry();
  }

  /**
   * Returns singleton logger instance.
   */
  public static getInstance(): Logger {
    if (!Logger.instance) {
      Logger.instance = new Logger();
    }

    return Logger.instance;
  }

  /**
   * Updates the minimum log level.
   */
  public setLevel(level: LogLevel): void {
    this.level = level;
  }

  /**
   * Logs debug messages when current threshold allows it.
   */
  public debug(message: string, meta?: LogMeta): void {
    try {
      if (this.shouldLog(LogLevel.DEBUG)) {
        log.debug(message, meta);
      }
    } catch (error) {
      log.error('Logger debug failed', error);
    }
  }

  /**
   * Logs informational messages when current threshold allows it.
   */
  public info(message: string, meta?: LogMeta): void {
    try {
      if (this.shouldLog(LogLevel.INFO)) {
        log.info(message, meta);
      }
    } catch (error) {
      log.error('Logger info failed', error);
    }
  }

  /**
   * Logs warning messages and forwards to Sentry in production.
   */
  public warn(message: string, meta?: LogMeta): void {
    try {
      if (this.shouldLog(LogLevel.WARN)) {
        log.warn(message, meta);
        if (process.env.NODE_ENV === 'production') {
          Sentry.captureMessage(message, {
            level: 'warning',
            extra: meta
          });
        }
      }
    } catch (error) {
      log.error('Logger warn failed', error);
    }
  }

  /**
   * Logs an error and forwards exception to Sentry in production.
   */
  public error(message: string, error?: Error, meta?: LogMeta): void {
    try {
      if (this.shouldLog(LogLevel.ERROR)) {
        log.error(message, error, meta);
        if (process.env.NODE_ENV === 'production' && error) {
          Sentry.captureException(error, {
            extra: { message, ...meta }
          });
        }
      }
    } catch (internalError) {
      log.error('Logger error failed', internalError);
    }
  }

  private configureTransports(): void {
    log.transports.file.level = 'info';
    log.transports.console.level = 'debug';
  }

  private configureSentry(): void {
    if (process.env.NODE_ENV !== 'production') {
      return;
    }

    try {
      Sentry.init({
        dsn: process.env.SENTRY_DSN,
        environment: process.env.NODE_ENV,
        release: `imposr@${process.env.npm_package_version ?? 'dev'}`
      });
    } catch (error) {
      log.error('Sentry init failed', error);
    }
  }

  private shouldLog(level: LogLevel): boolean {
    return this.levelPriority[level] >= this.levelPriority[this.level];
  }
}

export const logger = Logger.getInstance();
