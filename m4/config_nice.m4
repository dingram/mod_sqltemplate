dnl -------------------------------------------------------- -*- autoconf -*-
dnl Copyright 2005 The Apache Software Foundation
dnl
dnl Licensed under the Apache License, Version 2.0 (the "License");
dnl you may not use this file except in compliance with the License.
dnl You may obtain a copy of the License at
dnl
dnl     http://www.apache.org/licenses/LICENSE-2.0
dnl
dnl Unless required by applicable law or agreed to in writing, software
dnl distributed under the License is distributed on an "AS IS" BASIS,
dnl WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
dnl See the License for the specific language governing permissions and
dnl limitations under the License.

dnl this writes a "config.nice" file which reinvokes ./configure with all
dnl of the arguments. this is different from config.status which simply
dnl regenerates the output files. config.nice is useful after you rebuild
dnl ./configure (via autoconf or autogen.sh)
AC_DEFUN([MAKE_CONFIG_NICE],[
  echo configure: creating $1
  rm -f $1
  cat >$1<<EOF
#! /bin/sh
#
# Created by configure

EOF

  for arg in [$]0 "[$]@"; do
    if test "[$]arg" != "--no-create" -a "[$]arg" != "--no-recursion"; then
        echo "\"[$]arg\" \\" >> $1
    fi
  done
  echo '"[$]@"' >> $1
  chmod +x $1
])
