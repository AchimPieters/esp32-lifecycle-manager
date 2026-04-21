import express from 'express';
import request from 'supertest';
import { z } from 'zod';
import { createAuthMiddleware } from '../../../src/api/middleware/auth';
import { createRateLimitMiddleware } from '../../../src/api/middleware/rateLimit';
import { validateBody } from '../../../src/api/middleware/validation';
import { errorHandler } from '../../../src/api/middleware/errorHandler';

describe('api middleware', () => {
  it('enforces auth when token configured', async () => {
    const app = express();
    app.use(createAuthMiddleware('secret'));
    app.get('/secure', (_req, res) => res.json({ ok: true }));

    const noToken = await request(app).get('/secure');
    const invalid = await request(app).get('/secure').set('authorization', 'Bearer bad');
    const ok = await request(app).get('/secure').set('authorization', 'Bearer secret');

    expect(noToken.status).toBe(401);
    expect(invalid.status).toBe(403);
    expect(ok.status).toBe(200);
  });

  it('validates body and emits issues', async () => {
    const app = express();
    app.use(express.json());
    app.post('/check', validateBody(z.object({ pages: z.number().positive() })), (_req, res) => {
      res.json({ ok: true });
    });

    const invalid = await request(app).post('/check').send({ pages: 0 });
    const valid = await request(app).post('/check').send({ pages: 2 });

    expect(invalid.status).toBe(400);
    expect(valid.status).toBe(200);
  });

  it('applies rate limit and error handler', async () => {
    const app = express();
    app.use(createRateLimitMiddleware(60_000, 1));
    app.get('/limited', () => {
      throw Object.assign(new Error('boom'), { status: 418 });
    });
    app.use(errorHandler);

    const first = await request(app).get('/limited');
    const second = await request(app).get('/limited');

    expect(first.status).toBe(418);
    expect(second.status).toBe(429);
  });
});
