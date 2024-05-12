#!/bin/bash

make

mkdir -p ./tmp
echo "Hello" > ./tmp/in1
echo "Hello" > ./tmp/in2
echo "Hello" > ./tmp/in3

cat > config.txt <<EOF
/bin/sleep 60 $PWD/tmp/in1 $PWD/tmp/out1
/bin/sleep 120 $PWD/tmp/in2 $PWD/tmp/out2
/bin/sleep 180 $PWD/tmp/in3 $PWD/tmp/out3
EOF

./myinit $PWD/config.txt

sleep 1

ps --no-headers --ppid $(pgrep myinit) | wc -l
if [[ $(ps --no-headers --ppid $(pgrep myinit) | wc -l) -eq 3 ]]; then
    echo "Test 1 Passed: 3 children are running."
else
    echo "Test 1 Failed: Not all children are running."
fi

pkill -f "/bin/sleep 120"

sleep 1

ps --no-headers --ppid $(pgrep myinit) | wc -l
if [[ $(ps --no-headers --ppid $(pgrep myinit) | wc -l) -eq 3 ]]; then
    echo "Test 2 Passed: 3 children are still running after killing one."
else
    echo "Test 2 Failed: Incorrect number of children running after killing one."
fi

# Изменяем конфигурационный файл
cat > config.txt <<EOF
/bin/sleep 30 $PWD/tmp/in1 $PWD/tmp/out1
EOF

# Отправляем SIGHUP
pkill -HUP -f "./myinit"

sleep 1

ps --no-headers --ppid $(pgrep myinit) | wc -l
if [[ $(ps --no-headers --ppid $(pgrep myinit) | wc -l) -eq 1 ]]; then
    echo "Test 3 Passed: Only one child is running after SIGHUP."
else
    echo "Test 3 Failed: Incorrect number of children running after SIGHUP."
fi

killall myinit

sleep 1

echo "Log contents:"
echo "$(cat /tmp/myinit.log)"