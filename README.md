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

## Socket

Default socket path:

```
/var/mail/run/sa-learn.sock
```

(Defined in `include/config.h`.)

**Important:**  
We use `/var/mail/run/` rather than the usual `/run/` because **Dovecot runs inside a private filesystem namespace**.  
In that namespace, `/run/` from the host is not visible.  
A socket placed in `/var/mail/run/` is visible both to Dovecot and to system services, avoiding confusing “file not found” errors during IMAPSieve operations.

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
{
  echo "SPAM $USER"
  cat
} | /usr/bin/socat - UNIX-CONNECT:/var/mail/run/sa-learn.sock
```

#### sa-learn-ham.sh

```bash
#!/bin/bash
{
  echo "HAM $USER"
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

## Appendix: Sample SELinux Policy Module

**Note:**  
This example policy worked in one tested environment.  
Adjustments may be necessary depending on your distribution and customizations.  
Always validate using `audit2allow` output before deploying to production.

Save as `dovecot_sa_learn.te`:

```te
module dovecot_sa_learn 1.0;

require {
    type init_t;
    type dovecot_auth_t;
    type dovecot_t;
    type dovecot_tmp_t;
    type mail_spool_t;
    type unconfined_service_t;
    class process { noatsecure ptrace rlimitinh siginh };
    class sock_file write;
    class file unlink;
    class unix_stream_socket connectto;
}

# Allow Dovecot sieve scripts to talk to sa_learn_daemon
allow dovecot_t mail_spool_t:sock_file write;
allow dovecot_t unconfined_service_t:unix_stream_socket connectto;

# Allow internal Dovecot process interactions
allow dovecot_t dovecot_auth_t:process { noatsecure rlimitinh siginh };
allow dovecot_t self:process ptrace;

# Allow systemd to unlink leftover dovecot temp files
allow init_t dovecot_tmp_t:file unlink;
```

Build and install:

```bash
checkmodule -M -m -o dovecot_sa_learn.mod dovecot_sa_learn.te
semodule_package -o dovecot_sa_learn.pp -m dovecot_sa_learn.mod
semodule -i dovecot_sa_learn.pp
```

---

![Build](https://img.shields.io/badge/build-passing-brightgreen)
![SELinux](https://img.shields.io/badge/SELinux-enforcing-blue)
![License](https://img.shields.io/badge/license-BSD%203--Clause-lightgrey)
