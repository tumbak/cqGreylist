# cqGreylist

A greylisting implementation for qmail written in C, intended for high volume servers.

Version 0.2:
* Fixed where an overflow was possible if the path to the temp files was longer than 256. (Thanks Khamis)
* Changed all syslog logging to stderr, that way it will be handled by multilog.
* Cleaned up the code quite a bit. (Thanks Khamis)

Version 0.1:
* Initial release.

This code has been in production for some time (since the 10th of February 2006) on a quite busy server (300,000 smtp connetions per day) and everything is working like a charm.
4 other users have reported success in using it for their heavy usage setups.


## What is greylisting?

Greylisting is a new method of blocking significant amounts of spam at the mailserver level, but without resorting to heavyweight statistical analysis or other heuristical (and error-prone) approaches. Consequently, implementations are fairly lightweight, and may even decrease network traffic and processor load on your mailserver.

Greylisting relies on the fact that most spam sources do not behave in the same way as "normal" mail systems. Although it is currently very effective by itself, it will perform best when it is used in conjunction with other forms of spam prevention. For a detailed description of the method,

See http://projects.puremagic.com/greylisting/ for more details.


## What is cqGreylist?

cqgreylist is an implementation of greylisting written in C for qmail, I wrote it in C because the other perl implementation out there were just too slow for my needs and brought the server to its knees with 70-100 concurrent smtp connections, this implementation is intended for heavy traffic servers (250,000+ smtp connections per day).

I wrote this with the help of the two perl implementations found here
* Sirko Zidlewitz http://www.datenklause.de/en/software/qgreylistrbl.html
* Jon Atkins http://www.jonatkins.com/page/software/qgreylist

please note that this implementation provides only greylisting based on the source IP address and not the full triplet as suggested in the greylisting whitepaper.


## Requirements

make, gcc & glibc I geuss, I didn't use any fancy libraries.

## Installing cqGreylist

* unpack

```bash
~# tar -zxvf cqgreylist-x.x.tar.gz
```

* change basic configuration, its noted what variables you can change

```bash
~# cd cqgreylist-x.x
~# vim cqgreylist.c
```

* create the folder to hold the files

```bash
~# mkdir /var/qmail/cqgreylist
~# chown qmaild: /var/qmail/cqgreylist
```

* or the following if you use vpopmail

```bash
~# chown vpopmail: /var/qmail/cqgreylist
```

* compile

```bash
~# make
```

* or if you want to enable debugging messages in your smtpd logs

```bash
~# make dev
```

* copy the binary

```bash
~# cp cqgreylist /var/qmail/bin/
```

* edit the run script for qmail-smtpd, here is mine before

```bash
/usr/local/bin/tcpserver -v -R -l "$LOCAL" -x /etc/tcp.smtp.cdb -c "$MAXSMTPD" \
-u "$QMAILDUID" -g "$NOFILESGID" 0 smtp \
/var/qmail/bin/qmail-smtpd mail.albawaba.com \
/home/vpopmail/bin/vchkpw /usr/bin/true 2>&1
```

* and here it is after

```bash
/usr/local/bin/tcpserver -v -R -l "$LOCAL" -x /etc/tcp.smtp.cdb -c "$MAXSMTPD" \
-u "$QMAILDUID" -g "$NOFILESGID" 0 smtp \
/var/qmail/bin/cqgreylist \
/var/qmail/bin/qmail-smtpd mail.albawaba.com \
/home/vpopmail/bin/vchkpw /usr/bin/true 2>&1
```

you only need to add the binary as a wrapper before qmail-smtpd

* notify tcpserver that the run script has changed

```bash
~# svc -h /service/qmail-smtpd
```

* add this one liner to crontab to clean old files, here I chose 1 day for life of each entry, you can adjust this to your liking.

```bash
23 * * * * /usr/bin/find /var/qmail/cqgreylist -mtime +1 -type f -exec  rm -f {} \;
```


## Whitelisting

to whitelist certain hosts to skip greylisting for them you need to add them to tcpserver's cdb file, cqgreylist checks if the environment variales WHITELISTED or RELAYCLIENT are declared, if any of them is, greylisting is skipped, here is an example from my tcp.smtp file

```bash
127.:allow,RELAYCLIENT=""
10.200.200.:allow,RELAYCLIENT=""

64.124.204.39:allow,WHITELISTED=""
64.125.132.254:allow,WHITELISTED=""

#whitelist a whole C class
66.94.237:allow,WHITELISTED=""
```

please read this page for some hosts that you need to whitelist http://greylisting.org/whitelisting.shtml


## Testing

Send some emails from an external host which **isn't** allowed to relay or is whitelisted. If you don't have an external account send a 'help' command to a majordomo or simillar list server to get a reply.

For each external server which attempts to send mail a file will be created in /var/qmail/cqgreylist/first_octet_of_ip/. Check this is happening. If it is not, check the permissions/owner on the directory - you will not receive any mail until this is fixed.

Watch your smtpd log file and happy greylisting! =)

