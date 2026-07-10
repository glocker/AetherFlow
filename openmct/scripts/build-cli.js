import { buildDashboard } from './build.js';

buildDashboard().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
