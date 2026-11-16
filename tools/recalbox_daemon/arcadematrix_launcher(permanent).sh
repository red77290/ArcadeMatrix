#!/bin/sh
# ArcadeMatrix daemon launcher & watchdog (Recalbox).
# Ensures arcadematrix_daemon.py is running in its own detached session (setsid + SIGHUP-safe)
# without ever terminating a healthy running instance.
case " $* " in
    *" -action shutdown "*|*" -action reboot "*|*" -action stop "*|*" -action quit "*)
        exit 0
        ;;
esac

if ! (pgrep -f '[a]rcadematrix_daemon.py' >/dev/null 2>&1 || (ps w 2>/dev/null || ps) | grep -q '[a]rcadematrix_daemon.py'); then
    if command -v setsid >/dev/null 2>&1; then
        setsid python3 /recalbox/share/arcadematrix_daemon.py </dev/null >>/recalbox/share/userscripts/daemon.log 2>&1 &
    else
        nohup python3 /recalbox/share/arcadematrix_daemon.py </dev/null >>/recalbox/share/userscripts/daemon.log 2>&1 &
    fi
fi
exit 0
