import { buildDashboard } from './build.js';

buildDashboard()
  .then(() => {
    console.log('Dashboard build completed');
  })
  .catch((error) => {
    console.error(error);
    process.exitCode = 1;
  });
