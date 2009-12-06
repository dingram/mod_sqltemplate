#! /bin/bash

APR_MEMCACHE=/usr/local/apache-2.2.9
APXS_PATH=/usr/local/apache-2.2.9/bin/apxs
APACHECTL_PATH=/usr/local/apache-2.2.9/bin/apachectl

make clean

./autogen.sh && \
./configure --with-apr-memcache=$APR_MEMCACHE --with-apxs=$APXS_PATH && \
make && \
make install && \
sudo $APACHECTL_PATH restart