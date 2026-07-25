import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { viteSingleFile } from 'vite-plugin-singlefile';

// Single-file output: the firmware serves exactly one embedded, gzipped
// index.html. Rebuild with tools/webapp/build.sh (see README.md there).
export default defineConfig({
  plugins: [react(), viteSingleFile()],
  build: { target: 'es2018', minify: true },
});
