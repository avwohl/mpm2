# Running the Emulator

## SSH Setup

Generate host key and configure user authentication:

```bash
mkdir -p keys

# Generate host key (required for SSH)
ssh-keygen -t rsa -b 2048 -m PEM -f keys/ssh_host_rsa_key -N ''
```

**Authentication options:**

| Mode | Configuration | Use Case |
|------|--------------|----------|
| Public key | `keys/authorized_keys` | Production - add user public keys |
| Open access | `--no-auth` flag | Development - accept any connection |

For public key authentication, add authorized public keys:
```bash
# Add your key
cat ~/.ssh/id_rsa.pub >> keys/authorized_keys

# Or multiple users
cat user1.pub user2.pub >> keys/authorized_keys
```

For development/testing without authentication:
```bash
./build/mpm2_emu --no-auth -d A:disks/mpm2_system.img
```

## Running

```bash
./build/mpm2_emu [options] -d A:diskimage

Options:
  -d, --disk A:FILE           Mount disk image (required)
  -l, --local                 Local console mode (output to stdout)
  -w, --http [IP:]PORT        HTTP server address (default: 8000, 0 to disable)
                              Can be repeated for multiple listeners
  --log FILE                  Access log file (default: mpm2.log)
  -p, --port [IP:]PORT        SSH listen address (default: 2222)
                              Can be repeated for multiple listeners
                              Use [IPv6]:PORT for IPv6 addresses
  -k, --key FILE              Host key file (default: keys/ssh_host_rsa_key)
  -a, --authorized-keys FILE  Authorized keys file (default: keys/authorized_keys)
  -n, --no-auth               Disable SSH authentication (accept any connection)
  -t, --timeout SECS          Timeout for debugging
  -h, --help                  Show help
```

The emulator boots from sector 0 of the disk mounted as drive A.

### Examples

```bash
# Local console - see output directly
./build/mpm2_emu -l -d A:disks/mpm2_system.img

# SSH mode - connect via ssh (requires keys/authorized_keys)
./build/mpm2_emu -d A:disks/mpm2_system.img
ssh -p 2222 user@localhost

# SSH mode without authentication (development only)
./build/mpm2_emu --no-auth -d A:disks/mpm2_system.img

# Bind to specific IP address
./build/mpm2_emu -p 127.0.0.1:2222 -w 127.0.0.1:8000 -d A:disks/mpm2_system.img

# Multiple listeners (IPv4 and IPv6)
./build/mpm2_emu -p 127.0.0.1:2222 -p '[::1]:2222' -w 8000 -d A:disks/mpm2_system.img
```

## Access Logging

The emulator logs HTTP, SSH, and SFTP access to a file (default: `mpm2.log`):

```
2026-01-06 23:21:19 [HTTP] 127.0.0.1 GET /
2026-01-06 23:21:26 [SSH] 127.0.0.1 connected
2026-01-06 23:21:26 [SSH] 127.0.0.1 auth user=test method=none
2026-01-06 23:21:26 [SSH] 127.0.0.1 exec command=exit
2026-01-06 23:21:29 [SSH] 127.0.0.1 disconnected
```

Each log entry includes:
- ISO timestamp (YYYY-MM-DD HH:MM:SS)
- Service type (HTTP, SSH, SFTP)
- Client IP address
- Event details (request path, auth method, command, etc.)

To use a different log file:
```bash
./build/mpm2_emu --log /var/log/mpm2.log -d A:disks/mpm2_system.img
```

## Troubleshooting

### "MPM SYS ?" error on boot
Run `./scripts/gensys.sh` to regenerate MPM.SYS with matching serial numbers.

### Build fails with "um80 not found"
Install um80/ul80: `pip install -e path/to/um80_and_friends`

### "dyld: Library not loaded: /usr/local/lib/libqkz80.4.dylib" (macOS)
An emulator linked before 0.3.6 took cpmemu's `libqkz80` dylib, which is only
at that path after cpmemu's `make install`. The build now links `libqkz80.a`
by path; remove `build/` and run `./scripts/build_all.sh` again.

### SSH connection refused
Ensure the emulator is running and check if port 2222 is available.

### No output after boot
Use `-l` flag for local console mode to see boot messages.
