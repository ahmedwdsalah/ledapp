import { useEffect, useState } from 'react';
import { ActivityIndicator, Modal, Pressable, ScrollView, StyleSheet, Text, TextInput, View } from 'react-native';
import { displayError, getDisplayStatus, pairDisplay, scanDisplays, type MotifDevice } from './motif-device';
import { recordConnection } from './connection-log';

export function ConnectDisplay({ visible, onClose, onConnected }: {
  visible: boolean;
  onClose: () => void;
  onConnected: () => void;
}) {
  const [devices, setDevices] = useState<MotifDevice[]>([]);
  const [selected, setSelected] = useState<MotifDevice | null>(null);
  const [ssid, setSsid] = useState('');
  const [password, setPassword] = useState('');
  const [busy, setBusy] = useState(false);
  const [waiting, setWaiting] = useState(false);
  const [message, setMessage] = useState('');
  const activeDevice = selected;

  useEffect(() => {
    if (!visible || !waiting) return;
    let cancelled = false;
    let checking = false;
    const started = Date.now();
    const timer = setInterval(async () => {
      if (checking || cancelled) return;
      if (Date.now() - started > 30000) {
        clearInterval(timer);
        setWaiting(false);
        setMessage('The display did not join Wi-Fi. Check the network details and try again.');
        return;
      }
      checking = true;
      try {
        const status = await getDisplayStatus();
        if (!cancelled && status?.reachable) {
          clearInterval(timer);
          setWaiting(false);
          setSelected(null);
          onConnected();
        }
      } catch { /* The display may be switching from Bluetooth to Wi-Fi. */ }
      finally { checking = false; }
    }, 2000);
    return () => { cancelled = true; clearInterval(timer); };
  }, [visible, waiting, onConnected]);

  function close() { setWaiting(false); setSelected(null); onClose(); }

  async function search() {
    setWaiting(false); setSelected(null); setBusy(true); setMessage('Searching for nearby displays…');
    try {
      const next = await scanDisplays();
      setDevices(next);
      if (next.length === 1) setSelected(next[0]);
      setMessage(next.length ? '' : 'No Motif display found nearby.');
    } catch (error) {
      const detail = displayError(error, 'Could not search for displays. Check Bluetooth and try again.');
      recordConnection('Discovery error', detail);
      setMessage(detail);
    }
    finally { setBusy(false); }
  }

  async function connect() {
    if (!activeDevice) return;
    setBusy(true); setMessage('Check the pairing code on your display.');
    try {
      const info = await pairDisplay(activeDevice.id, ssid, password);
      const status = info.ip ? await getDisplayStatus() : null;
      if (status?.reachable) { setMessage('Display connected.'); setSelected(null); onConnected(); }
      else { setMessage('Paired. Waiting for the display to join Wi-Fi.'); setWaiting(true); }
    } catch (error) {
      const detail = displayError(error, 'Could not connect. Check the Wi-Fi details and try again.');
      recordConnection('Pairing error', detail);
      setMessage(detail);
    }
    finally { setBusy(false); }
  }

  return <Modal visible={visible} animationType="slide" transparent onRequestClose={close}>
    <View style={styles.backdrop}>
      <View style={styles.sheet}>
        <View style={styles.header}>
          <Text style={styles.heading}>Connect display</Text>
          <Pressable accessibilityRole="button" accessibilityLabel="Close" onPress={close}><Text style={styles.close}>Done</Text></Pressable>
        </View>
        <ScrollView keyboardShouldPersistTaps="handled">
          <Text style={styles.caption}>Choose your Motif display. The pairing code appears on its screen.</Text>
          <Pressable accessibilityRole="button" onPress={search} disabled={busy} style={styles.action}><Text style={styles.actionText}>{devices.length ? 'Search again' : 'Find display'}</Text></Pressable>
          {devices.map((device) => <Pressable key={device.id} accessibilityRole="button" onPress={() => setSelected(device)} style={[styles.device, activeDevice?.id === device.id && styles.selected]}><Text style={styles.deviceText}>{device.name}</Text></Pressable>)}
          {activeDevice && <>
            <Text style={styles.fieldLabel}>Wi-Fi network</Text>
            <TextInput value={ssid} onChangeText={setSsid} autoCapitalize="none" autoCorrect={false} placeholder="Network name" placeholderTextColor="#888" style={styles.input} accessibilityLabel="Wi-Fi network name" />
            <TextInput value={password} onChangeText={setPassword} secureTextEntry placeholder="Password" placeholderTextColor="#888" style={styles.input} accessibilityLabel="Wi-Fi password" />
            <Text style={styles.caption}>Leave Wi-Fi blank if the display is already connected.</Text>
            <Pressable accessibilityRole="button" onPress={connect} disabled={busy || waiting} style={styles.action}><Text style={styles.actionText}>{waiting ? 'Waiting for Wi-Fi…' : 'Pair and connect'}</Text></Pressable>
          </>}
          {(busy || waiting) && <ActivityIndicator color="#F05850" style={{ marginTop: 18 }} />}
          {!!message && <Text style={styles.message}>{message}</Text>}
        </ScrollView>
      </View>
    </View>
  </Modal>;
}

const styles = StyleSheet.create({
  backdrop: { flex: 1, justifyContent: 'flex-end', backgroundColor: '#0009' },
  sheet: { maxHeight: '82%', minHeight: 340, padding: 24, paddingBottom: 46, borderTopLeftRadius: 28, borderTopRightRadius: 28, backgroundColor: '#1B1B1D' },
  header: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', marginBottom: 18 },
  heading: { color: '#fff', fontSize: 25, fontWeight: '700' },
  close: { color: '#F58A82', fontSize: 16, fontWeight: '600' },
  caption: { color: '#AAA', fontSize: 14, lineHeight: 20, marginBottom: 14 },
  action: { backgroundColor: '#F05850', paddingVertical: 14, alignItems: 'center', borderRadius: 13, marginTop: 8 },
  actionText: { color: '#fff', fontSize: 16, fontWeight: '700' },
  device: { padding: 17, marginVertical: 5, borderRadius: 13, backgroundColor: '#29292C' },
  selected: { borderColor: '#F05850', borderWidth: 1 },
  deviceText: { color: '#fff', fontSize: 16, fontWeight: '600' },
  fieldLabel: { color: '#fff', fontSize: 15, fontWeight: '600', marginTop: 18, marginBottom: 8 },
  input: { color: '#fff', backgroundColor: '#29292C', borderRadius: 12, paddingHorizontal: 16, paddingVertical: 14, marginBottom: 10 },
  message: { color: '#DDD', marginTop: 18, lineHeight: 21 },
});
