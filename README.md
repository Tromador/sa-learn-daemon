# sa-learn-daemon

A socket-activated daemon to run SpamAssassin’s `sa-learn` **without giving Dovecot or IMAPSieve direct execution permissions**.  
It is designed for systems with SELinux enforcing and strict security boundaries.

---

## Purpose

Dovecot’s IMAPSieve plugin can trigger learning actions (SPAM/HAM) but, under SELinux, direct execution of `sa-learn` from sieve scripts is often blocked.

This daemon provides a controlled interface:

- Listens on a Unix domain socket (systemd socket activation).
- Accepts a mode (`SPAM` or `HAM`) and username.
- Feeds the email to SpamAssassin’s `sa-learn` on behalf of the client.

This keeps the training step **outside Dovecot’s SELinux context**, while maintaining full confinement.

---

## Protocol

The client connects and sends:

```
SPAM username
<raw email message>
```

or

```
HAM username
<raw email message>
```

Details:

- **First line:**  
  - `SPAM` or `HAM`
  - Followed by a single-word username (no spaces)
- **Remaining data:**  
  Raw RFC 822 message until EOF

The daemon then runs:

```
/usr/bin/sa-learn --spam|--ham -u username -
```

and pipes the message body into `sa-learn` via stdin.

**No response data** is sent; the connection closes when complete.

---

## Default Socket

```
/var/mail/run/sa-learn.sock
```

(Defined in `include/config.h`.)

**Permissions:**

- Mode: `0660`
- Owner: `spamd`
- Group: `mail`

These are created and managed by systemd.

---

## Systemd Integration

Two units control the daemon:

- `sa-learn.socket`  
  Creates `/var/mail/run/sa-learn.sock`.  
  When a client connects, systemd automatically starts the service.

- `sa-learn.service`  
  Runs `sa_learn_daemon` as user `spamd`, group `mail`.

Systemd advantages:

- No persistent daemon when idle.
- Automatic restart on failure.

### Installation

Copy both files from `systemd/` into `/etc/systemd/system/` and run:

```bash
systemctl daemon-reload
systemctl enable --now sa-learn.socket
```

---

## Dovecot / Sieve Integration

Configure your Sieve scripts to call `socat` via a small wrapper.

Example `learn-spam.sieve`:

```
require ["fileinto", "imap4flags", "mailbox", "vnd.dovecot.pipe"];

if header :contains "X-Spam-Flag" "YES" {
  pipe :copy "/usr/local/bin/sa-learn-spam.sh";
}
```

### Wrapper scripts

Place these in `/usr/local/bin/` and make them executable (`chmod 755`):

#### sa-learn-spam.sh

```bash
#!/bin/bash
echo "SPAM $USER"
cat
```

#### sa-learn-ham.sh

```bash
#!/bin/bash
echo "HAM $USER"
cat
```

The `echo` writes the required first line to the socket; `cat` streams the message.

The Sieve script pipes to these scripts, and these scripts can internally call `socat`, for example:

```bash
#!/bin/bash
{
  echo "SPAM $USER"
  cat
} | /usr/bin/socat - UNIX-CONNECT:/var/mail/run/sa-learn.sock
```

---

## SELinux

When SELinux is **enforcing**, you must permit:

1. `dovecot_t` to **connect to the socket**.
2. Your daemon (running as `unconfined_service_t`) to handle the connection.

### Steps to Enable

1. Trigger a MOVE action in IMAP and look for AVC denials:

```bash
ausearch -m avc --start recent
```

You will see lines like:

```
avc: denied { connectto } for pid=... comm="socat"
  scontext=system_u:system_r:dovecot_t:s0
  tcontext=system_u:system_r:unconfined_service_t:s0
  tclass=unix_stream_socket
```

2. Generate a policy module:

```bash
grep dovecot_t /var/log/audit/audit.log | audit2allow -M dovecot_sa_learn
```

3. Edit `dovecot_sa_learn.te` to confirm it includes:

```
allow dovecot_t mail_spool_t:sock_file write;
allow dovecot_t unconfined_service_t:unix_stream_socket connectto;
```

4. Load the policy:

```bash
semodule -i dovecot_sa_learn.pp
```

After installing this module, IMAPSieve training works with SELinux enforcing.

---

## Build

```bash
make
```

Produces `sa_learn_daemon` in the project root.  
Copy it to `/usr/local/bin/`.

---

## Logging

- If `LOG_FILE_PATH` in `include/config.h` is non-empty, logs go to that file.
- Otherwise, messages are sent to syslog (facility configurable in `config.h`).

---

## License

BSD 3-Clause

---

## Summary of Workflow

1. IMAPSieve MOVE triggers a shell script (`sa-learn-spam.sh` or `sa-learn-ham.sh`).
2. The script sends `SPAM` or `HAM` plus username and pipes the mail content to `socat`.
3. `socat` connects to `/var/mail/run/sa-learn.sock`.
4. `sa_learn_daemon` (socket-activated) executes `sa-learn`.

With the SELinux policy module loaded, this works in full enforcing mode.

---

![Build](https://img.shields.io/badge/build-passing-brightgreen)
![SELinux](https://img.shields.io/badge/SELinux-enforcing-blue)
![License](https://img.shields.io/badge/license-BSD%203--Clause-lightgrey)
