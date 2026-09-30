# SFTP and HTTP File Access

## SFTP File Transfer

The emulator includes an integrated SFTP server for transferring files to and from the MP/M II disk. This allows you to use standard SFTP clients to upload, download, and manage files.

### Connecting

```bash
# Connect with sftp (same port as SSH terminal)
sftp -P 2222 user@localhost
```

### Path Format

SFTP paths use the format `/<drive>.<user>/<filename>`:

| Path | Description |
|------|-------------|
| `/A.0/` | Drive A, user 0 |
| `/B.3/TEST.COM` | Drive B, user 3, file TEST.COM |
| `/A.0/*.TXT` | Wildcard pattern for .TXT files |

### Supported Commands

| Command | Description |
|---------|-------------|
| `ls /A.0/` | List directory |
| `get /A.0/FILE.TXT` | Download file |
| `put local.txt /A.0/FILE.TXT` | Upload file |
| `rm /A.0/FILE.TXT` | Delete file |
| `rename /A.0/OLD.TXT /A.0/NEW.TXT` | Rename file |

### Example Session

```bash
sftp -P 2222 user@localhost
sftp> ls /A.0/
/A.0/GENHEX.COM     /A.0/LIB.COM     /A.0/LINK.COM
sftp> put myfile.txt /A.0/MYFILE.TXT
Uploading myfile.txt to /A.0/MYFILE.TXT
sftp> ls /A.0/MYFILE.TXT
/A.0/MYFILE.TXT
sftp> quit
```

### How It Works

SFTP operations are handled by an RSP (Resident System Process) running inside MP/M II. The C++ emulator receives SFTP protocol messages and forwards them to the Z80 RSP via a bridge interface. The RSP performs actual file operations using BDOS calls, ensuring proper file locking and consistency with MP/M II processes.

- The RSP takes one request at a time, and each stands on its own: a read
  or write opens the file, reads or writes up to 1920 bytes at the request's
  offset and closes it again, and nothing is left open in between.  SFTP
  sessions and HTTP clients can use it at the same time.
- A download reads the whole file through the RSP when the client opens it.
  The RSP opens a file it reads in read-only mode, as TYPE and PIP do, so a
  console program reading the same file is not refused.  A file a console
  program has open in locked mode (ED's file, say) cannot be read until it
  is closed: SFTP reports it missing, HTTP answers 404.
- The RSP runs in the BDOS's return-error mode: an error comes back to it
  as a status and is never printed on a console.
- An upload creates the file when the client opens it (`put` replaces one of
  the same name), and holds what the client writes until the client closes
  it, when it is written.  `put` returns only once it is all on the disk and
  closed, so a console can use the file straight away.
- CP/M files are whole 128-byte records.  An upload whose length is not a
  multiple of 128 is padded with ctl-Z (1AH), and a download returns whole
  records, padding included.

Files involved:
- `asm/sftp_brs.plm` - Z80 RSP code (PL/M-80)
- `asm/sftp_glue.asm` - Assembly glue for BDOS calls, taking its arguments
  as PL/M-80 passes them (BC, DE)
- `src/sftp_bridge.cpp` - C++ request/reply bridge
- `src/ssh_session_libssh.cpp` - SFTP protocol handling

## HTTP File Browser

The emulator includes a read-only HTTP server for browsing and downloading files from MP/M II disks using a web browser.

### Accessing

Open in any web browser:
```
http://localhost:8000/
```

### Path Format

| Path | Description |
|------|-------------|
| `/` | List mounted drives |
| `/a/` | Drive A - the listing is of user 0, as `/a.0/` |
| `/a.0/` | Drive A, user 0 only |
| `/a/file.txt` | Download file from drive A |
| `/a.0/file.txt` | Download file from drive A, user 0 |

- URLs are case-insensitive (`/A/FILE.TXT` and `/a/file.txt` both work)
- A file path without a user number (`/a/file.txt`) reads user 0
- Directory listings show filenames in lowercase
- Text files (.txt, .asm, .plm, etc.) are served with Unix line endings (CR stripped), ending at the first ctl-Z
- Other files are served as whole 128-byte records

### Configuration

```bash
# Default: HTTP on port 8000
./build/mpm2_emu -d A:disks/mpm2_system.img

# Custom port
./build/mpm2_emu -w 8080 -d A:disks/mpm2_system.img

# Disable HTTP server
./build/mpm2_emu -w 0 -d A:disks/mpm2_system.img
```

### How It Works

HTTP file operations share the same RSP bridge as SFTP. When an HTTP request arrives, it queues file requests to the Z80 RSP, which performs the actual disk reads via BDOS calls. Requests from HTTP and SFTP clients are served one at a time, and each opens and closes its file, so they can be interleaved safely.
