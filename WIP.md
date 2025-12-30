# WIP: SSH Session Fixes

## Current Status
SSH connections are partially working. Key progress made:

1. **Fixed channel-based I/O**: Changed from `wolfSSH_stream_read/send` to `wolfSSH_worker` + `wolfSSH_ChannelIdRead/Send` pattern (following wolfSSH echoserver example)

2. **Fixed console assignment race condition**: Console is now marked as connected immediately in `accept_loop()` before creating the session, preventing multiple SSH connections from getting the same console

3. **Banner sending works**: SSH clients receive the "MP/M II Console N" banner

4. **MP/M output is being sent**: Debug shows `ChannelIdSend(142)` calls succeeding

## Current Issue
- SSH client receives banner but not MP/M output
- MP/M outputs to consoles 2 and 3 by default (when system starts before SSH connects)
- When SSH connects to console 0, that console has no active TMP output
- The 4-console test showed all consoles assigned correctly and ChannelIdSend working

## Key Files Modified
- `src/ssh_session.cpp` - Channel-based I/O, early console connection marking
- `src/xios.cpp` - Added missing `<iomanip>` header

## Next Steps to Debug
1. Run with DEBUG=1 to see which consoles MP/M outputs to
2. Check if SSH client receives the ChannelIdSend data (might be buffering issue)
3. Consider if wolfSSH needs a worker call after ChannelIdSend to flush
4. Test with real TTY allocation (`script -c "ssh ..."`)

## Test Commands
```bash
# Basic test (pipe stdin)
cd /home/wohl/src/mpm2
(timeout 20 ./build/mpm2_emu -s disks/mpm.sys -d A:disks/mpm2_system.img -p 2222 2>&1 &)
sleep 3
(sleep 2; echo "DIR"; sleep 5) | ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2222 test@localhost

# With debug
DEBUG=1 ./build/mpm2_emu -s disks/mpm.sys -d A:disks/mpm2_system.img -p 2222
```

## wolfSSH Pattern (from echoserver)
The echoserver uses:
1. `wolfSSH_worker(ssh, &lastChannel)` to process SSH messages
2. When `WS_CHAN_RXD` returned, call `wolfSSH_ChannelIdRead(ssh, channelId, buf, size)`
3. To send: `wolfSSH_ChannelIdSend(ssh, channelId, buf, size)`
