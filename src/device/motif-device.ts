import { Asset } from 'expo-asset';
import { fetch } from 'expo/fetch';
import { File } from 'expo-file-system';
import * as SecureStore from 'expo-secure-store';
import BleManager from 'react-native-ble-manager';
import { PermissionsAndroid, Platform } from 'react-native';

export const SERVICE = 'c4683809-f301-4312-83bb-31901d0245c7';
const INFO = '6edddece-b1f3-4cfb-9f64-127e68a2b9d3';
const PROVISION = '3eae6aab-3e28-4768-b019-affe73047fd7';
const TOKEN = '692d6042-d8fa-498b-a4db-8a7401d47baf';
const SAVED_DEVICE = 'motif-display-id';
const SAVED_TOKEN = 'motif-display-token';
let started = false;

export type MotifDevice = { id: string; name: string };

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
  }
}

export async function scanDisplays(): Promise<MotifDevice[]> {
  await startBle();
  const found = new Map<string, MotifDevice>();
  const subscription = BleManager.onDiscoverPeripheral((device) => {
    const name = device.name ?? device.advertising?.localName ?? '';
    if (name.startsWith('MOTIF-')) found.set(device.id, { id: device.id, name });
  });
  try {
    await BleManager.scan({ serviceUUIDs: [SERVICE], seconds: 8, allowDuplicates: false });
    await new Promise<void>((resolve) => {
      const stopped = BleManager.onStopScan(() => { stopped.remove(); resolve(); });
      setTimeout(() => { stopped.remove(); resolve(); }, 9000);
    });
    return [...found.values()];
  } finally {
    subscription.remove();
    await BleManager.stopScan().catch(() => {});
  }
}

async function connected(id: string) {
  await startBle();
  if (!(await BleManager.isPeripheralConnected(id, [SERVICE]))) await BleManager.connect(id);
  await BleManager.retrieveServices(id, [SERVICE]);
}

async function readInfo(id: string) {
  const data = await BleManager.read(id, SERVICE, INFO);
  if (data.length !== 16 || data[0] !== 1) throw new Error('Unsupported display firmware.');
  const ip = data.slice(2, 6);
  const maxBytes = data[12] + data[13] * 256 + data[14] * 65536 + data[15] * 16777216;
  return { ip: data[1] && ip.every((part) => part >= 0 && part <= 255) ? ip.join('.') : null, maxBytes };
}

export async function pairDisplay(id: string, ssid: string, password: string) {
  await connected(id);
  if (Platform.OS === 'android') await BleManager.createBond(id);
  const token = await BleManager.read(id, SERVICE, TOKEN); // Triggers authenticated system pairing.
  if (token.length !== 16) throw new Error('Display pairing failed.');
  if (ssid.trim()) {
    const nameBytes = Array.from(new TextEncoder().encode(ssid.trim()));
    const passBytes = Array.from(new TextEncoder().encode(password));
    if (nameBytes.length > 32 || passBytes.length > 63) throw new Error('Wi-Fi name or password is too long.');
    const payload = [nameBytes.length, passBytes.length, ...nameBytes, ...passBytes];
    await BleManager.write(id, SERVICE, PROVISION, [1, payload.length & 255, payload.length >> 8]);
    for (let offset = 0; offset < payload.length; offset += 17) {
      await BleManager.write(id, SERVICE, PROVISION, [2, offset & 255, offset >> 8, ...payload.slice(offset, offset + 17)]);
    }
    await BleManager.write(id, SERVICE, PROVISION, [3]);
  }
  await SecureStore.setItemAsync(SAVED_DEVICE, id);
  await SecureStore.setItemAsync(SAVED_TOKEN, bytesToHex(token));
  for (let attempt = 0; attempt < 15; attempt++) {
    const info = await readInfo(id);
    if (info.ip) return info;
    await new Promise((resolve) => setTimeout(resolve, 1000));
  }
  return readInfo(id);
}

export async function savedDisplay() {
  const [id, token] = await Promise.all([
    SecureStore.getItemAsync(SAVED_DEVICE), SecureStore.getItemAsync(SAVED_TOKEN),
  ]);
  return id && token ? { id, token } : null;
}

export async function uploadAnimation(assetModule: number): Promise<void> {
  const saved = await savedDisplay();
  if (!saved) throw new Error('Connect your display first.');
  await connected(saved.id);
  const info = await readInfo(saved.id);
  if (!info.ip) throw new Error('Display is offline. Connect it to the same Wi-Fi network as your phone.');
  const asset = await Asset.fromModule(assetModule).downloadAsync();
  if (!asset.localUri) throw new Error('Animation file is unavailable.');
  const file = new File(asset.localUri);
  if (file.size > info.maxBytes) throw new Error('Animation is too large for this display.');
  const base = `http://${info.ip}:8080`;
  const headers = { Authorization: `Bearer ${saved.token}` };
  let response: Response;
  try {
    response = await fetch(`${base}/v1/animation`, {
      method: 'POST', headers: { ...headers, 'Content-Type': 'image/gif' }, body: file,
    });
  } catch {
    throw new Error('Could not reach the display. Put your phone on the same Wi-Fi.');
  }
  if (!response.ok) throw new Error(`Display rejected the animation (${response.status}).`);
  const accepted = await response.json() as { generation: number };
  for (let attempt = 0; attempt < 20; attempt++) {
    await new Promise((resolve) => setTimeout(resolve, 400));
    const check = await fetch(`${base}/v1/status`, { headers });
    if (!check.ok) throw new Error('Display status could not be confirmed.');
    const status = await check.json() as { generation: number; state: string };
    if (status.generation === accepted.generation && status.state === 'playing') return;
    if (status.generation === accepted.generation && status.state === 'error') throw new Error('Display could not play this animation.');
  }
  throw new Error('The upload finished, but playback was not confirmed.');
}
