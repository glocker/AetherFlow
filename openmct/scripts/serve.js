import { createServer } from 'node:http';
import { createReadStream } from 'node:fs';
import { stat } from 'node:fs/promises';
import { extname, join, normalize, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

import { buildDashboard } from './build.js';

const projectRoot = resolve(fileURLToPath(new URL('..', import.meta.url)));
const distDir = resolve(projectRoot, 'dist');

const contentTypes = new Map([
  ['.html', 'text/html; charset=utf-8'],
  ['.js', 'text/javascript; charset=utf-8'],
  ['.css', 'text/css; charset=utf-8'],
  ['.json', 'application/json; charset=utf-8'],
  ['.svg', 'image/svg+xml'],
  ['.png', 'image/png'],
  ['.jpg', 'image/jpeg'],
  ['.jpeg', 'image/jpeg'],
  ['.gif', 'image/gif'],
  ['.webp', 'image/webp'],
  ['.woff', 'font/woff'],
  ['.woff2', 'font/woff2'],
  ['.ttf', 'font/ttf']
]);

function optionValue(name, fallback) {
  const index = process.argv.indexOf(name);
  return index >= 0 && process.argv[index + 1] ? process.argv[index + 1] : fallback;
}

function safePath(urlPath) {
  const decodedPath = decodeURIComponent(urlPath.split('?')[0]);
  const normalizedPath = normalize(decodedPath).replace(/^([/\\])+/, '');
  const filePath = resolve(distDir, normalizedPath || 'index.html');
  return filePath.startsWith(distDir + sep) || filePath === distDir ? filePath : null;
}

async function serveFile(request, response) {
  const url = new URL(request.url, 'http://127.0.0.1');
  let filePath = safePath(url.pathname);

  if (filePath === null) {
    response.writeHead(403);
    response.end('Forbidden');
    return;
  }

  try {
    const info = await stat(filePath);
    if (info.isDirectory()) {
      filePath = join(filePath, 'index.html');
    }
  } catch {
    filePath = resolve(distDir, 'index.html');
  }

  try {
    const info = await stat(filePath);
    if (!info.isFile()) {
      response.writeHead(404);
      response.end('Not found');
      return;
    }
  } catch {
    response.writeHead(404);
    response.end('Not found');
    return;
  }

  const contentType = contentTypes.get(extname(filePath)) || 'application/octet-stream';
  response.writeHead(200, {
    'Content-Type': contentType,
    'Cache-Control': 'no-store'
  });
  createReadStream(filePath).pipe(response);
}

async function main() {
  const shouldBuild = process.argv.includes('--build');
  const host = optionValue('--host', process.env.AETHERFLOW_DASHBOARD_HOST || '127.0.0.1');
  const port = Number(optionValue('--port', process.env.AETHERFLOW_DASHBOARD_PORT || '5173'));

  if (shouldBuild) {
    await buildDashboard({ minify: false });
  }

  const server = createServer((request, response) => {
    serveFile(request, response).catch((error) => {
      console.error(error);
      response.writeHead(500);
      response.end('Internal server error');
    });
  });

  server.listen(port, host, () => {
    console.log(`AetherFlow OpenMCT dashboard: http://${host}:${port}/`);
  });
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
