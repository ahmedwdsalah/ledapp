import { useIsFocused } from 'expo-router';
import { StatusBar } from 'expo-status-bar';
import { useCallback, useEffect, useRef, useState } from 'react';
import { ActivityIndicator, Pressable, ScrollView, Share, StyleSheet, Text, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { ConnectDisplay } from '@/device/connect-display';
import { formatConnectionLog, getConnectionLog, recordConnection, subscribeConnectionLog } from '@/device/connection-log';
import { displayError, getDisplayDiagnostics, getDisplayStatus, isDisplayUploadInProgress, savedDisplay, type BoardEvent } from '@/device/motif-device';

type DisplayStatus = NonNullable<Awaited<ReturnType<typeof getDisplayStatus>>>;

export default function SettingsScreen() {
  const focused = useIsFocused();
  const insets = useSafeAreaInsets();
  const [pairedId, setPairedId] = useState<string | null>(null);
  const [status, setStatus] = useState<DisplayStatus | null>(null);
  const [message, setMessage] = useState('');
  const [loading, setLoading] = useState(false);
  const [connectOpen, setConnectOpen] = useState(false);
  const [showAllLogs, setShowAllLogs] = useState(false);
  const [connectionLog, setConnectionLog] = useState(getConnectionLog);
  const [boardLog, setBoardLog] = useState<BoardEvent[]>([]);
  const refreshInFlight = useRef(false);
  const seenBoardEvents = useRef(new Set<string>());

  useEffect(() => subscribeConnectionLog(() => setConnectionLog(getConnectionLog())), []);

  const refresh = useCallback(async () => {
    if (refreshInFlight.current || isDisplayUploadInProgress()) return;
    refreshInFlight.current = true;
    setLoading(true);
    setMessage('');
    try {
      const paired = await savedDisplay();
      setPairedId(paired?.id ?? null);
      if (!paired) { setStatus(null); return; }
      const next = await getDisplayStatus();
      setStatus(next);
      if (next && !next.reachable) setMessage(next.ip ? 'Display joined Wi-Fi, but the upload service is unavailable.' : 'Display is not connected to Wi-Fi.');
      if (next?.reachable && next.ip) {
        try {
          const events = await getDisplayDiagnostics(next.ip);
          setBoardLog(events);
          for (const event of events) {
            const key = `${event.ms}:${event.event}:${event.value}`;
            if (!seenBoardEvents.current.has(key)) {
              seenBoardEvents.current.add(key);
              recordConnection('Board', `${event.ms}ms ${event.event}: ${event.value}`);
            }
          }
        }
        catch { setBoardLog([]); }
      } else setBoardLog([]);
    } catch (error) {
      setStatus(null);
      setMessage(displayError(error, 'Could not reach the display over Bluetooth.'));
    } finally { refreshInFlight.current = false; setLoading(false); }
  }, []);

  useEffect(() => {
    if (!focused) return;
    const timer = setTimeout(() => void refresh(), 0);
    return () => clearTimeout(timer);
  }, [focused, refresh]);

  return <>
    <ScrollView style={styles.root} contentContainerStyle={{ paddingTop: insets.top + 18, paddingBottom: insets.bottom + 110 }}>
      <StatusBar style="light" />
      <Text style={styles.title}>Settings</Text>
      <Text style={styles.section}>Display</Text>
      <View style={styles.statusRow}>
        <View style={[styles.dot, { backgroundColor: status?.reachable ? '#4CD177' : '#7B7B80' }]} />
        <Text style={styles.status}>{loading && !status ? 'Checking…' : status?.reachable ? 'Connected over Wi-Fi' : pairedId ? 'Cannot reach display' : 'Not linked to this phone'}</Text>
        {loading && <ActivityIndicator color="#F05850" />}
      </View>
      {status && <>
        <Text style={styles.name}>{status.name}</Text>
        {status.ip && <Text style={styles.detail}>Wi-Fi · {status.ip}</Text>}
        {status.reachable && <Text style={styles.detail}>Playback · {status.playback}</Text>}
      </>}
      {!!message && <Text style={styles.message}>{message}</Text>}
      <View style={styles.actions}>
        <Pressable accessibilityRole="button" onPress={() => void refresh()} disabled={loading} style={styles.action}>
          <Text style={styles.actionText}>Refresh status</Text>
        </Pressable>
        <Pressable accessibilityRole="button" onPress={() => setConnectOpen(true)} style={styles.action}>
          <Text style={styles.actionText}>{pairedId ? 'Reconnect or change Wi-Fi' : 'Connect display'}</Text>
        </Pressable>
      </View>
      <View style={styles.logHeader}>
        <Text style={styles.section}>Connection log</Text>
        <Pressable accessibilityRole="button" onPress={() => void Share.share({ message: `${formatConnectionLog()}\n\nBoard events\n${boardLog.map((event) => `${event.ms}ms ${event.event}: ${event.value}`).join('\n')}` })} disabled={!connectionLog.length && !boardLog.length}>
          <Text style={styles.share}>Share all</Text>
        </Pressable>
      </View>
      {connectionLog.length === 0 && <Text style={styles.detail}>No connection activity yet.</Text>}
      {(showAllLogs ? connectionLog : connectionLog.slice(-12)).slice().reverse().map((entry, index) => (
        <View key={`${entry.time}-${index}`} style={styles.logRow}>
          <Text style={styles.logTime}>{entry.time.slice(11, 19)}</Text>
          <Text style={styles.logText}>{entry.stage}: {entry.detail}</Text>
        </View>
      ))}
      {connectionLog.length > 12 && <Pressable accessibilityRole="button" onPress={() => setShowAllLogs(!showAllLogs)} style={styles.action}>
        <Text style={styles.actionText}>{showAllLogs ? 'Show recent' : `Show all ${connectionLog.length} events`}</Text>
      </Pressable>}
      <Text style={[styles.section, { marginTop: 30 }]}>Board events</Text>
      {boardLog.length === 0 && <Text style={styles.detail}>No board events available.</Text>}
      {boardLog.slice().reverse().map((event, index) => (
        <View key={`${event.ms}-${index}`} style={styles.logRow}>
          <Text style={styles.logTime}>{(event.ms / 1000).toFixed(1)}s</Text>
          <Text style={styles.logText}>{event.event} · {event.value}</Text>
        </View>
      ))}
    </ScrollView>
    <ConnectDisplay
      visible={connectOpen}
      onClose={() => setConnectOpen(false)}
      onConnected={() => { setConnectOpen(false); void refresh(); }}
    />
  </>;
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: '#08090B', paddingHorizontal: 24 },
  title: { color: '#F6F4F0', fontSize: 37, fontWeight: '700', letterSpacing: -1.4, marginBottom: 32 },
  section: { color: '#F6F4F0', fontSize: 18, fontWeight: '600', marginBottom: 20 },
  statusRow: { flexDirection: 'row', alignItems: 'center', gap: 10, minHeight: 32 },
  dot: { width: 10, height: 10, borderRadius: 5 },
  status: { color: '#F6F4F0', fontSize: 17, fontWeight: '600', flex: 1 },
  name: { color: '#E4E4E7', fontSize: 15, marginTop: 19 },
  detail: { color: '#99999E', fontSize: 14, marginTop: 8 },
  message: { color: '#E8A59F', fontSize: 14, lineHeight: 21, marginTop: 18 },
  actions: { marginTop: 30, borderTopWidth: 1, borderTopColor: '#303034' },
  action: { minHeight: 54, justifyContent: 'center', borderBottomWidth: 1, borderBottomColor: '#303034' },
  actionText: { color: '#F6F4F0', fontSize: 16, fontWeight: '600' },
  logHeader: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', marginTop: 34 },
  share: { color: '#F58A82', fontSize: 14, fontWeight: '600', paddingVertical: 10 },
  logRow: { flexDirection: 'row', gap: 14, paddingVertical: 9, borderBottomWidth: 1, borderBottomColor: '#242428' },
  logTime: { color: '#898990', fontSize: 12, width: 63, fontVariant: ['tabular-nums'] },
  logText: { color: '#D8D8DC', fontSize: 13, lineHeight: 18, flex: 1 },
});
