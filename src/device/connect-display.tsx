import { useState } from 'react';
import { ActivityIndicator, Modal, Pressable, ScrollView, StyleSheet, Text, TextInput, View } from 'react-native';
import { pairDisplay, scanDisplays, type MotifDevice } from './motif-device';

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
  const [message, setMessage] = useState('');

  async function search() {
    setBusy(true); setMessage('Searching for nearby displays…');
    try {
      const next = await scanDisplays();
      setDevices(next);
      setMessage(next.length ? '' : 'No Motif display found nearby.');
    } catch (error) { setMessage(error instanceof Error ? error.message : String(error)); }
    finally { setBusy(false); }
  }

  async function connect() {
    if (!selected) return;
    setBusy(true); setMessage('Check the pairing code on your display.');
    try {
      const info = await pairDisplay(selected.id, ssid, password);
      if (!info.ip) setMessage('Paired. Waiting for the display to join Wi-Fi.');
      else { setMessage('Display connected.'); onConnected(); }
    } catch (error) { setMessage(error instanceof Error ? error.message : String(error)); }
    finally { setBusy(false); }
  }

  return <Modal visible={visible} animationType="slide" transparent onRequestClose={onClose}>
    <View style={styles.backdrop}>
      <View style={styles.sheet}>
        <View style={styles.header}>
          <Text style={styles.heading}>Connect display</Text>
          <Pressable accessibilityRole="button" accessibilityLabel="Close" onPress={onClose}><Text style={styles.close}>Done</Text></Pressable>
        </View>
        <ScrollView keyboardShouldPersistTaps="handled">
          <Text style={styles.caption}>Choose your Motif display. The pairing code appears on its screen.</Text>
          <Pressable accessibilityRole="button" onPress={search} disabled={busy} style={styles.action}><Text style={styles.actionText}>{devices.length ? 'Search again' : 'Find display'}</Text></Pressable>
          {devices.map((device) => <Pressable key={device.id} accessibilityRole="button" onPress={() => setSelected(device)} style={[styles.device, selected?.id === device.id && styles.selected]}><Text style={styles.deviceText}>{device.name}</Text></Pressable>)}
          {selected && <>
            <Text style={styles.fieldLabel}>Wi-Fi network</Text>
            <TextInput value={ssid} onChangeText={setSsid} autoCapitalize="none" autoCorrect={false} placeholder="Network name" placeholderTextColor="#888" style={styles.input} accessibilityLabel="Wi-Fi network name" />
            <TextInput value={password} onChangeText={setPassword} secureTextEntry placeholder="Password" placeholderTextColor="#888" style={styles.input} accessibilityLabel="Wi-Fi password" />
            <Text style={styles.caption}>Leave Wi-Fi blank if the display is already connected.</Text>
            <Pressable accessibilityRole="button" onPress={connect} disabled={busy} style={styles.action}><Text style={styles.actionText}>Pair and connect</Text></Pressable>
          </>}
          {busy && <ActivityIndicator color="#F05850" style={{ marginTop: 18 }} />}
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
