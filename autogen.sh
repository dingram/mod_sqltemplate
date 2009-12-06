#!/bin/sh

export WANT_AUTOCONF=2.5

LIBTOOLIZE=libtoolize
AUTOMAKE=automake-1.9
ACLOCAL=aclocal-1.9

# Find the first extant libtoolize on the path out of a number of platform 
# specific possibilities.
for l in libtoolize libtoolize15 libtoolize14 glibtoolize; 
do
    ($l --version) < /dev/null > /dev/null 2>&1 && {
        LIBTOOLIZE=$l
        break
    }
done

# Same for aclocal
for a in aclocal aclocal-1.9 aclocal19 aclocal15 aclocal14; 
do
    ($a --version) < /dev/null > /dev/null 2>&1 && {
        ACLOCAL=$a
        break
    }
done

# Same for automake
for m in automake automake-1.9 automake19 automake15 automake14; 
do
    ($m --version) < /dev/null > /dev/null 2>&1 && {
        AUTOMAKE=$m
        break
    }
done

for h in autoheader autoheader259 autoheader253; 
do
    ($h --version) < /dev/null > /dev/null 2>&1 && {
        AUTOHEADER=$h
        break
    }
done

for c in autoconf autoconf259 autoconf253; 
do
    ($c --version) < /dev/null > /dev/null 2>&1 && {
        AUTOCONF=$c
        break
    }
done


$ACLOCAL -I m4
$AUTOHEADER
$LIBTOOLIZE --force --copy
$AUTOMAKE --add-missing --copy --foreign
$AUTOCONF

rm -rf autom4te.cache
