#!/bin/bash

qstat

echo ""
echo ""
echo "runner"
cat runner.sh.o$1

echo ""
echo ""
echo "output"
cat output.$1

echo ""
echo ""
echo "error"
cat error.$1


echo ""
echo ""
echo "server"
cat server.$1


echo ""
echo ""
echo "client"
cat client.$1
