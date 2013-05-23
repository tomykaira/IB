#!/bin/bash

qstat

echo ""
echo ""
echo "Output"
cat output.$1.csc.is.s.u-tokyo.ac.jp

echo ""
echo ""
echo "server"
cat server.$1.csc.is.s.u-tokyo.ac.jp

echo ""
echo ""
echo "client"
cat client.$1.csc.is.s.u-tokyo.ac.jp
