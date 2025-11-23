#!/bin/sh

./autogen.sh
CPPFLAGS="-DTEST_MODE" ./configure --enable-compat --enable-examples --with-filenr --with-fsmon --with-generic --with-loadavg --with-meminfo --with-tempmon
make -j5 clean
make -j5 all

