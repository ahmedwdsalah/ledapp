import { File, Paths } from 'expo-file-system';

type Entry = { time: string; stage: string; detail: string };
const listeners = new Set<() => void>();
const file = new File(Paths.document, 'motif-connection-log.json');
let entries: Entry[] = [];

try {
  if (file.exists) entries = (JSON.parse(file.textSync()) as Entry[]).slice(-100);
} catch { /* A damaged local log must not block the app. */ }

export function recordConnection(stage: string, detail: string) {
  const entry = { time: new Date().toISOString(), stage, detail };
  entries = [...entries.slice(-99), entry];
  try {
    if (!file.exists) file.create();
    file.write(JSON.stringify(entries));
  } catch { /* Console and on-screen logs still work without persistence. */ }
  console.info(`[Motif] ${entry.time} ${stage}: ${detail}`);
  listeners.forEach((listener) => listener());
}

export function getConnectionLog() { return entries; }
export function subscribeConnectionLog(listener: () => void) {
  listeners.add(listener);
  return () => { listeners.delete(listener); };
}
export function formatConnectionLog() {
  return entries.map(({ time, stage, detail }) => `${time}  ${stage}: ${detail}`).join('\n');
}
