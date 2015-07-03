#!/bin/sh

aclocal
automake --foreign --add-missing
autoconf
chmod +x ./configure
./configure && make && make install
