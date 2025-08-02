# sa-learn-daemon

A small socket-activated daemon to run SpamAssassin’s `sa-learn` without granting Dovecot execute permissions.  
Designed for SELinux-constrained environments.

---

## Purpose

Dovecot/Sieve cannot execute `sa-learn` directly under strict SELinux policies.  
This daemon provides a safe interface:

* Listens on a Unix domain socket.
* Receives messages and classification instructions.
* Calls `sa-learn` on behalf of the client.

---

## Protocol

Client connects to the socket and sends:

```
SPAM username
<raw email message data>
```

or

```
HAM username
<raw email message data>
```

* The **first line** contains:
  * `SPAM` or `HAM`
  * A single word username (no spaces)
* The **rest of the stream** (until EOF) is the raw message to feed into `sa-learn`.

The daemon runs:

```
/usr/bin/sa-learn --spam|--ham -u username -
```

and pipes the message body to stdin of `sa-learn`.

The connection closes immediately after the message is read; there is no response data.

---

## Socket

Default socket path:

```
/var/run/sa-learn.sock
```

**Permissions:**  
* Mode: `0660`
* Owner: `spamassassin`
* Group: `mail`

SELinux: label the socket `mail_spool_t` and add:

```
allow dovecot_t mail_spool_t:sock_file connectto;
```

---

## Systemd Integration

Two units are installed:

* **sa-learn.socket** – creates `/var/run/sa-learn.sock`
* **sa-learn.service** – launched automatically on the first connection

Systemd handles:
* Socket lifecycle
* Daemon restart on failure

---

## Dovecot / Sieve Integration

From a Sieve script (or `pipe` rule), run:

```
pipe :copy "/usr/bin/socat - UNIX-CONNECT:/var/run/sa-learn.sock"
```

The script writes the message to the socket.  

If you want to learn **ham** (moving a message out of the Spam folder):

* Send `HAM username` as the first line before the message.

For **spam** (moving into the Spam folder):

* Send `SPAM username`.

---

## Build

```
make
```

Installs `sa_learn_daemon` in the project root.

---

## License

BSD 3-Clause.
