#!/bin/bash
# Joining a game in progress: host + client + late joiner (and optionally a second late joiner) as separate
# instances on this machine. All of them must end with the same state hash and no desync.
#
#   tools/test_latejoin.sh            (run from the build directory, or set BEGOTTEN_BIN)
#
# environment:  BEGOTTEN_BIN (default ./begotten)  IWAD (default begotten.wad next to it)  DATAWAD (begotten-data.wad)
#               TICS (default 1800)   LATEMS wall-clock ms before the joiner connects (default 9000)
#               JOINAT first tic at which the observer presses Use (0: at once)   NM (default -nomonsters)
#               LEAVEA  tic at which the first late joiner goes away    LATE2=1  a second late joiner (LATEMS2, JOINAT2)
#               BEGOTTEN_NETTEST_EXIT=<tic>  everybody leaves the level at that tic
# Windowless: the instances use SDL_VIDEODRIVER=dummy; set SHOW=1 for the host to open a window (needs DISPLAY).
# Results: /tmp/nt1..4/out.txt ("NETTEST: slot N tics .. hash .. players .. desyncs .." per instance).
BIN=${BEGOTTEN_BIN:-./begotten}; IWAD=${IWAD:-begotten.wad}; DATA=${DATAWAD:-begotten-data.wad}
T=${TICS:-1800}
pkill -x begotten 2>/dev/null; sleep 1; mkdir -p /tmp/nt1 /tmp/nt2 /tmp/nt3 /tmp/nt4
CMD="$BIN -iwad $IWAD -file $DATA -window -nosound -nomusic -skill ${SKILL:-4} -warp 1 ${NM--nomonsters}"
V=${SHOW:+NOP=1}; V=${V:-SDL_VIDEODRIVER=dummy}
(env $V BEGOTTEN_NETTEST=host:2 BEGOTTEN_NETTEST_TICS=$T nohup $CMD > /tmp/nt1/out.txt 2>&1 &)
sleep 3
(env SDL_VIDEODRIVER=dummy BEGOTTEN_NETTEST=join:127.0.0.1 BEGOTTEN_NETTEST_TICS=$T nohup $CMD > /tmp/nt2/out.txt 2>&1 &)
(env SDL_VIDEODRIVER=dummy ${LEAVEA:+BEGOTTEN_NETTEST_LEAVE=$LEAVEA} BEGOTTEN_NETTEST=late:127.0.0.1 BEGOTTEN_NETTEST_LATEMS=${LATEMS:-9000} \
  BEGOTTEN_NETTEST_JOINAT=${JOINAT:-0} BEGOTTEN_NETTEST_TICS=$T nohup $CMD > /tmp/nt3/out.txt 2>&1 &)
if [ -n "$LATE2" ]; then
(env SDL_VIDEODRIVER=dummy BEGOTTEN_NETTEST=late:127.0.0.1 BEGOTTEN_NETTEST_LATEMS=${LATEMS2:-16000} \
  BEGOTTEN_NETTEST_JOINAT=${JOINAT2:-0} BEGOTTEN_NETTEST_TICS=$T nohup $CMD > /tmp/nt4/out.txt 2>&1 &)
fi
echo "started; results in /tmp/nt*/out.txt in about $((T / 35 + 30)) seconds"
