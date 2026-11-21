// 只准备 wx_demo 的静态资源；不启动 DSH、Node 服务或桌面端进程。
// Core 不复制到项目：由原生宿主从用户本地运行时缓存提供。
import { cp, mkdir, stat, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { tmpdir } from 'node:os';

const project = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const source = resolve(process.argv[2] || join(project, 'dsh-pet-live2d'));
const plugin = join(source, 'dsh-live2d-pet');
const target = join(project, 'wx_demo', 'res', 'pet');
await mkdir(join(target, 'licenses'), { recursive: true });

// scanPet 只读取源模型。隔离它可选的用户声音查找，不读取真实 DSH 配置。
process.env.DSH_HOME = join(tmpdir(), 'wx-demo-static-pet-catalog');
const { scanPet } = await import(pathToFileURL(join(plugin, 'lib', 'index.js')).href);
const pet = scanPet(join(plugin, 'pets', 'ds-whale-girl'), 'ds-whale-girl');
if (!pet) throw new Error('标准 Live2D 模型清单不完整。');
for (const entry of pet.closure) await stat(join(pet.dir, entry));
await cp(pet.dir, join(target, 'model'), { recursive: true });
await cp(join(pet.dir, 'previews', 'idle.png'), join(target, 'preview.png'));
await cp(join(plugin, 'lib', 'client.js'), join(target, 'client.js'));
await cp(join(plugin, 'lib', 'live2d-vendor.js'), join(target, 'vendor.js'));
await cp(join(source, 'LICENSE'), join(target, 'licenses', 'DSH-MIT.txt'));
await cp(join(source, 'NOTICE.md'), join(target, 'licenses', 'NOTICE.md'));
await cp(join(plugin, 'NOTICE.md'), join(target, 'licenses', 'DSH-NOTICE.md'));
await cp(join(pet.dir, 'LICENSE'), join(target, 'licenses', 'MODEL-LICENSE.txt'));

const { dir, closure, modelPath, ...publicPet } = pet;
publicPet.modelUrl = '/model/' + modelPath.split('/').map(encodeURIComponent).join('/');
// 构建时只读随包声音，不带入任何机器上的用户上传内容。
publicPet.soundLibrary = publicPet.voices.map(({ id, name }) => ({ id, name, builtin: true }));
await writeFile(join(target, 'catalog.json'), JSON.stringify({
  ok: true,
  coreUrl: '/api/live2d-pet/runtime/live2dcubismcore.min.js',
  vendorUrl: '/api/live2d-pet/runtime/live2d-vendor.js',
  pets: [publicPet],
}, null, 2) + '\n', 'utf8');

// 固定版本的 React UMD 只在准备时获取；程序运行完全使用项目本地文件。
async function obtain(name, url) {
  try { if ((await stat(join(target, name))).size > 100) return; } catch { /* 首次准备 */ }
  const response = await fetch(url, { signal: AbortSignal.timeout(30000) });
  if (!response.ok) throw new Error(`${name}: HTTP ${response.status}`);
  const content = await response.text();
  if (content.length < 100 || /^\s*</.test(content)) throw new Error(`${name}: 资源内容无效`);
  await writeFile(join(target, name), content, 'utf8');
}
await obtain('react.js', 'https://unpkg.com/react@18.3.1/umd/react.production.min.js');
await obtain('react-dom.js', 'https://unpkg.com/react-dom@18.3.1/umd/react-dom.production.min.js');
await obtain('licenses/React-MIT.txt', 'https://unpkg.com/react@18.3.1/LICENSE');
await obtain('licenses/Pixi-MIT.txt', 'https://unpkg.com/pixi.js@8.19.0/LICENSE');
await obtain('licenses/Engine-MIT.txt', 'https://unpkg.com/untitled-pixi-live2d-engine@1.3.5/LICENSE');
console.log(`已准备 ${pet.closure.size} 个模型引用文件、${pet.voices.length} 段声音；未复制 Core。`);
