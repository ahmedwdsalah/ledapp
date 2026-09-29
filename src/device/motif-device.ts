import { Asset } from 'expo-asset';
import { fetch } from 'expo/fetch';
import { File, UploadType } from 'expo-file-system';
import * as SecureStore from 'expo-secure-store';
import BleManager from 'react-native-ble-manager';
import { PermissionsAndroid, Platform } from 'react-native';
import { recordConnection } from './connection-log';

export const SERVICE = 'c4683809-f301-4312-83bb-31901d0245c7';
const INFO = '6edddece-b1f3-4cfb-9f64-127e68a2b9d3';
const PROVISION = '3eae6aab-3e28-4768-b019-affe73047fd7';
const TOKEN = '692d6042-d8fa-498b-a4db-8a7401d47baf';
const SAVED_DEVICE = 'motif-display-id';
const SAVED_TOKEN = 'motif-display-token';
const SAVED_IP = 'motif-display-ip';
const SAVED_NAME = 'motif-display-name';
let started = false;
let uploadInProgress = false;

export function isDisplayUploadInProgress() {
  return uploadInProgress;
}

async function fetchWithDeadline(url: string, options: Parameters<typeof fetch>[1], timeoutMs: number) {
  const controller = new AbortController();
  let timer: ReturnType<typeof setTimeout> | undefined;
  try {
    return await Promise.race([
      fetch(url, { ...options, signal: controller.signal }),
      new Promise<never>((_, reject) => {
        timer = setTimeout(() => {
          controller.abort();
          reject(new Error('The display did not respond in time.'));
        }, timeoutMs);
      }),
    ]);
  } finally {
    if (timer) clearTimeout(timer);
  }
}

export type MotifDevice = { id: string; name: string };

export function displayError(error: unknown, fallback: string) {
  if (error instanceof Error && error.message) {
    if (/could not find peripheral/i.test(error.message)) return 'Display was not found. Search again and select it.';
    return /peer removed pairing information/i.test(error.message)
      ? 'The display was reset. Search again, select it, then pair again.'
      : error.message;
  }
  if (typeof error === 'string' && error) {
    if (/could not find peripheral/i.test(error)) return 'Display was not found. Search again and select it.';
    return /peer removed pairing information/i.test(error)
      ? 'The display was reset. Search again, select it, then pair again.'
      : error;
  }
  if (error && typeof error === 'object') {
    const value = error as { message?: unknown; error?: unknown; reason?: unknown };
    for (const part of [value.message, value.error, value.reason]) {
      if (typeof part === 'string' && part) return displayError(part, fallback);
    }
  }
  return fallback;
}

function bytesToHex(bytes: number[]) {
  return bytes.map((value) => value.toString(16).padStart(2, '0')).join('');
}

async function startBle() {
  if (Platform.OS === 'web') throw new Error('Connect to a display from the iOS or Android app.');
  if (Platform.OS === 'android') {
    const permissions = Number(Platform.Version) >= 31
      ? [PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN, PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT]
      : [PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION];
    const result = await PermissionsAndroid.requestMultiple(permissions);
    if (Object.values(result).some((value) => value !== PermissionsAndroid.RESULTS.GRANTED)) {
      throw new Error('Bluetooth permission is needed to find your display.');
    }
  }
  if (!started) {
    await BleManager.start();
    started = true;
    recordConnection('Bluetooth', 'Ready');
  }
}

export async function scanDisplays(): Promise<MotifDevice[]> {
  await startBle();
  recordConnection('Discovery', 'Checking connected displays');
  // The iOS manager caches a newly retrieved connected peripheral on its first query.
  await BleManager.getConnectedPeripherals([SERVICE]);
  const connected = await BleManager.getConnectedPeripherals([SERVICE]);
  if (connected.length) {
    recordConnection('Discovery', `${connected.length} connected display found`);
    return connected.map((device) => ({ id: device.id, name: device.name ?? device.advertising?.localName ?? 'Motif display' }));
  }
  const found = new Map<string, MotifDevice>();
  const subscription = BleManager.onDiscoverPeripheral((device) => {
    const name = device.name ?? device.advertising?.localName ?? '';
    if (name.startsWith('MOTIF-')) found.set(device.id, { id: device.id, name });
  });
  try {
    recordConnection('Discovery', 'Scanning nearby displays');
    await BleManager.scan({ serviceUUIDs: [SERVICE], seconds: 8, allowDuplicates: false });
    await new Promise<void>((resolve) => {
      const stopped = BleManager.onStopScan(() => { stopped.remove(); resolve(); });
      setTimeout(() => { stopped.remove(); resolve(); }, 9000);
    });
    recordConnection('Discovery', `${found.size} nearby display found`);
    return [...found.values()];
  } finally {
    subscription.remove();
    await BleManager.stopScan().catch(() => {});
  }
}

async function connected(id: string) {
  await startBle();
  try {
    recordConnection('Bluetooth', 'Connecting to saved display');
    if (!(await BleManager.isPeripheralConnected(id, [SERVICE]))) await BleManager.connect(id);
    await BleManager.retrieveServices(id, [SERVICE]);
    recordConnection('Bluetooth', 'Services ready');
  } catch (error) {
    const message = displayError(error, 'Could not connect to the display.');
    recordConnection('Bluetooth error', message);
    throw new Error(message);
  }
}

async function readInfo(id: string) {
  const data = await BleManager.read(id, SERVICE, INFO);
  if (data.length !== 16 || data[0] !== 1) throw new Error('Unsupported display firmware.');
  const ip = data.slice(2, 6);
  const maxBytes = data[12] + data[13] * 256 + data[14] * 65536 + data[15] * 16777216;
  const deviceId = data.slice(9, 12).map((part) => part.toString(16).padStart(2, '0')).join('').toUpperCase();
  return { ip: data[1] && ip.every((part) => part >= 0 && part <= 255) ? ip.join('.') : null, maxBytes, name: `MOTIF-${deviceId}` };
}

export async function pairDisplay(id: string, ssid: string, password: string) {
  recordConnection('Pairing', 'Starting authenticated Bluetooth pairing');
  await connected(id);
  if (Platform.OS === 'android') await BleManager.createBond(id);
  const token = await BleManager.read(id, SERVICE, TOKEN); // Triggers authenticated system pairing.
  if (token.length !== 16) throw new Error('Display pairing failed.');
  recordConnection('Pairing', 'Upload key received securely');
  await SecureStore.setItemAsync(SAVED_DEVICE, id);
  await SecureStore.setItemAsync(SAVED_TOKEN, bytesToHex(token));
  await SecureStore.deleteItemAsync(SAVED_IP);
  if (ssid.trim()) {
    recordConnection('Wi-Fi setup', 'Sending network details over encrypted Bluetooth');
    const nameBytes = Array.from(new TextEncoder().encode(ssid.trim()));
    const passBytes = Array.from(new TextEncoder().encode(password));
    if (nameBytes.length > 32 || passBytes.length > 63) throw new Error('Wi-Fi name or password is too long.');
    const payload = [nameBytes.length, passBytes.length, ...nameBytes, ...passBytes];
    await BleManager.write(id, SERVICE, PROVISION, [1, payload.length & 255, payload.length >> 8]);
    for (let offset = 0; offset < payload.length; offset += 17) {
      await BleManager.write(id, SERVICE, PROVISION, [2, offset & 255, offset >> 8, ...payload.slice(offset, offset + 17)]);
    }
    await BleManager.write(id, SERVICE, PROVISION, [3]);
    recordConnection('Wi-Fi setup', 'Board accepted network details');
  }
  let info = await readInfo(id);
  await SecureStore.setItemAsync(SAVED_NAME, info.name);
  for (let attempt = 0; attempt < 15 && !info.ip; attempt++) {
    await new Promise((resolve) => setTimeout(resolve, 1000));
    try { info = await readInfo(id); }
    catch { await connected(id).catch(() => {}); }
  }
  if (info.ip) await SecureStore.setItemAsync(SAVED_IP, info.ip);
  recordConnection('Pairing', info.ip ? `Board joined Wi-Fi at ${info.ip}` : 'Board has not joined Wi-Fi yet');
  return info;
}

export async function savedDisplay() {
  const [id, token, ip, name] = await Promise.all([
    SecureStore.getItemAsync(SAVED_DEVICE), SecureStore.getItemAsync(SAVED_TOKEN),
    SecureStore.getItemAsync(SAVED_IP), SecureStore.getItemAsync(SAVED_NAME),
  ]);
  return id && token ? { id, token, ip, name: name ?? 'Motif display' } : null;
}

async function readLanStatus(ip: string, token: string) {
  recordConnection('LAN status', `Checking ${ip}:8080`);
  const response = await fetchWithDeadline(`http://${ip}:8080/v1/status`, {
    headers: { Authorization: `Bearer ${token}` },
  }, 5000);
  if (response.status === 401) throw new Error('Pair this display again.');
  if (!response.ok) throw new Error(`Display status failed (${response.status}).`);
  recordConnection('LAN status', 'Authenticated service responded');
  return response.json() as Promise<{ state: string }>;
}

export async function getDisplayStatus(allowDuringUpload = false) {
  const saved = await savedDisplay();
  if (!saved) return null;
  if (uploadInProgress && !allowDuringUpload) return { name: saved.name, ip: saved.ip, reachable: true, playback: 'uploading' };
  if (saved.ip) {
    try {
      const status = await readLanStatus(saved.ip, saved.token);
      return { name: saved.name, ip: saved.ip, reachable: true, playback: status.state };
    } catch (error) {
      if (displayError(error, '').startsWith('Pair this display again.')) throw error;
      recordConnection('LAN status', 'Cached address did not respond; refreshing over Bluetooth');
    }
  }
  let info;
  try {
    await connected(saved.id);
    info = await readInfo(saved.id);
  } catch {
    recordConnection('Status', 'Bluetooth could not refresh the board address');
    return { name: saved.name, ip: saved.ip, reachable: false, playback: null };
  }
  await SecureStore.setItemAsync(SAVED_NAME, info.name);
  if (!info.ip) return { name: info.name, ip: null, reachable: false, playback: null };
  await SecureStore.setItemAsync(SAVED_IP, info.ip);
  recordConnection('Status', `Current board address ${info.ip}`);
  try {
    const status = await readLanStatus(info.ip, saved.token);
    return { name: info.name, ip: info.ip, reachable: true, playback: status.state };
  } catch (error) {
    if (displayError(error, '').startsWith('Pair this display again.')) throw error;
    return { name: info.name, ip: info.ip, reachable: false, playback: null };
  }
}

export type BoardEvent = { ms: number; event: string; value: number };

export async function getDisplayDiagnostics(ip: string): Promise<BoardEvent[]> {
  const saved = await savedDisplay();
  if (!saved) return [];
  const response = await fetchWithDeadline(`http://${ip}:8080/v1/diagnostics`, {
    headers: { Authorization: `Bearer ${saved.token}` },
  }, 5000);
  if (!response.ok) throw new Error(`Device log unavailable (${response.status}).`);
  const result = await response.json() as { events: BoardEvent[] };
  return result.events;
}

async function sendAnimation(file: File, url: string, token: string, onProgress?: (percent: number) => void) {
  const controller = new AbortController();
  const deadline = setTimeout(() => controller.abort(), 180000);
  const task = file.createUploadTask(url, {
    httpMethod: 'POST',
    uploadType: UploadType.BINARY_CONTENT,
    mimeType: 'application/x-motif-animation',
    headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/x-motif-animation' },
    signal: controller.signal,
    onProgress: ({ bytesSent, totalBytes }) => onProgress?.(totalBytes ? bytesSent / totalBytes : 0),
  });
  try {
    const response = await task.uploadAsync();
    return { status: response.status, body: response.body };
  } finally {
    clearTimeout(deadline);
    task.release();
  }
}

async function sendWithRetry(file: File, url: string, token: string, onProgress?: (percent: number) => void) {
  try {
    return await sendAnimation(file, url, token, onProgress);
  } catch (error) {
    const message = displayError(error, 'network connection closed');
    recordConnection('Upload error', message);
    recordConnection('Upload', 'Retrying once');
    try {
      return await sendAnimation(file, url, token, onProgress);
    } catch (retryError) {
      const retryMessage = displayError(retryError, 'network connection closed');
      recordConnection('Upload error', retryMessage);
      throw new Error(`Upload failed: ${retryMessage}`);
    }
  }
}

export async function uploadAnimation(assetModule: number, onProgress?: (percent: number) => void): Promise<void> {
  if (uploadInProgress) throw new Error('An upload is already running.');
  uploadInProgress = true;
  let syncTimer: ReturnType<typeof setInterval> | undefined;
  try {
    recordConnection('Upload', 'Checking paired display');
    const saved = await savedDisplay();
    if (!saved) throw new Error('Connect your display first.');
    const display = await getDisplayStatus(true);
    if (!display?.reachable || !display.ip) throw new Error('The display is not reachable. Check Wi-Fi in Settings.');
    const asset = await Asset.fromModule(assetModule).downloadAsync();
    if (!asset.localUri) throw new Error('Animation file is unavailable.');
    const file = new File(asset.localUri);
    if (file.size > 4 * 1024 * 1024) throw new Error('Animation is too large for this display.');
    recordConnection('Upload', `Loaded ${file.size} animation bytes`);
    let sentBucket = -1;
    let lastDevicePercent = 0;
    let pollInFlight = false;
    const progress = (percent: number) => {
      onProgress?.(percent);
    };
    const nativeProgress = (percent: number) => {
      const bucket = Math.min(10, Math.floor(percent * 10));
      if (bucket > sentBucket) {
        sentBucket = bucket;
        recordConnection('Upload', `Sent ${bucket * 10}%`);
      }
    };
    const base = `http://${display.ip}:8080`;
    const headers = { Authorization: `Bearer ${saved.token}` };
    syncTimer = setInterval(async () => {
      if (pollInFlight) return;
      pollInFlight = true;
      try {
        const response = await fetchWithDeadline(`http://${display.ip}:8081/v1/progress`, { headers }, 2000);
        if (!response.ok) return;
        const status = await response.json() as { uploadBytes?: number; uploadTotal?: number };
        if (typeof status.uploadBytes !== 'number' || typeof status.uploadTotal !== 'number') return;
        if (status.uploadTotal <= 0 || status.uploadBytes > status.uploadTotal) return;
        const percent = Math.floor((status.uploadBytes * 100) / status.uploadTotal) / 100;
        if (percent > lastDevicePercent || lastDevicePercent - percent > 0.2) {
          lastDevicePercent = percent;
          progress(percent);
        }
      } catch {
        // Device polls are best-effort; the upload result itself is authoritative.
      } finally {
        pollInFlight = false;
      }
    }, 250);
    recordConnection('Upload', `Sending to ${display.ip}:8080`);
    const result = await sendWithRetry(file, `${base}/v1/animation`, saved.token, nativeProgress);
    recordConnection('Upload', `Board response HTTP ${result.status}`);
    if (result.status !== 202) {
      const reason = result.body.trim();
      throw new Error(`Display rejected the animation (${result.status})${reason ? `: ${reason}` : '.'}`);
    }
    let accepted: { generation: number } | null;
    try {
      accepted = JSON.parse(result.body) as { generation: number };
    } catch {
      accepted = null;
    }
    if (!accepted) throw new Error('Display confirmation was unreadable.');
    recordConnection('Playback', `Waiting for generation ${accepted.generation}`);
    // The board validates every compressed frame before committing playback.
    const playbackDeadline = Date.now() + 120_000;
    while (Date.now() < playbackDeadline) {
      await new Promise((resolve) => setTimeout(resolve, 400));
      const check = await fetchWithDeadline(`${base}/v1/status`, { headers }, 10000);
      if (!check.ok) throw new Error('Display status could not be confirmed.');
      const status = await check.json() as { generation: number; state: string };
      if (status.generation === accepted.generation && status.state === 'playing') {
        recordConnection('Playback', `Generation ${accepted.generation} playing`);
        return;
      }
      if (status.generation === accepted.generation && status.state === 'error') {
        try {
          const events = await getDisplayDiagnostics(display.ip);
          const failure = events.slice().reverse().find((event) => event.event.includes('failed') || event.event.includes('invalid'));
          if (failure) throw new Error(`Display playback failed: ${failure.event} (${failure.value}).`);
        } catch (error) {
          if (error instanceof Error && error.message.startsWith('Display playback failed:')) throw error;
        }
        throw new Error('Display could not play this animation.');
      }
    }
    throw new Error('The upload finished, but playback was not confirmed.');
  } finally {
    if (syncTimer) clearInterval(syncTimer);
    uploadInProgress = false;
  }
}

