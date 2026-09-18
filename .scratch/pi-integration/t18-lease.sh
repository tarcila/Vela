#!/usr/bin/env bash
# Lease matrix against the headless host, using vsrMcpProxy directly over stdio.
P=/home/tarcila/Code/ANARI/Vela/pi-integration/_out/_cmake/Release/vsrMcpProxy
export LD_LIBRARY_PATH=/home/tarcila/Code/ANARI/Vela/pi-integration/_out/_cmake/Release
D=$(cat /tmp/t18-host.out)
ts(){ date +%H:%M:%S; }
rm -f /tmp/t18-in; mkfifo /tmp/t18-in
$P "$D" < /tmp/t18-in > /tmp/t18-p1.out 2>/tmp/t18-p1.err & P1=$!
exec 3>/tmp/t18-in
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"t18","version":"1"}}}' >&3
echo '{"jsonrpc":"2.0","method":"notifications/initialized"}' >&3
echo '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"vsr_discover","arguments":{}}}' >&3
sleep 3
echo "$(ts) P1 pid=$P1 started; responses so far: $(wc -l < /tmp/t18-p1.out)"
$P "$D" < /dev/null > /tmp/t18-p2.out 2>/tmp/t18-p2.err; echo "$(ts) P2 (second client) rc=$? err=$(cat /tmp/t18-p2.err)"
echo "$(ts) P1 idle for 150 s"
sleep 150
echo '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"vsr_discover","arguments":{}}}' >&3
echo '{"jsonrpc":"2.0","id":4,"method":"vsr/attach","params":{}}' >&3
sleep 3
echo "$(ts) P1 after idle: $(tail -2 /tmp/t18-p1.out | cut -c1-200)"
echo "$(ts) P1 stderr: $(cat /tmp/t18-p1.err)"
kill -9 $P1; exec 3>&-; echo "$(ts) P1 SIGKILLed (no detach)"
sleep 2
timeout 5 $P "$D" < /dev/null > /tmp/t18-p3.out 2>/tmp/t18-p3.err; echo "$(ts) P3 right after abandonment rc=$? err=$(cat /tmp/t18-p3.err)"
sleep 125
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"t18","version":"1"}}}' | timeout 10 $P "$D" > /tmp/t18-p4.out 2>/tmp/t18-p4.err; echo "$(ts) P4 after lease expiry rc=$? out=$(cut -c1-160 /tmp/t18-p4.out) err=$(cat /tmp/t18-p4.err)"
