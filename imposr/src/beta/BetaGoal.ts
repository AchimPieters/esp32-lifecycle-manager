export interface BetaGoalItem {
  readonly id: string;
  readonly description: string;
  readonly requiredPaths: readonly string[];
}

export interface BetaGoal {
  readonly name: string;
  readonly targetDate: string;
  readonly successCriteria: readonly string[];
  readonly items: readonly BetaGoalItem[];
}

/**
 * Provisional end-goal for the first externally testable commercial beta.
 */
export const betaGoal: BetaGoal = {
  name: 'Imposr Pro Commercial Beta',
  targetDate: '2026-06-30',
  successCriteria: [
    'Core imposition workflow works end-to-end for real PDFs',
    'Licensing activation + upgrade UX is testable',
    'API security middleware exists and is wired',
    'Distribution scripts for Windows/macOS/Linux are present',
    'User/developer docs cover onboarding and troubleshooting'
  ],
  items: [
    {
      id: 'renderer-pdf-workflow',
      description: 'PDF workflow components are available for visual QA',
      requiredPaths: [
        'src/renderer/components/pdf/PDFViewer.tsx',
        'src/renderer/components/pdf/PDFThumbnail.tsx',
        'src/renderer/components/pdf/PDFRenderer.tsx',
        'src/renderer/components/pdf/PageNavigator.tsx'
      ]
    },
    {
      id: 'api-hardening',
      description: 'API security and validation middleware is implemented',
      requiredPaths: [
        'src/api/middleware/auth.ts',
        'src/api/middleware/rateLimit.ts',
        'src/api/middleware/validation.ts',
        'src/api/middleware/errorHandler.ts'
      ]
    },
    {
      id: 'template-pack',
      description: 'Commercial default templates are packaged',
      requiredPaths: [
        'templates/standard/4up-a4-a3.json',
        'templates/standard/8up-a4-a2.json',
        'templates/standard/booklet-8page.json',
        'templates/standard/booklet-32page.json',
        'templates/standard/perfect-bound.json'
      ]
    },
    {
      id: 'distribution',
      description: 'Cross-platform installers are prepared for beta users',
      requiredPaths: [
        'installers/windows/build.ps1',
        'installers/linux/rpm',
        'installers/linux/appimage'
      ]
    },
    {
      id: 'documentation',
      description: 'Beta docs are ready for users and integrators',
      requiredPaths: [
        'docs/user-guide/templates.md',
        'docs/user-guide/batch-processing.md',
        'docs/user-guide/troubleshooting.md',
        'docs/api-reference/rest-api.md',
        'docs/api-reference/cli-reference.md'
      ]
    }
  ]
};
