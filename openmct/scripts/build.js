import esbuild from 'esbuild';
import { cp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const projectRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const srcDir = resolve(projectRoot, 'src');
const distDir = resolve(projectRoot, 'dist');
const assetsDir = resolve(distDir, 'assets');
const openmctDistDir = resolve(projectRoot, 'node_modules', 'openmct', 'dist');

function sourceAliasPlugin() {
  return {
    name: 'source-alias',
    setup(build) {
      build.onResolve({ filter: /^@\/(.*)$/ }, (args) => ({
        path: resolve(srcDir, args.path.slice(2))
      }));
    }
  };
}

export async function buildDashboard({ minify = true } = {}) {
  await rm(distDir, { recursive: true, force: true });
  await mkdir(assetsDir, { recursive: true });

  const indexHtml = await readFile(resolve(projectRoot, 'index.html'), 'utf8');
  await writeFile(resolve(distDir, 'index.html'), indexHtml);
  await cp(resolve(srcDir, 'styles.css'), resolve(assetsDir, 'styles.css'));
  await cp(openmctDistDir, resolve(distDir, 'openmct'), { recursive: true });

  await esbuild.build({
    entryPoints: [resolve(srcDir, 'plugin.js')],
    outfile: resolve(assetsDir, 'dashboard.js'),
    bundle: true,
    format: 'esm',
    platform: 'browser',
    target: ['es2022'],
    sourcemap: true,
    minify,
    define: {
      __OPENMCT_ASSET_PATH__: JSON.stringify('/openmct')
    },
    loader: {
      '.css': 'text'
    },
    plugins: [sourceAliasPlugin()]
  });
}

