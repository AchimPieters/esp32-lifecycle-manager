import express, { type Express } from 'express';
import { z } from 'zod';
import { createImposeRouter } from './routes/impose';
import { createJobsRouter } from './routes/jobs';
import { createTemplateRouter } from './routes/templates';
import { createWebhookRouter } from './routes/webhooks';
import { createAuthMiddleware } from './middleware/auth';
import { createRateLimitMiddleware } from './middleware/rateLimit';
import { errorHandler } from './middleware/errorHandler';
import { validateBody } from './middleware/validation';
import { getAppConfig } from '@utils/config';

const imposeSchema = z.object({
  pages: z.number().int().positive()
});

/**
 * Builds configured API server instance.
 */
export function createApiServer(): Express {
  const app = express();
  const config = getAppConfig();

  app.use(express.json());
  app.use(createRateLimitMiddleware(config.rateLimitWindowMs, config.rateLimitMaxRequests));
  app.use(createAuthMiddleware(config.apiAuthToken));

  app.use('/impose', validateBody(imposeSchema), createImposeRouter());
  app.use('/templates', createTemplateRouter());
  app.use('/jobs', createJobsRouter());
  app.use('/webhooks', createWebhookRouter());

  app.use(errorHandler);

  return app;
}
